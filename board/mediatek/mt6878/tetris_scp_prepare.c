// SPDX-License-Identifier: GPL-2.0+
/* Explicit slot-A diagnostic profile. This does not release SCP reset. */
#include <blk.h>
#include <bootm.h>
#include <fdtdec.h>
#include <lmb.h>
#include <malloc.h>
#include <mapmem.h>
#include <part.h>
#include <stdio.h>
#include <asm/cache.h>
#include <asm/unaligned.h>
#include <linux/libfdt.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <u-boot/sha256.h>
#include "tetris_scp_crypto.h"
#include "tetris_scp_handoff.h"
#include "tetris_scp_security.h"

#define PROFILE_CONTAINER_SIZE 0xa43070U
#define PROFILE_ATF_SIZE 900240U
#define CORE_CAPACITY 0x700000U
#define DRAM_CAPACITY 0xe00000U
#define CERT_CAPACITY 16384U

/* Firmware-version identities, not per-device calibration or secrets. */
static const u8 atf_hash[32] = {
	0x05,0xa2,0x47,0xcb,0x02,0x69,0x6c,0xe4,0xfe,0x19,0x82,0xea,0x00,0xbb,0xa8,0x12,
	0x36,0xc3,0x52,0x14,0x6c,0x30,0x71,0x59,0xd3,0xf9,0xe3,0x80,0x63,0x5e,0xa3,0x2e,
};
static const u8 container_hash[32] = {
	0x11,0xc5,0x08,0x71,0xfa,0x38,0x53,0x82,0xf3,0x65,0xaf,0xea,0x25,0x75,0xb8,0xc1,
	0x4a,0x94,0x8c,0x9b,0xbf,0x1e,0x22,0xf4,0xe6,0xde,0xb8,0x76,0xa0,0xef,0xd0,0xcb,
};
/* SPKI fingerprint independently matched to the pinned stock LK public key. */
static const u8 root_pin[32] = {
	0xe1,0xb5,0x23,0x5d,0x94,0x11,0x47,0x3a,0x35,0x8c,0x75,0x4f,0x84,0x84,0x38,0x01,
	0xb9,0x1f,0x05,0xb8,0xfb,0x9d,0xc4,0x86,0x33,0x93,0xe3,0x78,0xe4,0x1a,0x11,0x5e,
};

static int read_bytes(struct blk_desc *dev, const struct disk_partition *part,
		      u64 offset, void *output, size_t size)
{
	u64 total = (u64)part->size * part->blksz;
	u8 *block;
	size_t take, within;
	lbaint_t sector;
	int ret = 0;

	if (!part->blksz || part->blksz != dev->blksz ||
	    part->size > (~(u64)0 / part->blksz) ||
	    part->start > dev->lba || part->size > dev->lba - part->start ||
	    !size || offset > total || size > total - offset)
		return -EINVAL;
	block = memalign(ARCH_DMA_MINALIGN, part->blksz);
	if (!block)
		return -ENOMEM;
	while (size) {
		within = offset % part->blksz;
		sector = part->start + offset / part->blksz;
		take = min(size, (size_t)part->blksz - within);
		if (blk_dread(dev, sector, 1, block) != 1) {
			ret = -EIO;
			break;
		}
		memcpy(output, block + within, take);
		output = (u8 *)output + take;
		offset += take;
		size -= take;
	}
	free(block);
	return ret;
}

static bool overlaps(u64 a, u64 length, u64 b, u64 size)
{
	if (length > ~(u64)0 - a || size > ~(u64)0 - b)
		return true;
	return length && size && a < b + size && b < a + length;
}

