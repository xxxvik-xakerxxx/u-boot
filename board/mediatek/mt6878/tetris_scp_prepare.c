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
#include <asm/io.h>
#include <cpu_func.h>
#include <asm/unaligned.h>
#include <linux/libfdt.h>
#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <u-boot/sha256.h>
#include "tetris_scp_crypto.h"
#include "tetris_scp_handoff.h"
#include "tetris_scp_security.h"
#include "tetris_scp_tcm.h"
#include "tetris_scp_secure.h"

#define PROFILE_CONTAINER_SIZE 0xa43070U
#define PROFILE_ATF_SIZE 900240U
#define CORE_CAPACITY 0x700000U
#define DRAM_CAPACITY 0xe00000U
#define CERT_CAPACITY 16384U

#if CONFIG_IS_ENABLED(TETRIS_SCP_TCM_DIAGNOSTIC)
static u32 tcm_read(u64 address)
{
	return readl((void *)(unsigned long)address);
}

static void tcm_write(u64 address, u32 value)
{
	writel(value, (void *)(unsigned long)address);
}

static void tcm_barrier(void)
{
	mb();
}

static int prepare_tcm(void *fdt, u8 *core, u64 base, u64 capacity,
		       u32 core_size, u32 dram_size)
{
	const struct tetris_scp_tcm_ops ops = { tcm_read, tcm_write, tcm_barrier };
	const char *control;
	const fdt32_t *sram;
	fdt_size_t bytes;
	fdt_addr_t address;
	u64 backup = CORE_CAPACITY + ALIGN((u64)dram_size, 1024);
	int node, index, length, ret;

	node = fdt_node_offset_by_compatible(fdt, -1, "mediatek,scp");
	if (node < 0 || fdt_node_offset_by_compatible(fdt, node,
					"mediatek,scp") != -FDT_ERR_NOTFOUND)
		return -EINVAL;
	index = fdt_stringlist_search(fdt, node, "reg-names", "scp_sram_base");
	if (index < 0)
		return index;
	address = fdtdec_get_addr_size_auto_noparent(fdt, node, "reg", index,
						   &bytes, true);
	sram = fdt_getprop(fdt, node, "scp-sram-size", &length);
	if (address != 0x1c400000 || bytes != 0x100000 || !sram || length != 4 ||
	    fdt32_to_cpu(*sram) != 0x100000 || core_size < 8192 ||
	    get_unaligned_le32(core + 32) != 60 || backup > capacity ||
	    dram_size > capacity - backup)
		return -ERANGE;
	/* LK treats missing scpctl as zero; reject unsupported nonzero policy. */
	control = fdt_getprop(fdt, node, "scpctl", &length);
	if (control && (length != 2 || memcmp(control, "0", 2)))
		return -EOPNOTSUPP;
	/* Incomplete secure handoff must never be consumed by a Linux probe. */
	ret = fdt_setprop_string(fdt, node, "status", "disabled");
	if (ret)
		return ret;
	memcpy(core + backup, core + CORE_CAPACITY, dram_size);
	flush_dcache_range((unsigned long)core,
		(unsigned long)core + ALIGN(backup + dram_size, ARCH_DMA_MINALIGN));
	return tetris_scp_tcm_prepare(core, core_size, base, capacity, dram_size, 0, &ops);
}
#endif

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

#if CONFIG_IS_ENABLED(TETRIS_SCP_SECURE_DIAGNOSTIC)
static u64 secure_smc(u32 function, u64 operation, u64 a, u64 b, u64 c)
{
	struct arm_smccc_res result;

	arm_smccc_smc(function, operation, a, b, c, 0, 0, 0, &result);
	return result.a0;
}

