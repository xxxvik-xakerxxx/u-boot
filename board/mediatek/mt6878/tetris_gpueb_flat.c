// SPDX-License-Identifier: GPL-2.0+
#include <lmb.h>
#include <malloc.h>
#include <mapmem.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_gpueb_layout.h"
#include "tetris_gpueb_flat.h"

#define FLAT_BYTES 156064U
#define RESERVE_BYTES 0x100000U
#define RESERVE_ALIGN 0x10000U
#define RESERVE_FLAGS (LMB_NOMAP | LMB_NOOVERWRITE)
#if (FLAT_BYTES % 4) || (FLAT_BYTES > 0x3fd1cU)
#error Flat signed span must be word aligned and below the source-defined GPR window
#endif

struct tetris_gpueb_flat {
	struct tetris_gpueb_flat_info info;
	const struct tetris_scp_crypto_ops *ops;
	void *mapping;
};

/* Signed leaf post-transform hashes, not device identity or calibration.
 * Phone a and the independently authenticated cached B4.1 distribution differ.
 * These restrict this draft to reviewed inputs; unknown revisions fail closed.
 */
static const u8 profiles[][32] = {
	{ 0x96,0x28,0xa4,0x46,0xc2,0x45,0x36,0x64,
	  0xee,0xbb,0x7c,0xe0,0xf5,0xb6,0xd9,0xdb,
	  0x51,0xd6,0x77,0xd6,0xc1,0xdb,0x3c,0x4a,
	  0xc1,0x44,0x9f,0x4c,0xfa,0xad,0x86,0xd7 },
	{ 0x4c,0xd3,0xac,0x60,0x60,0x5b,0x30,0xf9,
	  0x29,0x88,0xa7,0x60,0x6e,0x95,0xb7,0xe4,
	  0xf8,0x2d,0x70,0x51,0x3b,0xbd,0x12,0x92,
	  0x3a,0x0e,0x56,0x2e,0x9f,0xc3,0x17,0x02 },
};

static void erase(void *data, size_t size)
{
	volatile u8 *p = data;

	while (size--)
		*p++ = 0;
}

static int overlaps(const void *a, size_t na, const void *b, size_t nb)
{
	unsigned long x = (unsigned long)a, y = (unsigned long)b;

	if (na > ~0UL - x || nb > ~0UL - y)
		return 1;
	return x < y + nb && y < x + na;
}

static int reviewed(const u8 digest[32])
{
	unsigned int i;

	for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++)
		if (!memcmp(digest, profiles[i], 32))
			return 1;
	return 0;
}

int tetris_gpueb_flat_retain(struct tetris_scp_crypto *crypto,
	const struct tetris_scp_security_ops *security,
	const void *container, size_t bytes, const u8 root_pin[32],
	struct tetris_gpueb_flat **result)
{
	struct tetris_scp_security_metadata metadata = { 0 };
	struct tetris_gpueb_layout layout;
	const struct tetris_scp_crypto_ops *ops;
	struct tetris_gpueb_flat *image = NULL;
	const u8 *p = container;
	phys_addr_t base = 0x80000000ULL;
	const void *protected[7];
	size_t lengths[7];
	unsigned int i;
	int ret, writable = 0;

