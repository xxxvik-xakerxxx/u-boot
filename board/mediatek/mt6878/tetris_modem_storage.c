// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#else
#include <blk.h>
#include <part.h>
#include <asm/cache.h>
#include <linux/errno.h>
#endif
#include "tetris_modem_storage.h"
#include "tetris_scp_security.h"

#define MAX_SNAPSHOT (256U * 1024 * 1024)
#define READ_CHUNK (64U * 1024)

int tetris_modem_read_bundle(const struct tetris_modem_storage *storage,
		void *buffer, size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle)
{
	unsigned long long done = 0, count, chunk;
	size_t bytes;

	if (!storage || !storage->read || !buffer || !root_pin || !ops ||
	    !ops->sha256 || !ops->verify || !bundle || !reserved_capacity)
		return -EINVAL;
	if (storage->block_size != 512 && storage->block_size != 4096)
		return -EPROTONOSUPPORT;
	if (!storage->blocks || storage->start >= storage->device_blocks ||
	    storage->blocks > storage->device_blocks - storage->start ||
	    storage->blocks > MAX_SNAPSHOT / storage->block_size)
		return -ERANGE;
	bytes = storage->blocks * storage->block_size;
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

#ifndef TETRIS_MODEM_LAYOUT_HOST_TEST
static unsigned long long read_blocks(void *ctx, unsigned long long block,
				     unsigned long long count, void *buffer)
{
	return blk_dread(ctx, block, count, buffer);
}

int tetris_modem_read_slot(struct blk_desc *dev, char slot, void *buffer,
		size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle)
{
	struct disk_partition part;
	struct tetris_modem_storage storage;
	const char *name;
	int ret;

	if (!dev || !buffer || (unsigned long)buffer % ARCH_DMA_MINALIGN)
		return -EINVAL;
	if (dev->blksz != 512 && dev->blksz != 4096)
		return -EPROTONOSUPPORT;
	if (slot != 'a' && slot != 'b')
		return -EINVAL;
	name = slot == 'a' ? "md1img_a" : "md1img_b";
	ret = part_get_info_by_name(dev, name, &part);
	if (ret < 0)
		return ret;
	if (part.blksz != dev->blksz)
		return -EINVAL;
	storage.device_blocks = dev->lba;
	storage.start = part.start;
	storage.blocks = part.size;
	storage.block_size = dev->blksz;
	storage.read = read_blocks;
	storage.ctx = dev;
	return tetris_modem_read_bundle(&storage, buffer, capacity, root_pin, ops,
				       reserved_capacity, bundle);
}
#endif