static int prepare_secure(void *fdt, u8 *core, u64 base, u64 capacity,
			  u32 core_size, u32 dram_size)
{
	const struct tetris_scp_secure_ops ops = { secure_smc, tcm_write, tcm_barrier };
	struct tetris_scp_secure_plan plan;
	const fdt32_t *data;
	u32 table[75], dumps[5];
	u64 shared, shared_size;
	int node, length, cells, i, ret;

	ret = reserved_range(fdt, "mediatek,reserve-memory-scp_share", &shared,
			     &shared_size);
	if (ret)
		return ret;
	if (overlaps(shared, shared_size, TETRIS_SCP_CRYPTO_PAGE_PA, 4096))
		return -ERANGE;
	node = fdt_node_offset_by_compatible(fdt, -1, "mediatek,scp");
	if (node < 0)
		return node;
	data = fdt_getprop(fdt, node, "scp-mem-tbl", &length);
	if (!data || length <= 0 || length > sizeof(table) || length % 12)
		return -EINVAL;
	cells = length / 4;
	for (i = 0; i < cells; i++)
		table[i] = fdt32_to_cpu(data[i]);
	data = fdt_getprop(fdt, node, "memorydump", &length);
	if (!data || length != sizeof(dumps))
		return -EINVAL;
	for (i = 0; i < 5; i++)
		dumps[i] = fdt32_to_cpu(data[i]);
	ret = tetris_scp_secure_plan(&plan, base, capacity, shared, shared_size,
				    dram_size, table, cells, dumps);
	if (ret)
		return ret;
	ret = tetris_scp_secure_begin(&plan, &ops);
	if (!ret)
		ret = prepare_tcm(fdt, core, base, capacity, core_size, dram_size);
	if (!ret)
		ret = tetris_scp_secure_finish(&plan, &ops);
	printf("Tetris: SCP secure state=%u function=%x op=%u error=%llx ret=%d\n",
	       plan.state, plan.last_function, plan.last_operation,
	       (unsigned long long)plan.secure_error, ret);
	if (!ret) {
		node = fdt_node_offset_by_compatible(fdt, -1, "mediatek,scp");
		ret = fdt_setprop_u32(fdt, node, "secure-dump-size", plan.dump_size);
		if (!ret)
			ret = fdt_setprop_string(fdt, node, "secure-dump", "enable");
		/* Publish the consumer only after every secure call succeeded. */
		if (!ret)
			ret = fdt_setprop_string(fdt, node, "status", "okay");
	}
	node = fdt_path_offset(fdt, "/chosen");
	if (node >= 0) {
		int report = fdt_setprop_u32(fdt, node, "nothing,scp-secure-state", plan.state);
		if (!report)
			report = fdt_setprop_u32(fdt, node, "nothing,scp-secure-operation", plan.last_operation);
		if (!report)
			report = fdt_setprop_u64(fdt, node, "nothing,scp-secure-error", plan.secure_error);
		if (report)
			printf("Tetris: SCP secure diagnostic publication failed: %d\n", report);
	}
	return ret;
}
#endif

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
#if CONFIG_IS_ENABLED(TETRIS_SCP_TCM_DIAGNOSTIC)
	/* Also block the consumer on a warm boot with stale, nonzero TCM. */
	{
		int node = fdt_node_offset_by_compatible(fdt, -1, "mediatek,scp");

		ret = node < 0 ? node :
			fdt_setprop_string(fdt, node, "status", "disabled");
		chosen = fdt_path_offset(fdt, "/chosen");
		if (ret)
			goto out;
	}
#endif
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
	    overlaps(firmware, capacity, map_to_sysmem(images->ft_addr), images->ft_len) ||
	    overlaps(firmware, capacity, images->os.load, images->os.end - images->os.start) ||
	    overlaps(service, service_size, images->initrd_start,
		     images->initrd_end - images->initrd_start) ||
	    overlaps(service, service_size, map_to_sysmem(images->ft_addr), images->ft_len) ||
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
	core = map_sysmem(firmware, capacity);
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
#if CONFIG_IS_ENABLED(TETRIS_SCP_TCM_DIAGNOSTIC)
	if (!ret) {
		stage = "tcm-prepare";
#if CONFIG_IS_ENABLED(TETRIS_SCP_SECURE_DIAGNOSTIC)
		stage = "secure-handoff";
		ret = prepare_secure(fdt, core, firmware, capacity,
				     container.sections[0].payload_size,
				     container.sections[3].payload_size);
#else
		ret = prepare_tcm(fdt, core, firmware, capacity,
				  container.sections[0].payload_size,
				  container.sections[3].payload_size);
#endif
		/* Property insertion can shift the chosen node's offset. */
		chosen = fdt_path_offset(fdt, "/chosen");
		if (!ret)
			stage = IS_ENABLED(CONFIG_TETRIS_SCP_SECURE_DIAGNOSTIC) ?
				"secure-handoff-prepared" : "tcm-verified-reset-held";
	}
#endif
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
	printf("Tetris: SCP prepare stage=%s error=%d (reset not released)\n", stage, ret);
	return ret;
}
