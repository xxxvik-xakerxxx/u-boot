/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_STORAGE_H
#define __TETRIS_MODEM_STORAGE_H

#include "tetris_modem_bundle.h"

struct tetris_modem_storage {
	unsigned long long device_blocks;
	unsigned long long start;
	unsigned long long blocks;
	unsigned int block_size;
	/* Return the exact number of blocks read. Short reads are fatal. */
	unsigned long long (*read)(void *ctx, unsigned long long block,
				   unsigned long long count, void *buffer);
	void *ctx;
};

/*
 * Read the whole bounded partition into caller-owned staging RAM, then run
 * bundle authentication. Never use modem destination RAM as staging storage.
 * Buffer may contain partial/untrusted input on failure; output is unchanged.
 * Caller owns buffer exclusively and must keep it immutable after success.
 */
int tetris_modem_read_bundle(const struct tetris_modem_storage *storage,
		void *buffer, size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle);

struct blk_desc;
/* Explicit, externally established slot 'a' or 'b'; no fallback or inference. */
int tetris_modem_read_slot(struct blk_desc *dev, char slot, void *buffer,
		size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle);

#endif
