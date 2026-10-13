// SPDX-License-Identifier: GPL-2.0+
/* Actual producer/publisher with simulated auth/transform and allocation.
 * Synthetic bytes exercise ownership and metadata, never prove authentication.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>
#include <openssl/sha.h>

#define TETRIS_SCP_CRYPTO_HOST_TEST
#define TETRIS_GPUEB_FLAT_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_gpueb_flat.h"
#include "../../board/mediatek/mt6878/tetris_gpueb_layout.h"

typedef uint64_t phys_addr_t;
#define LMB_MEM_ALLOC_MAX 3
#define LMB_NOMAP 2
#define LMB_NOOVERWRITE 4
#define LMB_NONOTIFY 8
#define ARENA_BASE 0x70000000ULL
static u8 *arena;
static int auth_error, decrypt_error, release_error, allocations, releases;
static int profile_index, transformed, wrong_mapping, flushes;
static const char *property_fault;
static int lmb_alloc_mem(int mode, u64 align, phys_addr_t *base, size_t size, u32 flags);
static void *map_sysmem(phys_addr_t base, size_t size);
static void unmap_sysmem(void *mapping);
static u64 map_to_sysmem(const void *mapping);
static int lmb_free(phys_addr_t base, size_t size, u32 flags);

static int mock_set(void *fdt, int node, const char *name, const void *data, int size)
{
	if (property_fault && !strcmp(property_fault, name))
		return -FDT_ERR_NOSPACE;
	return fdt_setprop(fdt, node, name, data, size);
}

static int mock_set_u32(void *fdt, int node, const char *name, u32 value)
{
	fdt32_t cell = cpu_to_fdt32(value);

	return mock_set(fdt, node, name, &cell, sizeof(cell));
}

#define fdt_setprop mock_set
#define fdt_setprop_u32 mock_set_u32
#include "../../board/mediatek/mt6878/tetris_gpueb_flat.c"
#include "../../board/mediatek/mt6878/tetris_gpueb_flat_publish.c"
#undef fdt_setprop
#undef fdt_setprop_u32

static int lmb_alloc_mem(int mode, u64 align, phys_addr_t *base, size_t size, u32 flags)
{
	assert(mode == LMB_MEM_ALLOC_MAX && align == RESERVE_ALIGN);
	assert(*base == 0x80000000ULL && size == RESERVE_BYTES && flags == RESERVE_FLAGS);
	assert(!arena);
	arena = aligned_alloc(RESERVE_ALIGN, size);
	assert(arena);
	memset(arena, 0xab, size);
	*base = ARENA_BASE;
	allocations++;
	return 0;
}

static void *map_sysmem(phys_addr_t base, size_t size)
{
	assert(base == ARENA_BASE && size == RESERVE_BYTES);
	return arena;
}

static void unmap_sysmem(void *mapping)
{
	assert(mapping == arena);
}

static u64 map_to_sysmem(const void *mapping)
{
	return mapping == arena ? ARENA_BASE : 0x60000000ULL;
}

static int lmb_free(phys_addr_t base, size_t size, u32 flags)
{
	size_t i;

	assert(base == ARENA_BASE && size == RESERVE_BYTES);
	assert(flags == (RESERVE_FLAGS | LMB_NONOTIFY));
	if (!wrong_mapping)
		for (i = 0; i < size; i++)
			assert(!arena[i]);
	if (release_error)
		return release_error;
	/* Keep backing memory until unmap; model reservation release separately. */
	releases++;
	return 0;
}

static void flush(void *buffer, size_t size)
{
	assert(buffer == arena && size == RESERVE_BYTES);
	flushes++;
}

static void invalidate(void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
	assert(0); /* This fixture replaces decrypt, never invokes secure transport. */
}

static u64 physical(const void *buffer)
{
	if (buffer == arena)
		return ARENA_BASE + (wrong_mapping ? RESERVE_ALIGN : 0);
	return TETRIS_SCP_CRYPTO_PAGE_PA;
}

static u64 smc(u32 function, u64 a, u64 b)
{
	(void)function;
	(void)a;
	(void)b;
	assert(0);
	return 0;
}

static void hash(const void *buffer, u32 size, u8 digest[32])
{
	size_t i;

	SHA256(buffer, size, digest);
	if (!transformed || buffer != arena || size != FLAT_BYTES)
		return;
	for (i = 0; i < size; i++)
		if (arena[i] != (u8)(0xa5 + profile_index))
			return;
	/* Substitute the reviewed digest only for an intact simulated transform. */
	memcpy(digest, profiles[profile_index], 32);
}

static void security_hash(const void *buffer, size_t size, u8 digest[32])
{
	SHA256(buffer, size, digest);
}