	if (!crypto || !security || !container || !root_pin || !result ||
	    !bytes || bytes > TETRIS_GPUEB_MAX_CONTAINER ||
	    crypto->state != TETRIS_SCP_CRYPTO_READY || !crypto->page || !crypto->ops)
		return -EINVAL;
	ops = crypto->ops;
	if (!ops->smc || !ops->flush || !ops->invalidate || !ops->sha256 ||
	    !ops->physical || ops->physical(crypto->page) != TETRIS_SCP_CRYPTO_PAGE_PA)
		return -EINVAL;
	protected[0] = container; lengths[0] = bytes;
	protected[1] = crypto; lengths[1] = sizeof(*crypto);
	protected[2] = crypto->page; lengths[2] = TETRIS_SCP_CRYPTO_PAGE_SIZE;
	protected[3] = ops; lengths[3] = sizeof(*ops);
	protected[4] = security; lengths[4] = sizeof(*security);
	protected[5] = root_pin; lengths[5] = 32;
	protected[6] = result; lengths[6] = sizeof(*result);
	for (i = 0; i < 6; i++)
		if (overlaps(result, sizeof(*result), protected[i], lengths[i]))
			return -EINVAL;
	if (*result)
		return -EALREADY;
	ret = tetris_gpueb_parse_layout(container, bytes, &layout);
	if (ret)
		goto out;
	if (layout.sections[0].payload_size != FLAT_BYTES) {
		ret = -ENOEXEC;
		goto out;
	}
	ret = tetris_gpueb_authenticate(
		p + layout.sections[1].payload_offset, layout.sections[1].payload_size,
		p + layout.sections[2].payload_offset, layout.sections[2].payload_size,
		p + layout.sections[0].header_offset, 512,
		p + layout.sections[0].payload_offset, FLAT_BYTES,
		root_pin, security, &metadata);
	if (ret)
		goto out;
	if (!reviewed(metadata.plaintext)) {
		ret = -ENOEXEC;
		goto out;
	}
	image = calloc(1, sizeof(*image));
	if (!image) {
		ret = -ENOMEM;
		goto out;
	}
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, RESERVE_ALIGN, &base,
			    RESERVE_BYTES, RESERVE_FLAGS);
	if (ret)
		goto free_image;
	image->mapping = map_sysmem(base, RESERVE_BYTES);
	if (!image->mapping) {
		ret = -ENOMEM;
		goto release;
	}
	for (i = 0; i < 7; i++)
		if (overlaps(image->mapping, RESERVE_BYTES, protected[i], lengths[i])) {
			ret = -EINVAL;
			goto unmap;
		}
	if (overlaps(image->mapping, RESERVE_BYTES, image, sizeof(*image))) {
		ret = -EINVAL;
		goto unmap;
	}
	ret = tetris_scp_crypto_check_image(ops, image->mapping,
					  RESERVE_BYTES, RESERVE_BYTES);
	if (ret)
		goto unmap;
	writable = 1;
	erase(image->mapping, RESERVE_BYTES);
	memcpy(image->mapping, p + layout.sections[0].payload_offset, FLAT_BYTES);
	ret = tetris_scp_crypto_decrypt(crypto, image->mapping, FLAT_BYTES,
		RESERVE_BYTES, 1, metadata.wrapped, metadata.ciphertext, metadata.plaintext);
	if (ret)
		goto unmap;
	ops->flush(image->mapping, RESERVE_BYTES);
	image->ops = ops;
	image->info.reserved_base = base;
	image->info.reserved_bytes = RESERVE_BYTES;
	image->info.authenticated_bytes = FLAT_BYTES;
	/* LK source 0x1ed90 -> 0x1ee14: direct copy to SRAM base, no relocation.
	 * This describes only signed bytes, NOT LK's unauthenticated excess copy.
	 */
	image->info.sram_offset = 0;
	memcpy(image->info.plaintext_sha256, metadata.plaintext, 32);
	*result = image;
	erase(&metadata, sizeof(metadata));
	return 0;
unmap:
	if (writable) {
		erase(image->mapping, RESERVE_BYTES);
		ops->flush(image->mapping, RESERVE_BYTES);
	}
	unmap_sysmem(image->mapping);
release:
	/* No remote processor has seen this address; preserve original failure. */
	lmb_free(base, RESERVE_BYTES, RESERVE_FLAGS | LMB_NONOTIFY);
free_image:
	erase(image, sizeof(*image));
	free(image);
out:
	erase(&metadata, sizeof(metadata));
	return ret;
}

int tetris_gpueb_flat_describe(const struct tetris_gpueb_flat *image,
	struct tetris_gpueb_flat_info *info)
{
	if (!image || !info || overlaps(image, sizeof(*image), info, sizeof(*info)) ||
	    overlaps(image->mapping, RESERVE_BYTES, info, sizeof(*info)))
		return -EINVAL;
	*info = image->info;
	return 0;
}

int tetris_gpueb_flat_discard(struct tetris_gpueb_flat *image)
{
	int ret;

	if (!image)
		return -EINVAL;
	erase(image->mapping, RESERVE_BYTES);
	image->ops->flush(image->mapping, RESERVE_BYTES);
	/* Avoid a notification failure after _lmb_free already removed the range:
	 * otherwise a retained handle could refer to memory no longer reserved.
	 */
	ret = lmb_free(image->info.reserved_base, RESERVE_BYTES,
		       RESERVE_FLAGS | LMB_NONOTIFY);
	if (ret)
		return ret; /* Reservation and handle remain owned; no silent release. */
	unmap_sysmem(image->mapping);
	erase(image, sizeof(*image));
	free(image);
	return 0;
}