static int reserved_range(const void *fdt, const char *compatible,
			  u64 *base, u64 *size)
{
	fdt_size_t bytes;
	fdt_addr_t address;
	const struct lmb_region *region;
	struct lmb *lmb = lmb_get();
	int node, parent;
	bool covered = false;

	parent = fdt_path_offset(fdt, "/reserved-memory");
	node = fdt_node_offset_by_compatible(fdt, -1, compatible);
	if (parent < 0 || node < 0 || fdt_parent_offset(fdt, node) != parent ||
	    fdt_node_offset_by_compatible(fdt, node, compatible) != -FDT_ERR_NOTFOUND ||
	    !fdt_getprop(fdt, node, "no-map", NULL))
		return -EINVAL;
	address = fdtdec_get_addr_size_auto_noparent(fdt, node, "reg", 0, &bytes, true);
	if (address == FDT_ADDR_T_NONE || address < 0x40000000ULL ||
	    address >= (1ULL << 32) || !bytes || bytes > (1ULL << 32) - address ||
	    (address & (ARCH_DMA_MINALIGN - 1)))
		return -ERANGE;
	alist_for_each(region, &lmb->used_mem) {
		if (!overlaps(address, bytes, region->base, region->size))
			continue;
		if (!(region->flags & LMB_NOMAP))
			return -EBUSY;
		if (region->base <= address && bytes <= region->size &&
		    address - region->base <= region->size - bytes)
			covered = true;
	}
	if (!covered)
		return -EBUSY;
	*base = address;
	*size = bytes;
	return 0;
}

static int hash_partition(struct blk_desc *dev, const char *name, u64 offset,
			  size_t size, const u8 expected[32])
{
	struct disk_partition part;
	sha256_context sha;
	u8 hash[32], *buffer;
	size_t take;
	int ret;

	ret = part_get_info_by_name(dev, name, &part);
	if (ret < 0)
		return ret;
	buffer = malloc(CHUNKSZ_SHA256);
	if (!buffer)
		return -ENOMEM;
	sha256_starts(&sha);
	ret = 0;
	while (size) {
		take = min(size, (size_t)CHUNKSZ_SHA256);
		ret = read_bytes(dev, &part, offset, buffer, take);
		if (ret)
			break;
		sha256_update(&sha, buffer, take);
		offset += take;
		size -= take;
	}
	if (!ret) {
		sha256_finish(&sha, hash);
		if (memcmp(hash, expected, sizeof(hash)))
			ret = -EKEYREJECTED;
	}
	free(buffer);
	return ret;
}