int tetris_gpueb_authenticate(const void *cert1, size_t size1,
	const void *cert2, size_t size2, const void *header, size_t header_size,
	const void *image, size_t image_size, const u8 root_pin[32],
	const struct tetris_scp_security_ops *security,
	struct tetris_scp_security_metadata *metadata)
{
	assert(cert1 && size1 == 16 && cert2 && size2 == 16);
	assert(header && header_size == 512 && image && image_size == FLAT_BYTES);
	assert(root_pin && security->sha256 == security_hash);
	if (auth_error)
		return auth_error;
	security_hash(image, image_size, metadata->ciphertext);
	memcpy(metadata->plaintext, profiles[profile_index], 32);
	memset(metadata->wrapped, 0x91, 32);
	return 0;
}

int tetris_scp_crypto_check_image(const struct tetris_scp_crypto_ops *ops,
	void *image, size_t size, size_t capacity)
{
	assert(ops && image == arena && size == RESERVE_BYTES && capacity == RESERVE_BYTES);
	return 0;
}

int tetris_scp_crypto_decrypt(struct tetris_scp_crypto *crypto, void *image,
	u32 size, size_t capacity, u32 selector, const u8 wrapped[32],
	const u8 ciphertext[32], const u8 plaintext[32])
{
	u8 digest[32];

	assert(crypto->state == TETRIS_SCP_CRYPTO_READY && image == arena);
	assert(size == FLAT_BYTES && capacity == RESERVE_BYTES && selector == 1);
	assert(wrapped[0] == 0x91 && !memcmp(plaintext, profiles[profile_index], 32));
	security_hash(image, size, digest);
	assert(!memcmp(digest, ciphertext, 32));
	memset(image, 0xa5 + profile_index, size);
	transformed = 1;
	return decrypt_error;
}

static void put32(u8 *p, u32 value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		p[i] = value >> (i * 8);
}

static u8 container[FLAT_BYTES + 6 * 512 + 5 * 16];
static const struct tetris_scp_crypto_ops ops = {
	.smc = smc, .flush = flush, .invalidate = invalidate,
	.sha256 = hash, .physical = physical, .cache_alignment = 64,
};
static const struct tetris_scp_security_ops security = { .sha256 = security_hash };

static int capture(struct tetris_gpueb_flat **image)
{
	u8 page[TETRIS_SCP_CRYPTO_PAGE_SIZE], root_pin[32] = { 0 };
	struct tetris_scp_crypto crypto = { .ops = &ops, .page = page,
		.state = TETRIS_SCP_CRYPTO_READY };

	return tetris_gpueb_flat_retain(&crypto, &security, container,
					 sizeof(container), root_pin, image);
}

static void reset(void)
{
	static const char *const names[] = {
		"tinysys-gpueb-RV33_A", "cert1", "cert2",
		"tinysys-gpueb-RV33_A_xfile", "cert1", "cert2",
	};
	static const u32 roles[] = { 0, 0x02000000, 0x02000002 };
	size_t offset = 0, length;
	unsigned int i;
	u8 *h;

	free(arena);
	arena = NULL;
	assert(!published_owner);
	auth_error = decrypt_error = release_error = allocations = releases = 0;
	profile_index = transformed = wrong_mapping = flushes = 0;
	property_fault = NULL;
	memset(container, 0, sizeof(container));
	for (i = 0; i < 6; i++) {
		h = container + offset;
		length = i ? 16 : FLAT_BYTES;
		put32(h, 0x58881688);
		put32(h + 4, length);
		memcpy(h + 8, names[i], strlen(names[i]));
		put32(h + 48, 0x58891689);
		put32(h + 52, 512);
		put32(h + 56, 1);
		put32(h + 60, roles[i % 3]);
		put32(h + 68, 16);
		put32(h + 76, i % 3 ? 0x22345678 : 0);
		memset(h + 512, 0x31 + i, length);
		offset += 512 + length;
	}
	assert(offset == sizeof(container));
}

static void tree(void *fdt, size_t capacity)
{
	assert(!fdt_create_empty_tree(fdt, capacity));
	assert(!fdt_setprop_u32(fdt, 0, "#address-cells", 2));
	assert(!fdt_setprop_u32(fdt, 0, "#size-cells", 2));
}

static u32 get32(const void *fdt, int node, const char *name)
{
	int length;
	const fdt32_t *value = fdt_getprop(fdt, node, name, &length);

	assert(value && length == 4);
	return fdt32_to_cpu(*value);
}

static void check_digest(const void *fdt, int node, const char *name, const u8 *expected)
{
	int length;
	const void *value = fdt_getprop(fdt, node, name, &length);

	assert(value && length == 32 && !memcmp(value, expected, 32));
}

static void rejected_publication(int expected)
{
	u64 fdt[2048], before[2048];
	struct tetris_gpueb_flat *image = NULL;

	assert(!capture(&image));
	tree(fdt, sizeof(fdt));
	memcpy(before, fdt, sizeof(fdt));
	assert(tetris_gpueb_flat_publish(&image, fdt, sizeof(fdt)) == expected);
	assert(!image && !published_owner && releases == 1);
	assert(!memcmp(before, fdt, sizeof(fdt)));
}

