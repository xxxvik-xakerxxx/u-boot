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
struct tetris_modem_staging_ops {
	/* Failure must leave no allocation; success transfers exclusive ownership. */
	int (*acquire)(void *ctx, size_t size, void **buffer);
	/* Called exactly once after successful acquire, including read failures. */
	int (*release)(void *ctx, void *buffer, size_t size);
	void *ctx;
};

/*
 * Scoped diagnostic: allocate, read/authenticate, release, then publish a
 * pointer-free layout. No payload survives for execution. Release failure
 * suppresses output; an earlier read/authentication error takes precedence.
 */
int tetris_modem_stage_bundle(const struct tetris_modem_storage *storage,
		const struct tetris_modem_staging_ops *memory,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_layout *layout);

/* U-Boot LMB-backed staging; call only after image/firmware reservations. */
int tetris_modem_stage_slot(struct blk_desc *dev, char slot,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_layout *layout);

/*
 * Read, authenticate and place payloads before releasing the snapshot. Caller
 * owns destination exclusively; allocator/releaser must not modify other RAM.
 * Layout is published only after successful release. A release failure may
 * leave verified payload bytes in destination, but MUST NOT authorize boot.
 * No cache flush, SMC, reset or DT publication. Placement policy still applies.
 */
int tetris_modem_load_bundle(const struct tetris_modem_storage *storage,
		const struct tetris_modem_staging_ops *memory,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity,
		struct tetris_modem_layout *layout);

/*
 * Destination must already be reserved in LMB and mapped as writable RAM,
 * with base and capacity aligned to ARCH_DMA_MINALIGN. Flushes ROM/DSP to
 * coherency before publishing layout. No automatic caller or reset release.
 */
int tetris_modem_load_slot(struct blk_desc *dev, char slot,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity,
		struct tetris_modem_layout *layout);

/* Explicit, externally established slot 'a' or 'b'; no fallback or inference. */
int tetris_modem_read_slot(struct blk_desc *dev, char slot, void *buffer,
		size_t capacity, const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops, size_t reserved_capacity,
		struct tetris_modem_bundle *bundle);

#endif
