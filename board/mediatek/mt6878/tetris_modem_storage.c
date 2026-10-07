// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#else
#include <blk.h>
#include <part.h>
#include <lmb.h>
#include <mapmem.h>
#include <stdio.h>
#include <asm/cache.h>
#include <linux/errno.h>
#endif
#include "tetris_modem_storage.h"
#include "tetris_scp_security.h"

#define MAX_SNAPSHOT (256U * 1024 * 1024)
#define READ_CHUNK (64U * 1024)

static int snapshot_size(const struct tetris_modem_storage *storage, size_t *bytes)
{
	if (!storage || !storage->read)
		return -EINVAL;
	if (storage->block_size != 512 && storage->block_size != 4096)
		return -EPROTONOSUPPORT;
	if (!storage->blocks || storage->start >= storage->device_blocks ||
	    storage->blocks > storage->device_blocks - storage->start ||
	    storage->blocks > MAX_SNAPSHOT / storage->block_size)
		return -ERANGE;
	*bytes = storage->blocks * storage->block_size;
	return 0;
}

int tetris_modem_read_bundle(const struct tetris_modem_storage *storage,
		void *buffer, size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle)
{
	unsigned long long done = 0, count, chunk;
	size_t bytes;
	int ret;

	if (!storage || !storage->read || !buffer || !root_pin || !ops ||
	    !ops->sha256 || !ops->verify || !bundle || !reserved_capacity)
		return -EINVAL;
	ret = snapshot_size(storage, &bytes);
	if (ret)
		return ret;
	if (bytes > capacity)
		return -ENOSPC;
	chunk = READ_CHUNK / storage->block_size;
	while (done < storage->blocks) {
		count = storage->blocks - done;
		if (count > chunk)
			count = chunk;
		if (storage->read(storage->ctx, storage->start + done, count,
			(unsigned char *)buffer + done * storage->block_size) != count)
			return -EIO;
		done += count;
	}
	return tetris_modem_authenticate_bundle(buffer, bytes, root_pin, ops,
					      reserved_capacity, bundle);
}

int tetris_modem_stage_bundle(const struct tetris_modem_storage *storage,
		const struct tetris_modem_staging_ops *memory,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_layout *layout)
{
	struct tetris_modem_bundle bundle;
	void *buffer = NULL;
	size_t bytes;
	int ret, release_ret;

	if (!memory || !memory->acquire || !memory->release || !root_pin ||
	    !ops || !ops->sha256 || !ops->verify || !reserved_capacity || !layout)
		return -EINVAL;
	ret = snapshot_size(storage, &bytes);
	if (ret)
		return ret;
	ret = memory->acquire(memory->ctx, bytes, &buffer);
	if (ret)
		return ret;
	ret = tetris_modem_read_bundle(storage, buffer, bytes, root_pin, ops,
				       reserved_capacity, &bundle);
	release_ret = memory->release(memory->ctx, buffer, bytes);
	if (ret)
		return ret;
	if (release_ret)
		return release_ret;
	*layout = bundle.layout;
	return 0;
}

#ifndef TETRIS_MODEM_LAYOUT_HOST_TEST
static unsigned long long read_blocks(void *ctx, unsigned long long block,
				     unsigned long long count, void *buffer)
{
	return blk_dread(ctx, block, count, buffer);
}

static int slot_storage(struct blk_desc *dev, char slot,
			struct tetris_modem_storage *storage)
{
	struct disk_partition part;
	const char *name;
	int ret;

	if (!dev)
		return -EINVAL;
	if (dev->blksz != 512 && dev->blksz != 4096)
		return -EPROTONOSUPPORT;
	if (slot != 'a' && slot != 'b')
		return -EINVAL;
	/* B4.1 LK platform table overrides the generic md1img base with modem. */
	name = slot == 'a' ? "modem_a" : "modem_b";
	ret = part_get_info_by_name(dev, name, &part);
	if (ret < 0)
		return ret;
	if (part.blksz != dev->blksz)
		return -EINVAL;
	storage->device_blocks = dev->lba;
	storage->start = part.start;
	storage->blocks = part.size;
	storage->block_size = dev->blksz;
	storage->read = read_blocks;
	storage->ctx = dev;
	return 0;
}

int tetris_modem_read_slot(struct blk_desc *dev, char slot, void *buffer,
		size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle)
{
	struct tetris_modem_storage storage;
	int ret;

	if (!buffer || (unsigned long)buffer % ARCH_DMA_MINALIGN)
		return -EINVAL;
	ret = slot_storage(dev, slot, &storage);
	if (ret)
		return ret;
	return tetris_modem_read_bundle(&storage, buffer, capacity, root_pin, ops,
				       reserved_capacity, bundle);
}

struct staging_allocation {
	phys_addr_t base;
};

static int acquire_staging(void *ctx, size_t size, void **buffer)
{
	struct staging_allocation *allocation = ctx;
	int ret;

	ret = lmb_alloc_mem(LMB_MEM_ALLOC_ANY, READ_CHUNK, &allocation->base,
			    size, LMB_NOOVERWRITE);
	if (ret)
		return ret;
	/* ARM64 map_sysmem is a direct map; no heap-sized allocation is involved. */
	*buffer = map_sysmem(allocation->base, size);
	return 0;
}

static int release_staging(void *ctx, void *buffer, size_t size)
{
	struct staging_allocation *allocation = ctx;
	long ret;

	unmap_sysmem(buffer);
	ret = lmb_free(allocation->base, size, LMB_NOOVERWRITE);
	if (ret)
		printf("Tetris modem staging release failed: %ld\n", ret);
	return ret;
}

int tetris_modem_stage_slot(struct blk_desc *dev, char slot,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_layout *layout)
{
	struct staging_allocation allocation = { 0 };
	const struct tetris_modem_staging_ops memory = {
		.acquire = acquire_staging,
		.release = release_staging,
		.ctx = &allocation,
	};
	struct tetris_modem_storage storage;
	int ret;

	ret = slot_storage(dev, slot, &storage);
	if (ret)
		return ret;
	return tetris_modem_stage_bundle(&storage, &memory, root_pin, ops,
					reserved_capacity, layout);
}
#endif
