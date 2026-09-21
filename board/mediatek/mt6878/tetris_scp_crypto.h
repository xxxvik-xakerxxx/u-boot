/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_SCP_CRYPTO_H
#define __TETRIS_SCP_CRYPTO_H

#ifdef TETRIS_SCP_CRYPTO_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include <linux/types.h>
#endif

/* Fixed service ABI in the audited ATF, not a SCP allocation address. */
#define TETRIS_SCP_CRYPTO_PAGE_PA 0x48401000ULL
#define TETRIS_SCP_CRYPTO_PAGE_SIZE 4096U
#define TETRIS_SCP_CRYPTO_INIT 0xc200010bU
#define TETRIS_SCP_CRYPTO_DECRYPT 0xc2000133U
#define TETRIS_SCP_CRYPTO_MAX_IMAGE 0xe00000U

struct tetris_scp_crypto_ops {
	u64 (*smc)(u32 function, u64 arg1, u64 arg2);
	void (*flush)(void *buffer, size_t size);
	void (*invalidate)(void *buffer, size_t size);
	void (*sha256)(const void *buffer, u32 size, u8 digest[32]);
	u64 (*physical)(const void *buffer);
	size_t cache_alignment;
};

enum tetris_scp_crypto_state {
	TETRIS_SCP_CRYPTO_NEW,
	TETRIS_SCP_CRYPTO_READY,
	TETRIS_SCP_CRYPTO_FAILED,
};

struct tetris_scp_crypto {
	const struct tetris_scp_crypto_ops *ops;
	void *page;
	enum tetris_scp_crypto_state state;
	u64 secure_error;
};

/*
 * Caller owns/exclusively reserves the mapped service page and each image's
 * full capacity, checks the ATF identity and verifies certificate policy.
 * This transport does not authenticate metadata or allocate/reserve memory.
 */
int tetris_scp_crypto_init(struct tetris_scp_crypto *ctx, void *page,
			   size_t page_size,
			   const struct tetris_scp_crypto_ops *ops);
int tetris_scp_crypto_check_image(const struct tetris_scp_crypto_ops *ops,
				 void *image, size_t size, size_t capacity);
int tetris_scp_crypto_decrypt(struct tetris_scp_crypto *ctx, void *image,
			      u32 size, size_t capacity, u32 selector,
			      const u8 wrapped[32], const u8 ciphertext_sha256[32],
			      const u8 plaintext_sha256[32]);

#ifndef TETRIS_SCP_CRYPTO_HOST_TEST
extern const struct tetris_scp_crypto_ops tetris_scp_crypto_hw_ops;
#endif

#endif