int main(void)
{
	static const char *const properties[] = {
		"reg", "no-map", "phandle", "memory-region",
		"nothing,handoff-version", "nothing,payload-offset",
		"nothing,authenticated-bytes", "nothing,sram-offset", "nothing,profile-id",
		"nothing,plaintext-sha256", "nothing,primary-header-sha256",
		"nothing,ciphertext-sha256", "nothing,leaf-certificate-sha256",
		"nothing,container-sha256",
	};
	struct tetris_gpueb_flat *image = NULL;
	struct tetris_gpueb_flat_info info, before;
	u64 fdt[2048], original[2048];
	u8 digest[32];
	unsigned int i;
	int node, reserved, length;

	reset();
	auth_error = -EACCES;
	assert(capture(&image) == -EACCES && !image && !allocations);
	reset();
	decrypt_error = -EIO;
	assert(capture(&image) == -EIO && !image && releases == 1 && flushes == 1);
	reset();
	wrong_mapping = 1;
	assert(capture(&image) == -ERANGE && !image && releases == 1 && !flushes);

	/* Rejection between capture and publication erases RAM and leaves DT intact. */
	reset();
	assert(!capture(&image));
	memset(&info, 0xcc, sizeof(info));
	before = info;
	arena[FLAT_BYTES - 1] ^= 1;
	assert(tetris_gpueb_flat_describe(image, &info) == -EBADMSG);
	assert(!memcmp(&info, &before, sizeof(info)));
	tree(fdt, sizeof(fdt));
	memcpy(original, fdt, sizeof(fdt));
	release_error = -EBUSY;
	assert(tetris_gpueb_flat_publish(&image, fdt, sizeof(fdt)) == -EBADMSG);
	assert(image && !published_owner && !memcmp(original, fdt, sizeof(fdt)));
	release_error = 0;
	assert(!tetris_gpueb_flat_discard(image));
	image = NULL;

	for (i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
		reset();
		property_fault = properties[i];
		rejected_publication(-FDT_ERR_NOSPACE);
	}
	reset();
	assert(!capture(&image));
	image->info.payload_offset = 24;
	assert(tetris_gpueb_flat_describe(image, &info) == -EINVAL);
	image->info.payload_offset = 0;
	image->info.authenticated_bytes = TETRIS_GPUEB_LK_COPY_BYTES;
	assert(tetris_gpueb_flat_describe(image, &info) == -EINVAL);
	image->info.authenticated_bytes = FLAT_BYTES;
	image->info.profile_id = TETRIS_GPUEB_FLAT_PROFILE_B41;
	assert(tetris_gpueb_flat_describe(image, &info) == -EINVAL);
	image->info.profile_id = TETRIS_GPUEB_FLAT_PROFILE_PHONE_A;
	image->info.handoff_version++;
	assert(tetris_gpueb_flat_describe(image, &info) == -EINVAL);
	assert(!tetris_gpueb_flat_discard(image));
	image = NULL;

	/* Both reviewed identities publish the same bounded offset-zero layout. */
	for (i = 0; i < 2; i++) {
		reset();
		profile_index = i;
		assert(!capture(&image));
		assert(!tetris_gpueb_flat_describe(image, &info));
		tree(fdt, sizeof(fdt));
		assert(!tetris_gpueb_flat_publish(&image, fdt, sizeof(fdt)));
		assert(!image && published_owner && !releases);
		assert(!fdt_check_full(fdt, sizeof(fdt)));
		node = fdt_path_offset(fdt, "/gpueb-flat-analysis");
		assert(node >= 0);
		assert(get32(fdt, node, "nothing,handoff-version") == 1);
		assert(get32(fdt, node, "nothing,payload-offset") == 0);
		assert(get32(fdt, node, "nothing,sram-offset") == 0);
		assert(get32(fdt, node, "nothing,authenticated-bytes") == FLAT_BYTES);
		assert(get32(fdt, node, "nothing,profile-id") == i + 1);
		check_digest(fdt, node, "nothing,plaintext-sha256", profiles[i]);
		SHA256(container, sizeof(container), digest);
		check_digest(fdt, node, "nothing,container-sha256", digest);
		SHA256(container, 512, digest);
		check_digest(fdt, node, "nothing,primary-header-sha256", digest);
		SHA256(container + 512, FLAT_BYTES, digest);
		check_digest(fdt, node, "nothing,ciphertext-sha256", digest);
		SHA256(container + 3 * 512 + FLAT_BYTES + 16, 16, digest);
		check_digest(fdt, node, "nothing,leaf-certificate-sha256", digest);
		reserved = fdt_node_offset_by_phandle(fdt, get32(fdt, node, "memory-region"));
		assert(reserved >= 0);
		assert(fdt_getprop(fdt, reserved, "no-map", &length) && !length);
		assert(!memcmp(arena + FLAT_BYTES, (u8[32]){0}, 32));
		/* No public post-transfer discard: reset test-only process state. */
		assert(!tetris_gpueb_flat_discard(published_owner));
		published_owner = NULL;
	}
	free(arena);
	puts("GPUEB retained metadata: producer/publisher failure-atomic fixture passed");
	return 0;
}
