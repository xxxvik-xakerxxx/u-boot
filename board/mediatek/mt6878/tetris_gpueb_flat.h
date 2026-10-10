/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TETRIS_GPUEB_FLAT_H
#define TETRIS_GPUEB_FLAT_H
#include "tetris_scp_crypto.h"
#include "tetris_scp_security.h"

struct tetris_gpueb_flat;
struct tetris_gpueb_flat_info {
	u64 reserved_base;
	u32 reserved_bytes;
	u32 authenticated_bytes;
	u32 sram_offset;
	u8 plaintext_sha256[32];
};

/* Separate opt-in producer; never changes transform-only erasure semantics.
 * *result must initially be NULL and is unchanged on failure. Success owns an
 * LMB reservation, not a firmware execution permission.
 */
int tetris_gpueb_flat_retain(struct tetris_scp_crypto *crypto,
	const struct tetris_scp_security_ops *security,
	const void *container, size_t bytes, const u8 root_pin[32],
	struct tetris_gpueb_flat **result);
int tetris_gpueb_flat_describe(const struct tetris_gpueb_flat *image,
	struct tetris_gpueb_flat_info *info);
/* Allowed only before any consumer is given ownership of this reservation. */
int tetris_gpueb_flat_discard(struct tetris_gpueb_flat *image);
#endif
