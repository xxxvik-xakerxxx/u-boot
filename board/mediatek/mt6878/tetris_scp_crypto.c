// SPDX-License-Identifier: GPL-2.0+
#include "tetris_scp_crypto.h"

#ifdef TETRIS_SCP_CRYPTO_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <asm/cache.h>
#include <cpu_func.h>
#include <mapmem.h>
#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <u-boot/sha256.h>
#endif

#define DESCRIPTOR_OFFSET 0x40U
#define MATERIAL_OFFSET 0x100U

static void erase(void *buffer, size_t size)
{
	volatile u8 *p = buffer;

	while (size--)
		*p++ = 0;
}

static void put_le64(u8 *p, u64 value)
{
	size_t i;

	for (i = 0; i < 8; i++)
		p[i] = value >> (8 * i);
}

static bool digest_equal(const u8 *a, const u8 *b)
{
	u8 difference = 0;
	size_t i;

	for (i = 0; i < 32; i++)
		difference |= a[i] ^ b[i];
	return !difference;
}

int tetris_scp_crypto_init(struct tetris_scp_crypto *ctx, void *page,
			   size_t page_size,
			   const struct tetris_scp_crypto_ops *ops)
{
	size_t align;

	if (!ctx || ctx->state != TETRIS_SCP_CRYPTO_NEW || !page || !ops ||
	    !ops->smc || !ops->flush || !ops->invalidate || !ops->sha256 ||
	    !ops->physical || page_size != TETRIS_SCP_CRYPTO_PAGE_SIZE)
		return -EINVAL;
	align = ops->cache_alignment;
	if (!align || (align & (align - 1)) || align > page_size ||
	    ((unsigned long)page & (align - 1)) ||
	    ops->physical(page) != TETRIS_SCP_CRYPTO_PAGE_PA)
		return -EINVAL;
	ctx->ops = ops;
	ctx->page = page;
	ctx->state = TETRIS_SCP_CRYPTO_FAILED;
	ctx->secure_error = ops->smc(TETRIS_SCP_CRYPTO_INIT,
				    TETRIS_SCP_CRYPTO_PAGE_PA, page_size);
	if (ctx->secure_error)
		return -EIO;
	ctx->state = TETRIS_SCP_CRYPTO_READY;
	return 0;
}

int tetris_scp_crypto_decrypt(struct tetris_scp_crypto *ctx, void *image,
			      u32 size, size_t capacity, u32 selector,
			      const u8 wrapped[32], const u8 ciphertext_sha256[32],
			      const u8 plaintext_sha256[32])
{
	u8 expected[32], digest[32], material[32];
	const struct tetris_scp_crypto_ops *ops;
	u64 address, end;
	size_t span, align;
	u8 *page;
	int ret = 0;

	if (!ctx || ctx->state != TETRIS_SCP_CRYPTO_READY || !image ||
	    !wrapped || !ciphertext_sha256 || !plaintext_sha256 ||
	    !size || size > TETRIS_SCP_CRYPTO_MAX_IMAGE || (size & 15))
		return -EINVAL;
	/* Only the wrapped-key selector traced in this ATF is supported. */
	if (selector != 1)
		return -EOPNOTSUPP;
	ops = ctx->ops;
	align = ops->cache_alignment;
	span = (size + align - 1) & ~(align - 1);
	if (capacity < span || ((unsigned long)image & (align - 1)))
		return -EINVAL;
	address = ops->physical(image);
	if (address < 0x40000000ULL || address >= (1ULL << 32) ||
	    span > (1ULL << 32) - address || (address & (align - 1)))
		return -ERANGE;
	end = address + span;
	if (address < TETRIS_SCP_CRYPTO_PAGE_PA + TETRIS_SCP_CRYPTO_PAGE_SIZE &&
	    end > TETRIS_SCP_CRYPTO_PAGE_PA)
		return -EINVAL;
	ops->sha256(image, size, digest);
	if (!digest_equal(digest, ciphertext_sha256)) {
		erase(digest, sizeof(digest));
		return -EBADMSG;
	}
	memcpy(expected, plaintext_sha256, sizeof(expected));
	memcpy(material, wrapped, sizeof(material));
	page = ctx->page;
	/* Explicit little-endian layout, including zeroed u32 padding. */
	memset(page + DESCRIPTOR_OFFSET, 0, 32);
	put_le64(page + 0x40, address);
	put_le64(page + 0x48, size);
	put_le64(page + 0x50, TETRIS_SCP_CRYPTO_PAGE_PA + MATERIAL_OFFSET);
	put_le64(page + 0x58, 32);
	memcpy(page + MATERIAL_OFFSET, material, sizeof(material));
	ops->flush(image, span);
	ops->flush(page, TETRIS_SCP_CRYPTO_PAGE_SIZE);
	ctx->secure_error = ops->smc(TETRIS_SCP_CRYPTO_DECRYPT, selector, 0);
	/* The secure side can partially write the image even on failure. */
	ops->invalidate(image, span);
	if (ctx->secure_error) {
		ret = -EIO;
	} else {
		ops->sha256(image, size, digest);
		if (!digest_equal(digest, expected))
			ret = -EBADMSG;
	}
	if (ret) {
		ctx->state = TETRIS_SCP_CRYPTO_FAILED;
		erase(image, span);
		ops->flush(image, span);
	}
	erase(page + DESCRIPTOR_OFFSET, 32);
	erase(page + MATERIAL_OFFSET, 32);
	ops->flush(page, TETRIS_SCP_CRYPTO_PAGE_SIZE);
	erase(material, sizeof(material));
	erase(expected, sizeof(expected));
	erase(digest, sizeof(digest));
	return ret;
}

#ifndef TETRIS_SCP_CRYPTO_HOST_TEST
static u64 hw_smc(u32 function, u64 arg1, u64 arg2)
{
	struct arm_smccc_res result;

	arm_smccc_smc(function, arg1, arg2, 0, 0, 0, 0, 0, &result);
	return result.a0;
}

static void hw_flush(void *buffer, size_t size)
{
	flush_dcache_range((unsigned long)buffer, (unsigned long)buffer + size);
}

static void hw_invalidate(void *buffer, size_t size)
{
	invalidate_dcache_range((unsigned long)buffer,
				(unsigned long)buffer + size);
}

static void hw_sha256(const void *buffer, u32 size, u8 digest[32])
{
	sha256_csum_wd(buffer, size, digest, CHUNKSZ_SHA256);
}

static u64 hw_physical(const void *buffer)
{
	return map_to_sysmem(buffer);
}

const struct tetris_scp_crypto_ops tetris_scp_crypto_hw_ops = {
	.smc = hw_smc,
	.flush = hw_flush,
	.invalidate = hw_invalidate,
	.sha256 = hw_sha256,
	.physical = hw_physical,
	.cache_alignment = ARCH_DMA_MINALIGN,
};
#endif
