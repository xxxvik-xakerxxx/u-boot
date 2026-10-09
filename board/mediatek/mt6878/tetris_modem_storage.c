// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#else
#include <blk.h>
#include <cpu_func.h>
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

static int read_snapshot(const struct tetris_modem_storage *storage,
			 void *buffer, size_t capacity, size_t *size)
{
	unsigned long long done = 0, count, chunk;
	size_t bytes;
	int ret;

	if (!buffer)
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
	*size = bytes;
	return 0;
}

int tetris_modem_read_bundle(const struct tetris_modem_storage *storage,
		void *buffer, size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle)
{
	size_t bytes;
	int ret;

	if (!root_pin || !ops || !ops->sha256 || !ops->verify || !bundle ||
	    !reserved_capacity)
		return -EINVAL;
	ret = read_snapshot(storage, buffer, capacity, &bytes);
	if (ret)
		return ret;
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

static int separate(const void *a, size_t a_size, const void *b, size_t b_size)
{
	unsigned long x = (unsigned long)a, y = (unsigned long)b;

	return x && y && a_size && b_size && a_size <= ~0UL - x &&
	       b_size <= ~0UL - y && (x + a_size <= y || y + b_size <= x);
}

int tetris_modem_load_bundle(const struct tetris_modem_storage *storage,
		const struct tetris_modem_staging_ops *memory,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity,
		struct tetris_modem_layout *layout)
{
	struct tetris_modem_layout result;
	void *buffer = NULL;
	size_t bytes, used;
	int ret, release_ret;

	if (!memory || !memory->acquire || !memory->release || !root_pin ||
	    !ops || !ops->sha256 || !ops->verify ||
	    !separate(destination, capacity, layout, sizeof(*layout)))
		return -EINVAL;
	ret = snapshot_size(storage, &bytes);
	if (ret)
		return ret;
	ret = memory->acquire(memory->ctx, bytes, &buffer);
	if (ret)
		return ret;
	/* Reject aliasing before storage DMA can overwrite destination or output. */
	if (!separate(buffer, bytes, destination, capacity) ||
	    !separate(buffer, bytes, layout, sizeof(*layout))) {
		ret = -EINVAL;
	} else {
		ret = read_snapshot(storage, buffer, bytes, &used);
		if (!ret)
			ret = tetris_modem_place_bundle(buffer, used, root_pin, ops,
						 destination, capacity, &result);
	}
	release_ret = memory->release(memory->ctx, buffer, bytes);
	if (ret)
		return ret;
	if (release_ret)
		return release_ret;
	*layout = result;
	return 0;
}

int tetris_modem_load_bundle_b41(const struct tetris_modem_storage *storage,
		const struct tetris_modem_staging_ops *memory,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity, unsigned int ccb_gear,
		struct tetris_modem_boot_plan *plan)
{
	struct tetris_modem_boot_plan result;
	void *buffer = NULL;
	size_t bytes, used;
	int ret, release_ret;

	if (!memory || !memory->acquire || !memory->release || !root_pin ||
	    !ops || !ops->sha256 || !ops->verify ||
	    !separate(destination, capacity, plan, sizeof(*plan)))
		return -EINVAL;
	ret = snapshot_size(storage, &bytes);
	if (ret)
		return ret;
	ret = memory->acquire(memory->ctx, bytes, &buffer);
	if (ret)
		return ret;
	if (!separate(buffer, bytes, destination, capacity) ||
	    !separate(buffer, bytes, plan, sizeof(*plan))) {
		ret = -EINVAL;
	} else {
		ret = read_snapshot(storage, buffer, bytes, &used);
		if (!ret)
			ret = tetris_modem_place_bundle_b41(buffer, used, root_pin, ops,
						     destination, capacity, ccb_gear,
						     &result);
	}
	release_ret = memory->release(memory->ctx, buffer, bytes);
	if (ret)
		return ret;
	if (release_ret)
		return release_ret;
	*plan = result;
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

static int flush_payload(void *ctx, unsigned long start, unsigned long end)
{
	(void)ctx;
	/* ARM64 flush_dcache_range completes with dsb sy. */
	flush_dcache_range(start, end);
	return 0;
}

int tetris_modem_load_slot(struct blk_desc *dev, char slot,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity,
		struct tetris_modem_layout *layout)
{
	const struct tetris_modem_cache_ops cache = { .flush = flush_payload };
	struct tetris_modem_layout loaded;
	struct staging_allocation allocation = { 0 };
	const struct tetris_modem_staging_ops memory = {
		.acquire = acquire_staging,
		.release = release_staging,
		.ctx = &allocation,
	};
	struct tetris_modem_storage storage;
	int ret;

	if (!layout || !separate(destination, capacity, layout, sizeof(*layout)) ||
	    (((unsigned long)destination | capacity) & (ARCH_DMA_MINALIGN - 1)))
		return -EINVAL;
	ret = slot_storage(dev, slot, &storage);
	if (ret)
		return ret;
	ret = tetris_modem_load_bundle(&storage, &memory, root_pin, ops,
				       destination, capacity, &loaded);
	if (ret)
		return ret;
	ret = tetris_modem_sync_payloads(destination, capacity, &loaded,
					ARCH_DMA_MINALIGN, &cache);
	if (ret)
		return ret;
	*layout = loaded;
	return 0;
}

int tetris_modem_load_slot_b41(struct blk_desc *dev, char slot,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity, unsigned int ccb_gear,
		struct tetris_modem_boot_plan *plan)
{
	const struct tetris_modem_cache_ops cache = { .flush = flush_payload };
	struct tetris_modem_boot_plan loaded;
	struct staging_allocation allocation = { 0 };
	const struct tetris_modem_staging_ops memory = {
		.acquire = acquire_staging,
		.release = release_staging,
		.ctx = &allocation,
	};
	struct tetris_modem_storage storage;
	int ret;

	if (!separate(destination, capacity, plan, sizeof(*plan)) ||
	    (((unsigned long)destination | capacity) & (ARCH_DMA_MINALIGN - 1)))
		return -EINVAL;
	ret = slot_storage(dev, slot, &storage);
	if (ret)
		return ret;
	ret = tetris_modem_load_bundle_b41(&storage, &memory, root_pin, ops,
					   destination, capacity, ccb_gear, &loaded);
	if (ret)
		return ret;
	ret = tetris_modem_sync_payloads(destination, capacity, &loaded.layout,
					ARCH_DMA_MINALIGN, &cache);
	if (ret)
		return ret;
	*plan = loaded;
	return 0;
}
#endif