int tetris_scp_prepare_diagnostic(struct bootm_headers *images, void *fdt)
{
	static bool attempted;
	struct blk_desc *dev;
	struct disk_partition part;
	struct tetris_scp_container container;
	struct tetris_scp_crypto crypto = { 0 };
	struct tetris_scp_component_input input = { 0 };
	u8 headers[6][512], record[32];
	u64 firmware, capacity, service, service_size, offset = 0;
	u8 *cert1 = NULL, *cert2 = NULL, *core = NULL, *page = NULL;
	enum tetris_scp_slot slot;
	const char *stage = "preflight";
	int ret, chosen = fdt_path_offset(fdt, "/chosen");
	int len;
	const char *status;
	size_t i;

	if (attempted)
		return -EALREADY;
	attempted = true;
	status = chosen < 0 ? NULL : fdt_getprop(fdt, chosen,
					"nothing,scp-region-info-status", &len);
	if (!status || len != 5 || memcmp(status, "zero", 5)) {
		ret = -EBUSY;
		goto out;
	}
	ret = blk_get_desc(UCLASS_SCSI, 2, &dev);
	if (ret)
		goto out;
	ret = part_get_info_by_name(dev, "misc", &part);
	if (ret < 0)
		goto out;
	ret = read_bytes(dev, &part, 2048, record, sizeof(record));
	if (ret)
		goto out;
	ret = tetris_scp_decode_boot_control(record, sizeof(record), &slot);
	if (ret || slot != TETRIS_SCP_SLOT_A) {
		ret = -EPROTO;
		goto out;
	}
	/* This diagnostic's slot provenance is established by the flash procedure. */
	stage = "atf-profile";
	ret = hash_partition(dev, "tee_a", 512, PROFILE_ATF_SIZE, atf_hash);
	if (ret)
		goto out;
	stage = "scp-profile";
	ret = hash_partition(dev, "scp_a", 0, PROFILE_CONTAINER_SIZE, container_hash);
	if (ret)
		goto out;
	stage = "memory-ownership";
	ret = reserved_range(fdt, "mediatek,SCP-reserved", &firmware, &capacity);
	if (ret)
		goto out;
	ret = reserved_range(fdt, "mediatek,crypto_hw", &service, &service_size);
	if (ret)
		goto out;
	if (capacity < CORE_CAPACITY + 2 * DRAM_CAPACITY ||
	    service != TETRIS_SCP_CRYPTO_PAGE_PA || service_size != 4096 ||
	    overlaps(firmware, capacity, service, service_size) ||
	    overlaps(firmware, capacity, images->initrd_start,
		     images->initrd_end - images->initrd_start) ||
	    overlaps(firmware, capacity, images->ft_addr, images->ft_len) ||
	    overlaps(firmware, capacity, images->os.load, images->os.end - images->os.start) ||
	    overlaps(service, service_size, images->initrd_start,
		     images->initrd_end - images->initrd_start) ||
	    overlaps(service, service_size, images->ft_addr, images->ft_len) ||
	    overlaps(service, service_size, images->os.load, images->os.end - images->os.start)) {
		ret = -ERANGE;
		goto out;
	}
	stage = "container";
	ret = part_get_info_by_name(dev, "scp_a", &part);
	if (ret < 0)
		goto out;
	for (i = 0; i < 6; i++) {
		ret = read_bytes(dev, &part, offset, headers[i], 512);
		if (ret)
			goto out;
		offset = ALIGN(offset + 512 + get_unaligned_le32(headers[i] + 4), 16);
		if (offset > PROFILE_CONTAINER_SIZE) {
			ret = -EOVERFLOW;
			goto out;
		}
	}
	ret = tetris_scp_parse_container_headers(headers, (u64)part.size * part.blksz,
					       &container);
	if (ret)
		goto out;
	if (container.image_size != PROFILE_CONTAINER_SIZE) {
		ret = -EPROTO;
		goto out;
	}
	cert1 = malloc(CERT_CAPACITY);
	cert2 = malloc(CERT_CAPACITY);
	core = map_sysmem(firmware, CORE_CAPACITY + DRAM_CAPACITY);
	page = map_sysmem(service, service_size);
	if (!cert1 || !cert2 || !core || !page) {
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < 6; i += 3) {
		stage = i ? "dram-decrypt" : "core-decrypt";
		input.image = core + (i ? CORE_CAPACITY : 0);
		input.capacity = i ? DRAM_CAPACITY : CORE_CAPACITY;
		input.size = container.sections[i].payload_size;
		input.cert1 = cert1;
		input.cert1_size = container.sections[i + 1].payload_size;
		input.cert2 = cert2;
		input.cert2_size = container.sections[i + 2].payload_size;
		if (input.size > input.capacity || input.cert1_size > CERT_CAPACITY ||
		    input.cert2_size > CERT_CAPACITY) {
			ret = -E2BIG;
			goto out;
		}
		ret = read_bytes(dev, &part, container.sections[i].offset + 512,
				 input.image, input.size);
		if (!ret)
			ret = read_bytes(dev, &part, container.sections[i + 1].offset + 512,
					 cert1, input.cert1_size);
		if (!ret)
			ret = read_bytes(dev, &part, container.sections[i + 2].offset + 512,
					 cert2, input.cert2_size);
		if (!ret)
			ret = tetris_scp_prepare_component(&crypto, page,
				&tetris_scp_crypto_hw_ops, &tetris_scp_security_hw_ops,
				&input, root_pin);
		if (ret)
			goto out;
	}
	stage = "plaintext-verified";
	if (chosen >= 0)
		ret = fdt_setprop_u32(fdt, chosen, "nothing,scp-plaintext-region-size",
				get_unaligned_le32(core + 4 + 28));
out:
	free(cert1);
	free(cert2);
	if (core)
		unmap_sysmem(core);
	if (page)
		unmap_sysmem(page);
	if (chosen >= 0) {
		int report = fdt_setprop_string(fdt, chosen,
					      "nothing,scp-prepare-stage", stage);
		if (!report)
			report = fdt_setprop_u32(fdt, chosen,
						 "nothing,scp-prepare-error", ret);
		if (report)
			printf("Tetris: SCP diagnostic FDT report error=%d\n", report);
	}
	printf("Tetris: SCP prepare stage=%s error=%d (no TCM/reset)\n", stage, ret);
	return ret;
}
