/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TETRIS_GPUEB_FLAT_H
#define TETRIS_GPUEB_FLAT_H
#include "tetris_scp_crypto.h"
#include "tetris_scp_security.h"

struct tetris_gpueb_flat;
#define TETRIS_GPUEB_FLAT_HANDOFF_VERSION 1U
/* Stable reviewed plaintext profiles: phone-a and cached B4.1 distribution. */
#define TETRIS_GPUEB_FLAT_PROFILE_PHONE_A 1U
#define TETRIS_GPUEB_FLAT_PROFILE_B41 2U
struct tetris_gpueb_flat_info {
	u64 reserved_base;
	u32 reserved_bytes;
	u32 handoff_version;
	u32 payload_offset;
	u32 authenticated_bytes;
	u32 sram_offset;
	u32 profile_id;
	u8 plaintext_sha256[32];
	u8 primary_header_sha256[32];
	u8 ciphertext_sha256[32];
	u8 leaf_certificate_sha256[32];
	/* Whole input framing identity; only the primary member is authenticated. */
	u8 container_sha256[32];
};

/* Separate opt-in producer; never changes transform-only erasure semantics.
 * *result must initially be NULL and is unchanged on failure. Success owns an
 * LMB reservation, not a firmware execution permission.
 */
int tetris_gpueb_flat_retain(struct tetris_scp_crypto *crypto,
	const struct tetris_scp_security_ops *security,
	const void *container, size_t bytes, const u8 root_pin[32],
	struct tetris_gpueb_flat **result);
/* Revalidate immutable bounds/profile and retained digest; failure leaves
 * *info unchanged. Offset zero is this producer's layout, not LK prefix usage.
 */
int tetris_gpueb_flat_describe(const struct tetris_gpueb_flat *image,
	struct tetris_gpueb_flat_info *info);
/* Allowed only before any consumer is given ownership of this reservation. */
int tetris_gpueb_flat_discard(struct tetris_gpueb_flat *image);
#endif
