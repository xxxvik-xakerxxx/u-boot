// SPDX-License-Identifier: GPL-2.0+
#define TETRIS_SCP_CRYPTO_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_scp_crypto.c"
#include <assert.h>
#include <stdio.h>

static _Alignas(4096) u8 page[4096];
static _Alignas(64) u8 image[128];
static u8 cipher_digest[32], plain_digest[32], wrapped[32];
static u64 image_pa, failure;
static unsigned int calls, flushes, invalidates;
static bool wrong_plaintext;

static u64 read_le64(const u8 *p)
{
	u64 value = 0;
	unsigned int i;

	for (i = 0; i < 8; i++)
		value |= (u64)p[i] << (8 * i);
	return value;
}

static void mock_flush(void *buffer, size_t size)
{
	assert(buffer == page || buffer == image);
	assert(size == (buffer == page ? sizeof(page) : sizeof(image)));
	flushes++;
}

static void mock_invalidate(void *buffer, size_t size)
{
	assert(buffer == image && size == sizeof(image));
	assert(calls >= 2);
	invalidates++;
}

/* Test sentinel, not a cryptographic implementation or a firmware fixture. */
static void mock_hash(const void *buffer, u32 size, u8 digest[32])
{
	assert(buffer == image && size == 80);
	memset(digest, *(const u8 *)buffer, 32);
}

static u64 mock_physical(const void *buffer)
{
	return buffer == page ? TETRIS_SCP_CRYPTO_PAGE_PA : image_pa;
}

static u64 mock_smc(u32 function, u64 a1, u64 a2)
{
	calls++;
	if (function == TETRIS_SCP_CRYPTO_INIT) {
		assert(a1 == TETRIS_SCP_CRYPTO_PAGE_PA && a2 == sizeof(page));
		return failure;
	}
	assert(function == TETRIS_SCP_CRYPTO_DECRYPT && a1 == 1 && a2 == 0);
	assert(flushes >= 2);
	assert(read_le64(page + 0x40) == image_pa);
	assert(read_le64(page + 0x48) == 80);
	assert(read_le64(page + 0x50) == TETRIS_SCP_CRYPTO_PAGE_PA + 0x100);
	assert(read_le64(page + 0x58) == 32);
	assert(!memcmp(page + 0x100, wrapped, 32));
	memset(image, wrong_plaintext ? 0x44 : 0x22, 80);
	return failure;
}

static const struct tetris_scp_crypto_ops ops = {
	.smc = mock_smc, .flush = mock_flush, .invalidate = mock_invalidate,
	.sha256 = mock_hash, .physical = mock_physical, .cache_alignment = 64,
};

static void setup(struct tetris_scp_crypto *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	memset(page, 0xa5, sizeof(page));
	memset(image, 0x11, sizeof(image));
	memset(cipher_digest, 0x11, 32);
	memset(plain_digest, 0x22, 32);
	memset(wrapped, 0x77, 32);
	image_pa = 0x90000000;
	failure = calls = flushes = invalidates = 0;
	wrong_plaintext = false;
}

static int decrypt(struct tetris_scp_crypto *ctx)
{
	return tetris_scp_crypto_decrypt(ctx, image, 80, sizeof(image), 1,
					 wrapped, cipher_digest, plain_digest);
}

static void check_erased(const void *buffer, size_t size)
{
	const u8 *p = buffer;

	while (size--)
		assert(*p++ == 0);
}

int main(void)
{
	struct tetris_scp_crypto ctx;
	struct tetris_scp_crypto_ops bad_ops = ops;
	unsigned int i;

	setup(&ctx);
	assert(decrypt(&ctx) == -EINVAL && calls == 0);
	assert(tetris_scp_crypto_init(&ctx, page, 4095, &ops) == -EINVAL);
	bad_ops.cache_alignment = 3;
	assert(tetris_scp_crypto_init(&ctx, page, 4096, &bad_ops) == -EINVAL);
	assert(calls == 0);
	assert(!tetris_scp_crypto_init(&ctx, page, 4096, &ops));
	assert(!decrypt(&ctx));
	assert(ctx.state == TETRIS_SCP_CRYPTO_READY);
	assert(calls == 2 && invalidates == 1);
	assert(image[0] == 0x22 && image[80] == 0x11);
	check_erased(page + 0x40, 32);
	check_erased(page + 0x100, 32);
	assert(page[0] == 0xa5 && page[0x120] == 0xa5);
	assert(tetris_scp_crypto_init(&ctx, page, 4096, &ops) == -EINVAL);
	/* Same registered page supports the second component without re-init. */
	memset(image, 0x11, sizeof(image));
	assert(!decrypt(&ctx) && calls == 3);

	setup(&ctx);
	failure = (u64)-4;
	assert(tetris_scp_crypto_init(&ctx, page, 4096, &ops) == -EIO);
	assert(ctx.secure_error == failure && ctx.state == TETRIS_SCP_CRYPTO_FAILED);
	assert(decrypt(&ctx) == -EINVAL && calls == 1);

	for (i = 0; i < 2; i++) {
		setup(&ctx);
		assert(!tetris_scp_crypto_init(&ctx, page, 4096, &ops));
		wrong_plaintext = i == 0;
		failure = i ? (u64)-4 : 0;
		assert(decrypt(&ctx) == (i ? -EIO : -EBADMSG));
		assert(ctx.state == TETRIS_SCP_CRYPTO_FAILED);
		assert(invalidates == 1);
		check_erased(image, sizeof(image));
		check_erased(page + 0x40, 32);
		check_erased(page + 0x100, 32);
		assert(decrypt(&ctx) == -EINVAL && calls == 2);
	}

	setup(&ctx);
	assert(!tetris_scp_crypto_init(&ctx, page, 4096, &ops));
	image[0] ^= 1;
	assert(decrypt(&ctx) == -EBADMSG && calls == 1 && flushes == 0);
	image[0] ^= 1;
	image_pa = TETRIS_SCP_CRYPTO_PAGE_PA;
	assert(decrypt(&ctx) == -EINVAL && calls == 1);
	image_pa = 0xffffffc0ULL;
	assert(decrypt(&ctx) == -ERANGE && calls == 1);
	image_pa = 0x90000000;
	assert(tetris_scp_crypto_decrypt(&ctx, image, 80, 80, 1,
		wrapped, cipher_digest, plain_digest) == -EINVAL);
	assert(tetris_scp_crypto_decrypt(&ctx, image, 80, 128, 0,
		wrapped, cipher_digest, plain_digest) == -EOPNOTSUPP);
	assert(tetris_scp_crypto_decrypt(&ctx, image, 79, 128, 1,
		wrapped, cipher_digest, plain_digest) == -EINVAL);
	assert(calls == 1);
	puts("SCP crypto transport: PASS (mock secure monitor, no hardware)");
	return 0;
}
