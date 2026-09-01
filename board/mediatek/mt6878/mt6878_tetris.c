// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#ifdef TETRIS_CCCI_HANDOFF_HOST_TEST
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef __UINT64_TYPE__ u64;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BIT(nr) (1UL << (nr))
#define get_unaligned_le32(p) tetris_test_get_le32(p)
#define get_unaligned_le64(p) tetris_test_get_le64(p)

static u32 tetris_test_get_le32(const void *ptr)
{
	const u8 *p = ptr;

	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 |
	       (u32)p[3] << 24;
}

static u64 tetris_test_get_le64(const void *ptr)
{
	return get_unaligned_le32(ptr) |
	       (u64)get_unaligned_le32((const u8 *)ptr + 4) << 32;
}
#else
#include <config.h>
#include <bootm.h>
#include <command.h>
#include <env.h>
#include <fastboot.h>
#include <fdtdec.h>
#include <init.h>
#include <mapmem.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <linux/arm-smccc.h>
#include <linux/libfdt.h>
#include <string.h>
#endif

#define TETRIS_CCCI_MAX_INFO_SIZE	0x10000U
#define TETRIS_CCCI_V1_DESC_SIZE	16U
#define TETRIS_CCCI_V2_DESC_SIZE	32U
#define TETRIS_CCCI_V1_NAME_SIZE	16U
#define TETRIS_CCCI_V2_NAME_SIZE	64U
#define TETRIS_CCCI_V1_TAG_SIZE		28U
#define TETRIS_CCCI_V2_TAG_SIZE		76U
/*
 * LK payload ABI from Nothing OS 4.1 Tetris B4.1 device modules commit
 * ee2be53cb75670b548948636a0db1d1ff112bf12. Keep parsing byte-oriented.
 */
#define TETRIS_CCCI_MODEM_INFO_SIZE	24U
#define TETRIS_CCCI_MD_MEM_BLOCK_SIZE	24U
#define TETRIS_CCCI_SMEM_LAYOUT_SIZE	40U
#define TETRIS_CCCI_SMEM_REGION_SIZE	40U
#define TETRIS_CCCI_MD_CHECK_V2_SIZE	188U
#define TETRIS_CCCI_MD_CHECK_V5_SIZE	344U
#define TETRIS_CCCI_MD_CHECK_V6_SIZE	512U
#define TETRIS_CCCI_MD_CHECK_MAGIC	"CHECK_HEADER"
#define TETRIS_CCCI_MD_CHECK_MAGIC_SIZE	12U
#define TETRIS_CCCI_MD_TYPE_MAX		14U
#define TETRIS_CCCI_MAX_TAGS \
	(TETRIS_CCCI_MAX_INFO_SIZE / TETRIS_CCCI_V1_TAG_SIZE)

#define TETRIS_CCCI_CORE_HDR_COUNT	BIT(0)
#define TETRIS_CCCI_CORE_HDR_TABLE	BIT(1)
#define TETRIS_CCCI_CORE_MD_LAYOUT	BIT(2)
#define TETRIS_CCCI_CORE_MD_CHECK	BIT(3)
#define TETRIS_CCCI_CORE_MD_IMAGE	BIT(4)
#define TETRIS_CCCI_CORE_SMEM_LAYOUT	BIT(5)
#define TETRIS_CCCI_CORE_NC_SMEM		BIT(6)
#define TETRIS_CCCI_CORE_PHY_CAP		BIT(7)
#define TETRIS_CCCI_CORE_NC_LAYOUT_NUM	BIT(8)
#define TETRIS_CCCI_CORE_C_LAYOUT_NUM	BIT(9)
#define TETRIS_CCCI_CORE_NC_LAYOUT	BIT(10)
#define TETRIS_CCCI_CORE_C_LAYOUT	BIT(11)
#define TETRIS_CCCI_REQUIRED_PAYLOAD \
	(TETRIS_CCCI_CORE_HDR_COUNT | TETRIS_CCCI_CORE_HDR_TABLE | \
	 TETRIS_CCCI_CORE_MD_LAYOUT | TETRIS_CCCI_CORE_MD_CHECK | \
	 TETRIS_CCCI_CORE_MD_IMAGE | TETRIS_CCCI_CORE_SMEM_LAYOUT)
#define TETRIS_CCCI_DIRECT_SMEM_TAGS \
	(TETRIS_CCCI_CORE_NC_LAYOUT_NUM | TETRIS_CCCI_CORE_C_LAYOUT_NUM | \
	 TETRIS_CCCI_CORE_NC_LAYOUT | TETRIS_CCCI_CORE_C_LAYOUT)

enum tetris_ccci_tag_id {
	TETRIS_CCCI_TAG_HDR_COUNT,
	TETRIS_CCCI_TAG_HDR_TABLE,
	TETRIS_CCCI_TAG_MD_LAYOUT,
	TETRIS_CCCI_TAG_MD_CHECK,
	TETRIS_CCCI_TAG_MD_IMAGE,
	TETRIS_CCCI_TAG_SMEM_LAYOUT,
	TETRIS_CCCI_TAG_NC_SMEM,
	TETRIS_CCCI_TAG_PHY_CAP,
	TETRIS_CCCI_TAG_NC_LAYOUT_NUM,
	TETRIS_CCCI_TAG_C_LAYOUT_NUM,
	TETRIS_CCCI_TAG_NC_LAYOUT,
	TETRIS_CCCI_TAG_C_LAYOUT,
	TETRIS_CCCI_TAG_COUNT,
};

enum tetris_ccci_failure {
	TETRIS_CCCI_OK,
	TETRIS_CCCI_NO_FDT,
	TETRIS_CCCI_NO_NODE,
	TETRIS_CCCI_NO_DESCRIPTOR,
	TETRIS_CCCI_BAD_DESCRIPTOR_SIZE,
	TETRIS_CCCI_BAD_DESCRIPTOR,
	TETRIS_CCCI_RANGE_OVERFLOW,
	TETRIS_CCCI_NOT_RESERVED,
	TETRIS_CCCI_NOT_DRAM,
	TETRIS_CCCI_MAP_FAILED,
	TETRIS_CCCI_BAD_TAG_COUNT,
	TETRIS_CCCI_BAD_TAG_HEADER,
	TETRIS_CCCI_BAD_TAG_NAME,
	TETRIS_CCCI_BAD_TAG_DATA,
	TETRIS_CCCI_TAG_CYCLE,
	TETRIS_CCCI_TAG_COUNT_MISMATCH,
	TETRIS_CCCI_DUPLICATE_CORE_TAG,
	TETRIS_CCCI_BAD_DESCRIPTOR_STATUS,
	TETRIS_CCCI_UNSUPPORTED_DESCRIPTOR_VERSION,
	TETRIS_CCCI_MISSING_PAYLOAD_TAG,
	TETRIS_CCCI_BAD_HDR_COUNT,
	TETRIS_CCCI_BAD_MODEM_INFO,
	TETRIS_CCCI_BAD_MD_RANGE,
	TETRIS_CCCI_BAD_MD_LAYOUT,
	TETRIS_CCCI_BAD_CHECK_HEADER,
	TETRIS_CCCI_BAD_IMAGE_SIZE,
	TETRIS_CCCI_BAD_SMEM_LAYOUT,
	TETRIS_CCCI_BAD_SMEM_TAG_SET,
	TETRIS_CCCI_BAD_SMEM_TABLE,
};

struct tetris_ccci_result {
	u32 version;
	u32 size;
	u32 count;
	u32 known_tags;
	u32 payload_valid_mask;
	u32 md_id;
	u32 md_type;
	u32 md_check_header_version;
	u32 md_image_size;
	u32 md_memory_size;
	u32 smem_total_size;
	bool structure_valid;
	bool payload_valid;
	enum tetris_ccci_failure failure;
};

struct tetris_ccci_descriptor {
	u64 base;
	u32 size;
	u32 count;
	u32 version;
	u32 err_no;
	u32 load_flag;
	u32 load_md_errno;
};

struct tetris_ccci_access {
	bool (*range_allowed)(u64 base, u32 size, void *context);
	const void *(*map)(u64 base, u32 size, void *context);
	void (*unmap)(const void *buffer, void *context);
	void *context;
};

struct tetris_ccci_core_tag {
	const char *name;
	enum tetris_ccci_tag_id id;
	u32 bit;
	u32 exact_size;
	u32 size_multiple;
	u32 max_size;
};

struct tetris_ccci_tag_data {
	const u8 *data;
	u32 size;
};

static const struct tetris_ccci_core_tag tetris_ccci_core_tags[] = {
	{ "hdr_count", TETRIS_CCCI_TAG_HDR_COUNT,
	  TETRIS_CCCI_CORE_HDR_COUNT, 4, 0, 0 },
	{ "hdr_tbl_inf", TETRIS_CCCI_TAG_HDR_TABLE,
	  TETRIS_CCCI_CORE_HDR_TABLE, TETRIS_CCCI_MODEM_INFO_SIZE, 0, 0 },
	{ "md_mem_layout", TETRIS_CCCI_TAG_MD_LAYOUT,
	  TETRIS_CCCI_CORE_MD_LAYOUT, 0, TETRIS_CCCI_MD_MEM_BLOCK_SIZE, 1024 },
	{ "md1_chk", TETRIS_CCCI_TAG_MD_CHECK,
	  TETRIS_CCCI_CORE_MD_CHECK, 0, 0, TETRIS_CCCI_MD_CHECK_V6_SIZE },
	{ "md1img", TETRIS_CCCI_TAG_MD_IMAGE,
	  TETRIS_CCCI_CORE_MD_IMAGE, 4, 0, 0 },
	{ "smem_layout", TETRIS_CCCI_TAG_SMEM_LAYOUT,
	  TETRIS_CCCI_CORE_SMEM_LAYOUT, TETRIS_CCCI_SMEM_LAYOUT_SIZE, 0, 0 },
	{ "nc_smem_info_ext", TETRIS_CCCI_TAG_NC_SMEM,
	  TETRIS_CCCI_CORE_NC_SMEM, 0, 16, 0 },
	{ "md1_phy_cap", TETRIS_CCCI_TAG_PHY_CAP,
	  TETRIS_CCCI_CORE_PHY_CAP, 4, 0, 0 },
	{ "nc_smem_layout_num", TETRIS_CCCI_TAG_NC_LAYOUT_NUM,
	  TETRIS_CCCI_CORE_NC_LAYOUT_NUM, 4, 0, 0 },
	{ "c_smem_layout_num", TETRIS_CCCI_TAG_C_LAYOUT_NUM,
	  TETRIS_CCCI_CORE_C_LAYOUT_NUM, 4, 0, 0 },
	{ "nc_smem_layout", TETRIS_CCCI_TAG_NC_LAYOUT,
	  TETRIS_CCCI_CORE_NC_LAYOUT, 0, TETRIS_CCCI_SMEM_REGION_SIZE,
	  TETRIS_CCCI_MAX_INFO_SIZE },
	{ "c_smem_layout", TETRIS_CCCI_TAG_C_LAYOUT,
	  TETRIS_CCCI_CORE_C_LAYOUT, 0, TETRIS_CCCI_SMEM_REGION_SIZE,
	  TETRIS_CCCI_MAX_INFO_SIZE },
};

static const char *const tetris_ccci_failure_names[] = {
	[TETRIS_CCCI_OK] = "none",
	[TETRIS_CCCI_NO_FDT] = "no-fdt",
	[TETRIS_CCCI_NO_NODE] = "no-node",
	[TETRIS_CCCI_NO_DESCRIPTOR] = "no-descriptor",
	[TETRIS_CCCI_BAD_DESCRIPTOR_SIZE] = "bad-descriptor-size",
	[TETRIS_CCCI_BAD_DESCRIPTOR] = "bad-descriptor",
	[TETRIS_CCCI_RANGE_OVERFLOW] = "range-overflow",
	[TETRIS_CCCI_NOT_RESERVED] = "not-reserved",
	[TETRIS_CCCI_NOT_DRAM] = "not-dram",
	[TETRIS_CCCI_MAP_FAILED] = "map-failed",
	[TETRIS_CCCI_BAD_TAG_COUNT] = "bad-tag-count",
	[TETRIS_CCCI_BAD_TAG_HEADER] = "bad-tag-header",
	[TETRIS_CCCI_BAD_TAG_NAME] = "bad-tag-name",
	[TETRIS_CCCI_BAD_TAG_DATA] = "bad-tag-data",
	[TETRIS_CCCI_TAG_CYCLE] = "tag-cycle",
	[TETRIS_CCCI_TAG_COUNT_MISMATCH] = "tag-count-mismatch",
	[TETRIS_CCCI_DUPLICATE_CORE_TAG] = "duplicate-core-tag",
	[TETRIS_CCCI_BAD_DESCRIPTOR_STATUS] = "bad-descriptor-status",
	[TETRIS_CCCI_UNSUPPORTED_DESCRIPTOR_VERSION] = "unsupported-descriptor-version",
	[TETRIS_CCCI_MISSING_PAYLOAD_TAG] = "missing-payload-tag",
	[TETRIS_CCCI_BAD_HDR_COUNT] = "bad-hdr-count",
	[TETRIS_CCCI_BAD_MODEM_INFO] = "bad-modem-info",
	[TETRIS_CCCI_BAD_MD_RANGE] = "bad-md-range",
	[TETRIS_CCCI_BAD_MD_LAYOUT] = "bad-md-layout",
	[TETRIS_CCCI_BAD_CHECK_HEADER] = "bad-check-header",
	[TETRIS_CCCI_BAD_IMAGE_SIZE] = "bad-image-size",
	[TETRIS_CCCI_BAD_SMEM_LAYOUT] = "bad-smem-layout",
	[TETRIS_CCCI_BAD_SMEM_TAG_SET] = "bad-smem-tag-set",
	[TETRIS_CCCI_BAD_SMEM_TABLE] = "bad-smem-table",
};

static bool tetris_ccci_range_end(u64 base, u64 size, u64 *end)
{
	if (!size || base > UINT64_MAX - size)
		return false;

	*end = base + size;
	return true;
}

static bool tetris_ccci_range_contains(u64 outer_base, u64 outer_size,
				       u64 base, u64 size)
{
	u64 end, outer_end;

	return tetris_ccci_range_end(base, size, &end) &&
	       tetris_ccci_range_end(outer_base, outer_size, &outer_end) &&
	       base >= outer_base && end <= outer_end;
}

static u64 tetris_ccci_read_cells(const fdt32_t *cells, int count)
{
	u64 value = 0;

	while (count--)
		value = value << 32 | fdt32_to_cpu(*cells++);

	return value;
}

static bool tetris_ccci_reserved_by_memreserve(const void *fdt, u64 base,
					       u32 size)
{
	u64 reserved_base, reserved_size;
	int count, i;

	count = fdt_num_mem_rsv(fdt);
	if (count < 0)
		return false;

	for (i = 0; i < count; i++) {
		if (fdt_get_mem_rsv(fdt, i, &reserved_base, &reserved_size))
			return false;
		if (tetris_ccci_range_contains(reserved_base, reserved_size,
					       base, size))
			return true;
	}

	return false;
}

static bool tetris_ccci_node_enabled(const void *fdt, int node)
{
	const char *status;
	int len;

	status = fdt_getprop(fdt, node, "status", &len);
	if (!status)
		return len == -FDT_ERR_NOTFOUND;

	return (len == 3 && !memcmp(status, "ok", 3)) ||
	       (len == 5 && !memcmp(status, "okay", 5));
}

static bool tetris_ccci_reserved_by_node(const void *fdt, u64 base, u32 size)
{
	const fdt32_t *reg;
	int addr_cells, child, len, parent, size_cells, tuple_cells;

	parent = fdt_path_offset(fdt, "/reserved-memory");
	if (parent < 0)
		return false;

	addr_cells = fdt_address_cells(fdt, parent);
	size_cells = fdt_size_cells(fdt, parent);
	if (addr_cells < 1 || addr_cells > 2 || size_cells < 1 ||
	    size_cells > 2)
		return false;
	tuple_cells = addr_cells + size_cells;

	fdt_for_each_subnode(child, fdt, parent) {
		int offset;

		if (!tetris_ccci_node_enabled(fdt, child))
			continue;
		reg = fdt_getprop(fdt, child, "reg", &len);
		if (!reg || len <= 0 || len % (tuple_cells * sizeof(*reg)))
			continue;

		for (offset = 0; offset < len / (int)sizeof(*reg);
		     offset += tuple_cells) {
			u64 reserved_base, reserved_size;

			reserved_base = tetris_ccci_read_cells(reg + offset,
							       addr_cells);
			reserved_size = tetris_ccci_read_cells(reg + offset +
								 addr_cells,
						       size_cells);
			if (tetris_ccci_range_contains(reserved_base,
						       reserved_size, base, size))
				return true;
		}
	}

	return false;
}

static bool tetris_ccci_buffer_reserved(const void *fdt, u64 base, u32 size)
{
	return tetris_ccci_reserved_by_memreserve(fdt, base, size) ||
	       tetris_ccci_reserved_by_node(fdt, base, size);
}

static bool tetris_ccci_buffer_in_lk_memory(const void *fdt, u64 base,
					    u32 size)
{
	const fdt32_t *reg;
	const char *device_type;
	int addr_cells, len, node, size_cells, tuple_cells;

	addr_cells = fdt_address_cells(fdt, 0);
	size_cells = fdt_size_cells(fdt, 0);
	if (addr_cells < 1 || addr_cells > 2 || size_cells < 1 ||
	    size_cells > 2)
		return false;
	tuple_cells = addr_cells + size_cells;

	fdt_for_each_subnode(node, fdt, 0) {
		int offset;

		device_type = fdt_getprop(fdt, node, "device_type", &len);
		if (!device_type || len != sizeof("memory") ||
		    memcmp(device_type, "memory", sizeof("memory")))
			continue;
		reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg || len <= 0 || len % (tuple_cells * sizeof(*reg)))
			continue;

		for (offset = 0; offset < len / (int)sizeof(*reg);
		     offset += tuple_cells) {
			const fdt32_t *tuple = reg + offset;
			u64 memory_base, memory_size;

			memory_base = tetris_ccci_read_cells(tuple, addr_cells);
			memory_size = tetris_ccci_read_cells(tuple + addr_cells,
							     size_cells);
			if (tetris_ccci_range_contains(memory_base, memory_size,
						       base, size))
				return true;
		}
	}

	return false;
}

static int tetris_ccci_read_descriptor(const void *fdt,
				       struct tetris_ccci_descriptor *desc,
				       struct tetris_ccci_result *result)
{
	const u8 *raw;
	int len, node;

	if (!fdt || fdt_check_header(fdt)) {
		result->failure = TETRIS_CCCI_NO_FDT;
		return -EINVAL;
	}

	node = fdt_node_offset_by_compatible(fdt, -1, "mediatek,mddriver");
	if (node < 0) {
		result->failure = TETRIS_CCCI_NO_NODE;
		return -ENODATA;
	}

	raw = fdt_getprop(fdt, node, "ccci,modem_info_v2", &len);
	if (!raw) {
		if (len != -FDT_ERR_NOTFOUND) {
			result->failure = TETRIS_CCCI_BAD_DESCRIPTOR_SIZE;
			return -EINVAL;
		}
		raw = fdt_getprop(fdt, node, "ccci,modem_info", &len);
		if (!raw) {
			result->failure = len == -FDT_ERR_NOTFOUND ?
				TETRIS_CCCI_NO_DESCRIPTOR :
				TETRIS_CCCI_BAD_DESCRIPTOR_SIZE;
			return len == -FDT_ERR_NOTFOUND ? -ENODATA : -EINVAL;
		}
		if (len != TETRIS_CCCI_V1_DESC_SIZE) {
			result->failure = TETRIS_CCCI_BAD_DESCRIPTOR_SIZE;
			return -EINVAL;
		}
		desc->base = get_unaligned_le64(raw);
		desc->size = get_unaligned_le32(raw + 8);
		desc->count = get_unaligned_le32(raw + 12);
		desc->version = 1;
	} else {
		if (len != TETRIS_CCCI_V2_DESC_SIZE) {
			result->failure = TETRIS_CCCI_BAD_DESCRIPTOR_SIZE;
			return -EINVAL;
		}
		desc->base = get_unaligned_le64(raw);
		desc->size = get_unaligned_le32(raw + 8);
		desc->err_no = get_unaligned_le32(raw + 12);
		desc->version = get_unaligned_le32(raw + 16);
		desc->count = get_unaligned_le32(raw + 20);
		desc->load_flag = get_unaligned_le32(raw + 24);
		desc->load_md_errno = get_unaligned_le32(raw + 28);
	}

	result->version = desc->version;
	result->size = desc->size;
	result->count = desc->count;
	if (!desc->base || !desc->size || desc->size > TETRIS_CCCI_MAX_INFO_SIZE ||
	    !desc->version || desc->version > 0x7fffffffU) {
		result->failure = TETRIS_CCCI_BAD_DESCRIPTOR;
		return -EINVAL;
	}
	if (desc->base > UINT64_MAX - desc->size) {
		result->failure = TETRIS_CCCI_RANGE_OVERFLOW;
		return -EOVERFLOW;
	}
	if (!desc->count || desc->count > TETRIS_CCCI_MAX_TAGS) {
		result->failure = TETRIS_CCCI_BAD_TAG_COUNT;
		return -EINVAL;
	}
	if (desc->version >= 2 &&
	    (desc->err_no || desc->load_md_errno)) {
		result->failure = TETRIS_CCCI_BAD_DESCRIPTOR_STATUS;
		return -EINVAL;
	}
	if (desc->version > 3) {
		result->failure = TETRIS_CCCI_UNSUPPORTED_DESCRIPTOR_VERSION;
		return -EPROTONOSUPPORT;
	}

	return 0;
}

static bool tetris_ccci_check_core_size(u32 bit, u32 size)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(tetris_ccci_core_tags); i++) {
		const struct tetris_ccci_core_tag *core =
			&tetris_ccci_core_tags[i];

		if (core->bit != bit)
			continue;
		if (core->exact_size)
			return size == core->exact_size;
		if (!size || (core->max_size && size > core->max_size) ||
		    (core->size_multiple && size % core->size_multiple))
			return false;
		if (bit == TETRIS_CCCI_CORE_MD_CHECK)
			return size == 188 || size == 344 || size == 512;
		return true;
	}

	return true;
}

static u32 tetris_ccci_core_bit(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(tetris_ccci_core_tags); i++)
		if (!strcmp(name, tetris_ccci_core_tags[i].name))
			return tetris_ccci_core_tags[i].bit;

	return 0;
}

static int tetris_ccci_validate_tags(const u8 *buffer,
				     const struct tetris_ccci_descriptor *desc,
				     struct tetris_ccci_result *result,
				     struct tetris_ccci_tag_data tags[])
{
	u32 header_size, name_size, offset = 0, seen = 0;
	static u32 visited[TETRIS_CCCI_MAX_TAGS];
	unsigned int i, j;

	if (desc->version >= 2) {
		header_size = TETRIS_CCCI_V2_TAG_SIZE;
		name_size = TETRIS_CCCI_V2_NAME_SIZE;
	} else {
		header_size = TETRIS_CCCI_V1_TAG_SIZE;
		name_size = TETRIS_CCCI_V1_NAME_SIZE;
	}
	if (desc->count > desc->size / header_size) {
		result->failure = TETRIS_CCCI_BAD_TAG_COUNT;
		return -EINVAL;
	}

	for (i = 0; i < desc->count; i++) {
		const u8 *header;
		const char *nul;
		char name[TETRIS_CCCI_V2_NAME_SIZE];
		u32 bit, data_offset, data_size, next_offset;

		for (j = 0; j < i; j++) {
			if (visited[j] == offset) {
				result->failure = TETRIS_CCCI_TAG_CYCLE;
				return -EINVAL;
			}
		}
		visited[i] = offset;
		if (offset > desc->size || header_size > desc->size - offset) {
			result->failure = TETRIS_CCCI_BAD_TAG_HEADER;
			return -EINVAL;
		}

		header = buffer + offset;
		nul = memchr(header, '\0', name_size);
		if (!nul || nul == (const char *)header) {
			result->failure = TETRIS_CCCI_BAD_TAG_NAME;
			return -EINVAL;
		}
		memcpy(name, header, nul - (const char *)header);
		name[nul - (const char *)header] = '\0';
		data_offset = get_unaligned_le32(header + name_size);
		data_size = get_unaligned_le32(header + name_size + 4);
		next_offset = get_unaligned_le32(header + name_size + 8);
		if (data_offset > desc->size || data_size > desc->size - data_offset) {
			result->failure = TETRIS_CCCI_BAD_TAG_DATA;
			return -EINVAL;
		}

		bit = tetris_ccci_core_bit(name);
		if (bit) {
			const struct tetris_ccci_core_tag *core = NULL;
			unsigned int core_index;

			if (seen & bit) {
				result->failure = TETRIS_CCCI_DUPLICATE_CORE_TAG;
				return -EINVAL;
			}
			if (!tetris_ccci_check_core_size(bit, data_size)) {
				result->failure = TETRIS_CCCI_BAD_TAG_DATA;
				return -EINVAL;
			}
			for (core_index = 0;
			     core_index < ARRAY_SIZE(tetris_ccci_core_tags);
			     core_index++) {
				if (tetris_ccci_core_tags[core_index].bit == bit) {
					core = &tetris_ccci_core_tags[core_index];
					break;
				}
			}
			if (!core)
				return -EINVAL;
			tags[core->id].data = buffer + data_offset;
			tags[core->id].size = data_size;
			seen |= bit;
		}

		if (i + 1 == desc->count) {
			if (next_offset) {
				result->failure = TETRIS_CCCI_TAG_COUNT_MISMATCH;
				return -EINVAL;
			}
		} else if (!next_offset) {
			result->failure = TETRIS_CCCI_TAG_COUNT_MISMATCH;
			return -EINVAL;
		}
		offset = next_offset;
	}

	result->known_tags = seen;
	return 0;
}

static bool tetris_ccci_ranges_overlap(u64 first_base, u64 first_size,
				       u64 second_base, u64 second_size)
{
	u64 first_end, second_end;

	if (!tetris_ccci_range_end(first_base, first_size, &first_end) ||
	    !tetris_ccci_range_end(second_base, second_size, &second_end))
		return true;

	return first_base < second_end && second_base < first_end;
}

static bool tetris_ccci_payload_range_allowed(const void *fdt,
					      const struct tetris_ccci_access *access,
					      u64 base, u32 size)
{
	return tetris_ccci_buffer_reserved(fdt, base, size) &&
	       tetris_ccci_buffer_in_lk_memory(fdt, base, size) &&
	       access && access->range_allowed &&
	       access->range_allowed(base, size, access->context);
}

static int tetris_ccci_validate_md_layout(const struct tetris_ccci_tag_data *layout,
					  u64 md_base, u32 md_size,
					  struct tetris_ccci_result *result)
{
	u32 count = layout->size / TETRIS_CCCI_MD_MEM_BLOCK_SIZE;
	unsigned int i, j;

	if (!count) {
		result->failure = TETRIS_CCCI_BAD_MD_LAYOUT;
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		const u8 *entry = layout->data +
			i * TETRIS_CCCI_MD_MEM_BLOCK_SIZE;
		u32 offset = get_unaligned_le32(entry);
		u32 size = get_unaligned_le32(entry + 4);
		u64 ap_phy = get_unaligned_le64(entry + 16);
		u64 expected;

		if (!size || offset > md_size || size > md_size - offset ||
		    md_base > UINT64_MAX - offset) {
			result->failure = TETRIS_CCCI_BAD_MD_LAYOUT;
			return -ERANGE;
		}
		expected = md_base + offset;
		if (ap_phy != expected ||
		    !tetris_ccci_range_contains(md_base, md_size, ap_phy, size)) {
			result->failure = TETRIS_CCCI_BAD_MD_LAYOUT;
			return -EINVAL;
		}

		for (j = 0; j < i; j++) {
			const u8 *previous = layout->data +
				j * TETRIS_CCCI_MD_MEM_BLOCK_SIZE;
			u32 previous_offset = get_unaligned_le32(previous);
			u32 previous_size = get_unaligned_le32(previous + 4);

			if (tetris_ccci_ranges_overlap(offset, size,
						       previous_offset,
						       previous_size)) {
				result->failure = TETRIS_CCCI_BAD_MD_LAYOUT;
				return -EINVAL;
			}
		}
	}

	result->payload_valid_mask |= TETRIS_CCCI_CORE_MD_LAYOUT;
	return 0;
}

static int tetris_ccci_validate_check_header(const struct tetris_ccci_tag_data *check,
					     u32 md_type, u32 md_size,
					     struct tetris_ccci_result *result)
{
	const u8 *data = check->data;
	u32 header_version = get_unaligned_le32(data + 12);
	u32 product_version = get_unaligned_le32(data + 16);
	u32 image_type = get_unaligned_le32(data + 20);
	u32 memory_size = get_unaligned_le32(data + 172);
	u32 image_size = get_unaligned_le32(data + 176);
	u32 declared_size = get_unaligned_le32(data + check->size - 4);
	bool version_valid;

	if (check->size == TETRIS_CCCI_MD_CHECK_V2_SIZE)
		version_valid = header_version == 1 || header_version == 2;
	else if (check->size == TETRIS_CCCI_MD_CHECK_V5_SIZE)
		version_valid = header_version == 5;
	else
		version_valid = header_version == 6;

	if (memcmp(data, TETRIS_CCCI_MD_CHECK_MAGIC,
		   TETRIS_CCCI_MD_CHECK_MAGIC_SIZE) ||
	    !version_valid || !product_version || product_version > 2 ||
	    !image_type || image_type > TETRIS_CCCI_MD_TYPE_MAX ||
	    image_type != md_type || data[168] != 1 ||
	    declared_size != check->size) {
		result->failure = TETRIS_CCCI_BAD_CHECK_HEADER;
		return -EINVAL;
	}
	if (!memory_size || memory_size > md_size || !image_size ||
	    image_size > memory_size) {
		result->failure = TETRIS_CCCI_BAD_IMAGE_SIZE;
		return -ERANGE;
	}

	result->md_check_header_version = header_version;
	result->md_image_size = image_size;
	result->md_memory_size = memory_size;
	result->payload_valid_mask |= TETRIS_CCCI_CORE_MD_CHECK;
	return 0;
}

static int tetris_ccci_validate_smem_layout(const void *fdt,
					    const struct tetris_ccci_access *access,
					    const struct tetris_ccci_tag_data *layout,
					    u64 md_base, u32 md_size,
					    struct tetris_ccci_result *result)
{
	const u8 *data = layout->data;
	u64 base = get_unaligned_le64(data);
	u32 total_size = get_unaligned_le32(data + 36);
	unsigned int i;

	if (!base || !total_size || !get_unaligned_le32(data + 12) ||
	    !tetris_ccci_payload_range_allowed(fdt, access, base, total_size) ||
	    tetris_ccci_ranges_overlap(base, total_size, md_base, md_size)) {
		result->failure = TETRIS_CCCI_BAD_SMEM_LAYOUT;
		return -ERANGE;
	}

	for (i = 0; i < 3; i++) {
		u32 offset = get_unaligned_le32(data + 8 + i * 8);
		u32 size = get_unaligned_le32(data + 12 + i * 8);

		if (offset > total_size || size > total_size - offset) {
			result->failure = TETRIS_CCCI_BAD_SMEM_LAYOUT;
			return -ERANGE;
		}
	}

	result->smem_total_size = total_size;
	result->payload_valid_mask |= TETRIS_CCCI_CORE_SMEM_LAYOUT;
	return 0;
}

static int tetris_ccci_validate_smem_table(const void *fdt,
					   const struct tetris_ccci_access *access,
					   const struct tetris_ccci_tag_data *count_tag,
					   const struct tetris_ccci_tag_data *layout,
					   struct tetris_ccci_result *result)
{
	u32 count = get_unaligned_le32(count_tag->data);
	unsigned int i, j;

	if (!count || count > UINT32_MAX / TETRIS_CCCI_SMEM_REGION_SIZE ||
	    layout->size != count * TETRIS_CCCI_SMEM_REGION_SIZE) {
		result->failure = TETRIS_CCCI_BAD_SMEM_TABLE;
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		const u8 *entry = layout->data +
			i * TETRIS_CCCI_SMEM_REGION_SIZE;
		u64 base = get_unaligned_le64(entry);
		u32 id = get_unaligned_le32(entry + 16);
		u32 size = get_unaligned_le32(entry + 24);
		u32 align = get_unaligned_le32(entry + 28);

		if (!base || !size ||
		    (align && ((align & (align - 1)) || (base & (align - 1)))) ||
		    !tetris_ccci_payload_range_allowed(fdt, access, base, size)) {
			result->failure = TETRIS_CCCI_BAD_SMEM_TABLE;
			return -ERANGE;
		}

		for (j = 0; j < i; j++) {
			const u8 *previous = layout->data +
				j * TETRIS_CCCI_SMEM_REGION_SIZE;
			u64 previous_base = get_unaligned_le64(previous);
			u32 previous_id = get_unaligned_le32(previous + 16);
			u32 previous_size = get_unaligned_le32(previous + 24);

			if (id == previous_id ||
			    tetris_ccci_ranges_overlap(base, size, previous_base,
						       previous_size)) {
				result->failure = TETRIS_CCCI_BAD_SMEM_TABLE;
				return -EINVAL;
			}
		}
	}

	return 0;
}

static bool tetris_ccci_smem_tables_overlap(const struct tetris_ccci_tag_data *first,
					    const struct tetris_ccci_tag_data *second)
{
	u32 first_count = first->size / TETRIS_CCCI_SMEM_REGION_SIZE;
	u32 second_count = second->size / TETRIS_CCCI_SMEM_REGION_SIZE;
	unsigned int i, j;

	for (i = 0; i < first_count; i++) {
		const u8 *first_entry = first->data +
			i * TETRIS_CCCI_SMEM_REGION_SIZE;
		u64 first_base = get_unaligned_le64(first_entry);
		u32 first_size = get_unaligned_le32(first_entry + 24);

		for (j = 0; j < second_count; j++) {
			const u8 *second_entry = second->data +
				j * TETRIS_CCCI_SMEM_REGION_SIZE;
			u64 second_base = get_unaligned_le64(second_entry);
			u32 second_size = get_unaligned_le32(second_entry + 24);

			if (tetris_ccci_ranges_overlap(first_base, first_size,
						       second_base, second_size))
				return true;
		}
	}

	return false;
}

static int tetris_ccci_validate_payload(const void *fdt,
					const struct tetris_ccci_access *access,
					const struct tetris_ccci_descriptor *desc,
					const struct tetris_ccci_tag_data tags[],
					struct tetris_ccci_result *result)
{
	const struct tetris_ccci_tag_data *modem =
		&tags[TETRIS_CCCI_TAG_HDR_TABLE];
	const struct tetris_ccci_tag_data *nc_count =
		&tags[TETRIS_CCCI_TAG_NC_LAYOUT_NUM];
	const struct tetris_ccci_tag_data *c_count =
		&tags[TETRIS_CCCI_TAG_C_LAYOUT_NUM];
	const struct tetris_ccci_tag_data *nc_layout =
		&tags[TETRIS_CCCI_TAG_NC_LAYOUT];
	const struct tetris_ccci_tag_data *c_layout =
		&tags[TETRIS_CCCI_TAG_C_LAYOUT];
	u32 direct_tags = result->known_tags & TETRIS_CCCI_DIRECT_SMEM_TAGS;
	u64 md_base;
	u32 md_size, raw_image_size;
	int ret;

	(void)desc->load_flag;
	if ((result->known_tags & TETRIS_CCCI_REQUIRED_PAYLOAD) !=
	    TETRIS_CCCI_REQUIRED_PAYLOAD) {
		result->failure = TETRIS_CCCI_MISSING_PAYLOAD_TAG;
		return -ENODATA;
	}
	if (get_unaligned_le32(tags[TETRIS_CCCI_TAG_HDR_COUNT].data) != 1) {
		result->failure = TETRIS_CCCI_BAD_HDR_COUNT;
		return -EINVAL;
	}
	result->payload_valid_mask |= TETRIS_CCCI_CORE_HDR_COUNT;

	md_base = get_unaligned_le64(modem->data);
	md_size = get_unaligned_le32(modem->data + 8);
	result->md_id = modem->data[12];
	result->md_type = modem->data[14];
	if (!md_base || !md_size || result->md_id || modem->data[13] ||
	    !result->md_type || result->md_type > TETRIS_CCCI_MD_TYPE_MAX) {
		result->failure = TETRIS_CCCI_BAD_MODEM_INFO;
		return -EINVAL;
	}
	if (!tetris_ccci_payload_range_allowed(fdt, access, md_base, md_size)) {
		result->failure = TETRIS_CCCI_BAD_MD_RANGE;
		return -ERANGE;
	}
	result->payload_valid_mask |= TETRIS_CCCI_CORE_HDR_TABLE;

	ret = tetris_ccci_validate_md_layout(&tags[TETRIS_CCCI_TAG_MD_LAYOUT],
					     md_base, md_size, result);
	if (ret)
		return ret;
	ret = tetris_ccci_validate_check_header(&tags[TETRIS_CCCI_TAG_MD_CHECK],
						result->md_type, md_size, result);
	if (ret)
		return ret;

	raw_image_size = get_unaligned_le32(tags[TETRIS_CCCI_TAG_MD_IMAGE].data);
	if (!raw_image_size || raw_image_size < result->md_image_size ||
	    raw_image_size > md_size) {
		result->failure = TETRIS_CCCI_BAD_IMAGE_SIZE;
		return -ERANGE;
	}
	result->payload_valid_mask |= TETRIS_CCCI_CORE_MD_IMAGE;

	ret = tetris_ccci_validate_smem_layout(fdt, access,
					       &tags[TETRIS_CCCI_TAG_SMEM_LAYOUT],
					       md_base, md_size, result);
	if (ret)
		return ret;

	if (direct_tags && direct_tags != TETRIS_CCCI_DIRECT_SMEM_TAGS) {
		result->failure = TETRIS_CCCI_BAD_SMEM_TAG_SET;
		return -EINVAL;
	}
	if (direct_tags) {
		ret = tetris_ccci_validate_smem_table(fdt, access, nc_count,
						      nc_layout, result);
		if (ret)
			return ret;
		ret = tetris_ccci_validate_smem_table(fdt, access, c_count,
						      c_layout, result);
		if (ret)
			return ret;
		if (tetris_ccci_smem_tables_overlap(nc_layout, c_layout)) {
			result->failure = TETRIS_CCCI_BAD_SMEM_TABLE;
			return -EINVAL;
		}
		result->payload_valid_mask |= TETRIS_CCCI_DIRECT_SMEM_TAGS;
	}

	result->payload_valid = true;
	return 0;
}

static int tetris_ccci_observe(const void *fdt,
			       const struct tetris_ccci_access *access,
			       struct tetris_ccci_result *result)
{
	struct tetris_ccci_descriptor desc = { 0 };
	struct tetris_ccci_tag_data tags[TETRIS_CCCI_TAG_COUNT] = { 0 };
	const void *buffer;
	int ret;

	memset(result, 0, sizeof(*result));
	ret = tetris_ccci_read_descriptor(fdt, &desc, result);
	if (ret)
		return ret;
	if (!tetris_ccci_buffer_reserved(fdt, desc.base, desc.size)) {
		result->failure = TETRIS_CCCI_NOT_RESERVED;
		return -EPERM;
	}
	if (!tetris_ccci_buffer_in_lk_memory(fdt, desc.base, desc.size) ||
	    !access || !access->range_allowed ||
	    !access->range_allowed(desc.base, desc.size, access->context)) {
		result->failure = TETRIS_CCCI_NOT_DRAM;
		return -ERANGE;
	}
	if (!access->map) {
		result->failure = TETRIS_CCCI_MAP_FAILED;
		return -EFAULT;
	}

	buffer = access->map(desc.base, desc.size, access->context);
	if (!buffer) {
		result->failure = TETRIS_CCCI_MAP_FAILED;
		return -EFAULT;
	}
	ret = tetris_ccci_validate_tags(buffer, &desc, result, tags);
	if (!ret) {
		result->structure_valid = true;
		ret = tetris_ccci_validate_payload(fdt, access, &desc, tags,
						   result);
	}
	if (access->unmap)
		access->unmap(buffer, access->context);
	if (ret)
		return ret;

	result->failure = TETRIS_CCCI_OK;
	return 0;
}

static int tetris_ccci_publish_status(void *fdt,
				      const struct tetris_ccci_result *result)
{
	const char *failure, *observation, *payload_status;
	int chosen, node, ret;

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return chosen;
	node = fdt_subnode_offset(fdt, chosen, "nothing,ccci-handoff-status");
	if (node >= 0) {
		ret = fdt_del_node(fdt, node);
		if (ret)
			return ret;
	} else if (node != -FDT_ERR_NOTFOUND) {
		return node;
	}
	node = fdt_add_subnode(fdt, chosen, "nothing,ccci-handoff-status");
	if (node < 0)
		return node;

	failure = result->failure < ARRAY_SIZE(tetris_ccci_failure_names) ?
		tetris_ccci_failure_names[result->failure] : "unknown";
	observation = result->structure_valid ? "structure-valid" : "invalid";
	payload_status = result->payload_valid ? "valid" :
		result->structure_valid ? "invalid" : "not-checked";
	ret = fdt_setprop_u32(fdt, node, "version", result->version);
	if (!ret)
		ret = fdt_setprop_u32(fdt, node, "size", result->size);
	if (!ret)
		ret = fdt_setprop_u32(fdt, node, "count", result->count);
	if (!ret)
		ret = fdt_setprop_u32(fdt, node, "known-tag-mask",
				      result->known_tags);
	if (!ret)
		ret = fdt_setprop_u32(fdt, node, "payload-valid-mask",
				      result->payload_valid_mask);
	if (!ret)
		ret = fdt_setprop_u32(fdt, node, "failure-code", result->failure);
	if (!ret)
		ret = fdt_setprop_string(fdt, node, "failure", failure);
	if (!ret)
		ret = fdt_setprop_string(fdt, node, "observation-status",
					 observation);
	if (!ret)
		ret = fdt_setprop_string(fdt, node, "payload-status",
					 payload_status);
	if (result->structure_valid && !result->payload_valid) {
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "payload-failure-code",
					      result->failure);
		if (!ret)
			ret = fdt_setprop_string(fdt, node, "payload-failure",
						 failure);
	}
	if (result->structure_valid)
		if (!ret)
			ret = fdt_setprop(fdt, node, "structure-validated", NULL, 0);
	if (result->payload_valid) {
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "md-id", result->md_id);
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "md-type",
					      result->md_type);
		if (!ret)
			ret = fdt_setprop_u32(fdt, node,
					      "md-check-header-version",
					      result->md_check_header_version);
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "md-image-size",
					      result->md_image_size);
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "md-memory-size",
					      result->md_memory_size);
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "smem-total-size",
					      result->smem_total_size);
		if (!ret)
			ret = fdt_setprop(fdt, node, "payload-validated", NULL, 0);
	}
	if (!ret)
		return 0;

	fdt_del_node(fdt, node);
	return ret;
}

#ifndef TETRIS_CCCI_HANDOFF_HOST_TEST

DECLARE_GLOBAL_DATA_PTR;

#define MTK_SIP_CONNSYS_EMI_SET		0xc200041a
#define MTK_SIP_CONNSYS_EMI_MAIN	0
#define MTK_SIP_CONNSYS_EMI_MD		1
#define MTK_SIP_CONNSYS_EMI_GPS		2
#define MTK_SIP_CONNSYS_EMI_SUCCESS	0x100
#define MTK_SIP_EMIMPU_SET		0x82000415
#define MTK_SIP_EMIMPU_PAGE_SHIFT	12

#define TETRIS_CONNSYS_EMI_BASE		0x85e00000ULL
#define TETRIS_CONNSYS_EMI_SIZE		0x00c00000ULL
#define TETRIS_GPS_EMI_BASE		0x86a00000ULL
#define TETRIS_GPS_EMI_SIZE		0x00100000ULL
#define TETRIS_MD_CACHE_EMI_BASE	0x88000000ULL
#define TETRIS_MD_CACHE_EMI_SIZE	0x02560000ULL
#define TETRIS_MD_CONNSYS_EMI_SIZE	0x00d80000ULL
#define TETRIS_DEVINFO_MAX_WORDS	400

#define TETRIS_WDT_BASE			0x1c00a000UL
#define TETRIS_WDT_NONRST_REG2		0x24
#define TETRIS_REBOOT_MODE_MASK		0xf
#define TETRIS_REBOOT_MODE_BOOTLOADER	0x3

struct tetris_devinfo_tag {
	u32 data_size;
	u32 data[];
};

struct tetris_emimpu_region {
	u64 start;
	u64 end;
	unsigned long id;
	const char *name;
};

int board_late_init(void)
{
	void __iomem *reboot_mode = (void __iomem *)(TETRIS_WDT_BASE +
						      TETRIS_WDT_NONRST_REG2);
	u32 mode = readl(reboot_mode) & TETRIS_REBOOT_MODE_MASK;

	if (mode != TETRIS_REBOOT_MODE_BOOTLOADER)
		return 0;

	/* Consume the request once so a later reset cannot loop into fastboot. */
	clrbits_le32(reboot_mode, TETRIS_REBOOT_MODE_MASK);
	readl(reboot_mode);

	printf("Tetris: Linux requested U-Boot fastboot\n");
	return env_set("bootcmd", "run fastboot");
}

static const struct tetris_emimpu_region tetris_connsys_emimpu_regions[] = {
	{ 0x85e00000ULL, 0x86480000ULL, 0x2c, "conninfra RO" },
	{ 0x86480000ULL, 0x86900000ULL, 0x2e, "conninfra RW" },
	{ 0x86900000ULL, 0x87f00000ULL, 0x2f, "GPS and WiFi DMA" },
	{ 0x87f00000ULL, 0x87f20000ULL, 0x2d, "connscp shared" },
};

static int tetris_set_emimpu_region(const struct tetris_emimpu_region *region)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_EMIMPU_SET, 0,
		      region->start >> MTK_SIP_EMIMPU_PAGE_SHIFT,
		      region->end >> MTK_SIP_EMIMPU_PAGE_SHIFT,
		      region->id, 0, 0, 0, &res);
	if (res.a0) {
		printf("Tetris: %s EMI MPU region %lx failed: %lx\n",
		       region->name, region->id, res.a0);
		return -EIO;
	}

	printf("Tetris: %s EMI MPU region %lx set at %llx..%llx\n",
	       region->name, region->id,
	       (unsigned long long)region->start,
	       (unsigned long long)region->end);

	return 0;
}

static int tetris_set_connsys_emimpu_regions(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(tetris_connsys_emimpu_regions); i++) {
		ret = tetris_set_emimpu_region(&tetris_connsys_emimpu_regions[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int tetris_set_connsys_emi(unsigned long selector, u64 base, u64 size,
				  const char *name)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_CONNSYS_EMI_SET, selector, base, size,
		      0, 0, 0, 0, &res);
	if (res.a0 != MTK_SIP_CONNSYS_EMI_SUCCESS) {
		printf("Tetris: %s EMI SMC failed: %lx\n", name, res.a0);
		return -EIO;
	}

	printf("Tetris: %s EMI registered at %llx, size %llx\n", name,
	       (unsigned long long)base, (unsigned long long)size);

	return 0;
}

static int tetris_prepare_connsys_emi(const void *fdt)
{
	fdt_size_t md_reserved_size, reserved_size;
	fdt_addr_t base, md_base;
	const fdt32_t *memory_region;
	u64 emi_size, gps_base, gps_size;
	int consys, gps, len, md_reserved, reserved, ret;

	consys = fdt_node_offset_by_compatible(fdt, -1,
					       "mediatek,mt6878-6631-consys");
	if (consys < 0) {
		printf("Tetris: conninfra node missing: %s\n",
		       fdt_strerror(consys));
		return consys;
	}

	memory_region = fdt_getprop(fdt, consys, "memory-region", &len);
	if (!memory_region || len != sizeof(*memory_region)) {
		printf("Tetris: invalid conninfra memory-region\n");
		return -FDT_ERR_BADPHANDLE;
	}

	reserved = fdt_node_offset_by_phandle(fdt,
					      fdt32_to_cpu(*memory_region));
	if (reserved < 0) {
		printf("Tetris: conninfra reserved memory missing: %s\n",
		       fdt_strerror(reserved));
		return reserved;
	}

	base = fdtdec_get_addr_size_auto_noparent(fdt, reserved, "reg", 0,
						  &reserved_size, true);
	if (base == FDT_ADDR_T_NONE) {
		printf("Tetris: invalid conninfra reserved memory reg\n");
		return -FDT_ERR_BADVALUE;
	}

	emi_size = fdtdec_get_uint(fdt, consys, "emi-size", 0);
	gps = fdt_node_offset_by_compatible(fdt, -1, "mediatek,mt6878-gps");
	if (gps < 0) {
		printf("Tetris: GPS node missing: %s\n", fdt_strerror(gps));
		return gps;
	}

	gps_size = fdtdec_get_uint(fdt, gps, "emi-size", 0);
	gps_base = base + emi_size;
	if (base != TETRIS_CONNSYS_EMI_BASE ||
	    emi_size != TETRIS_CONNSYS_EMI_SIZE ||
	    gps_base != TETRIS_GPS_EMI_BASE ||
	    gps_size != TETRIS_GPS_EMI_SIZE ||
	    reserved_size < emi_size + gps_size) {
		printf("Tetris: unsafe EMI layout conn=%llx/%llx gps=%llx/%llx reserved=%llx\n",
		       (unsigned long long)base,
		       (unsigned long long)emi_size,
		       (unsigned long long)gps_base,
		       (unsigned long long)gps_size,
		       (unsigned long long)reserved_size);
		return -FDT_ERR_BADVALUE;
	}

	md_reserved = fdt_node_offset_by_compatible(fdt, -1,
						    "mediatek,ap_md_c_smem");
	if (md_reserved < 0) {
		printf("Tetris: AP/MD cache shared memory missing: %s\n",
		       fdt_strerror(md_reserved));
		return md_reserved;
	}

	md_base = fdtdec_get_addr_size_auto_noparent(fdt, md_reserved, "reg", 0,
						     &md_reserved_size, true);
	if (md_base != TETRIS_MD_CACHE_EMI_BASE ||
	    md_reserved_size != TETRIS_MD_CACHE_EMI_SIZE) {
		printf("Tetris: unsafe AP/MD cache layout %llx/%llx\n",
		       (unsigned long long)md_base,
		       (unsigned long long)md_reserved_size);
		return -FDT_ERR_BADVALUE;
	}

	ret = tetris_set_connsys_emimpu_regions();
	if (ret)
		return ret;

	ret = tetris_set_connsys_emi(MTK_SIP_CONNSYS_EMI_MAIN, base,
				     emi_size, "conninfra");
	if (ret)
		return ret;

	ret = tetris_set_connsys_emi(MTK_SIP_CONNSYS_EMI_MD, md_base,
				     TETRIS_MD_CONNSYS_EMI_SIZE,
				     "modem conninfra");
	if (ret)
		return ret;

	return tetris_set_connsys_emi(MTK_SIP_CONNSYS_EMI_GPS, gps_base,
				      gps_size, "GPS");
}

static int tetris_handoff_devinfo(void *fdt)
{
	const struct tetris_devinfo_tag *tag;
	const void *prev_fdt;
	int chosen, len, prev_chosen, ret;
	ulong prev_fdt_addr;
	u32 words;

	prev_fdt_addr = env_get_hex("prevbl_fdt_addr", 0);
	if (!prev_fdt_addr)
		return -ENODATA;

	prev_fdt = (const void *)prev_fdt_addr;
	if (fdt_check_header(prev_fdt))
		return -ENODATA;

	prev_chosen = fdt_path_offset(prev_fdt, "/chosen");
	if (prev_chosen < 0)
		prev_chosen = fdt_path_offset(prev_fdt, "/chosen@0");
	if (prev_chosen < 0)
		return prev_chosen;

	tag = fdt_getprop(prev_fdt, prev_chosen, "atag,devinfo", &len);
	if (!tag || len < sizeof(*tag))
		return -ENODATA;

	memcpy(&words, &tag->data_size, sizeof(words));
	if (!words || words > TETRIS_DEVINFO_MAX_WORDS ||
	    len < sizeof(*tag) + words * sizeof(u32))
		return -EINVAL;

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return chosen;

	ret = fdt_setprop(fdt, chosen, "atag,devinfo", tag,
			  sizeof(*tag) + words * sizeof(u32));
	if (ret)
		return ret;

	printf("Tetris: handed off %u devinfo words from LK\n", words);

	return 0;
}

static bool tetris_ccci_in_uboot_dram(u64 base, u32 size, void *context)
{
	const struct bd_info *bd = gd->bd;
	phys_addr_t phys = (phys_addr_t)base;
	unsigned int i;

	(void)context;
	if ((u64)phys != base)
		return false;

	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++) {
		if (tetris_ccci_range_contains(bd->bi_dram[i].start,
					       bd->bi_dram[i].size, base, size))
			return true;
	}

	return false;
}

static const void *tetris_ccci_map(u64 base, u32 size, void *context)
{
	(void)context;
	return map_sysmem((phys_addr_t)base, size);
}

static void tetris_ccci_unmap(const void *buffer, void *context)
{
	(void)context;
	unmap_sysmem(buffer);
}

static int tetris_observe_ccci_handoff(void *fdt)
{
	const struct tetris_ccci_access access = {
		.range_allowed = tetris_ccci_in_uboot_dram,
		.map = tetris_ccci_map,
		.unmap = tetris_ccci_unmap,
	};
	struct tetris_ccci_result result = {
		.failure = TETRIS_CCCI_NO_FDT,
	};
	const void *prev_fdt = NULL;
	const char *failure;
	ulong prev_fdt_addr;
	int publish_ret, ret;

	prev_fdt_addr = env_get_hex("prevbl_fdt_addr", 0);
	if (prev_fdt_addr)
		prev_fdt = (const void *)prev_fdt_addr;
	if (!prev_fdt || fdt_check_header(prev_fdt))
		ret = -ENODATA;
	else
		ret = tetris_ccci_observe(prev_fdt, &access, &result);

	publish_ret = tetris_ccci_publish_status(fdt, &result);
	if (publish_ret)
		printf("Tetris: CCCI status publication failed: %d\n",
		       publish_ret);

	failure = result.failure < ARRAY_SIZE(tetris_ccci_failure_names) ?
		tetris_ccci_failure_names[result.failure] : "unknown";
	printf("Tetris: CCCI structure=%s payload=%s: v%u size=%u tags=%u known=%x failure=%s\n",
	       result.structure_valid ? "valid" : "invalid",
	       result.payload_valid ? "valid" :
	       result.structure_valid ? "invalid" : "not-checked",
	       result.version, result.size, result.count, result.known_tags,
	       failure);

	return ret;
}

void board_prep_linux(struct bootm_headers *images)
{
	int ret;
	void *fdt = (void *)images->ft_addr;

	ret = tetris_handoff_devinfo(fdt);
	if (ret)
		printf("Tetris: LK devinfo handoff unavailable: %d\n", ret);

	/* Observation only: validation failure must not alter the boot path. */
	tetris_observe_ccci_handoff(fdt);

	ret = tetris_prepare_connsys_emi(fdt);
	if (ret)
		panic("Tetris: refusing Linux boot without conninfra EMI mapping\n");
}

static int tetris_boot_pmos(const char *extra_bootargs)
{
	int ret;

	env_set("bootargs_extra", extra_bootargs ? extra_bootargs : "");
	if (run_command("run scan_storage", 0)) {
		ret = -EIO;
		goto out;
	}
	if (run_command("run find_pmos_partitions", 0)) {
		ret = -ENOENT;
		goto out;
	}
	if (run_command("run set_pmos_bootargs", 0)) {
		ret = -EINVAL;
		goto out;
	}
	if (run_command("ext4load scsi 2:${pmos_root_part} 0x85e00000 "
			"/usr/lib/firmware/connsys_bt_mt6878_mt6631.bin "
			"0xb4e5c", 0)) {
		ret = -ENOENT;
		goto out;
	}
	if (run_command("ext4load scsi 2:${pmos_root_part} 0x860e0000 "
			"/usr/lib/firmware/connsys_gnss_mt6878_mt6631.bin "
			"0x7fc68", 0)) {
		ret = -ENOENT;
		goto out;
	}
	if (run_command("ext4load scsi 2:${pmos_root_part} 0x86180000 "
			"/usr/lib/firmware/connsys_wifi_mt6878_mt6631.bin "
			"0x174c24", 0)) {
		ret = -ENOENT;
		goto out;
	}
	if (run_command("run load_boot_image", 0)) {
		ret = -ENOENT;
		goto out;
	}

	ret = run_command("bootm 0x49000000", 0);

out:
	env_set("bootargs_extra", "");

	return ret;
}

void fastboot_oem_board(char *cmd_parameter, void *data, u32 size, char *response)
{
	if (!cmd_parameter) {
		fastboot_fail("missing oem_board command", response);
		return;
	}

	if (!strcmp(cmd_parameter, "poweroff")) {
		fastboot_okay("goodbye :(", response);
		psci_system_off();
	} else if (!strcmp(cmd_parameter, "boot_pmos")) {
		fastboot_okay("booting postmarketOS", response);
		tetris_boot_pmos(NULL);
	} else if (!strcmp(cmd_parameter, "boot_pmos_safe")) {
		fastboot_okay("booting postmarketOS with radio modules disabled",
			      response);
		tetris_boot_pmos(env_get("bootargs_safe"));
	} else {
		fastboot_fail("unknown oem_board command", response);
	}
}
#endif /* !TETRIS_CCCI_HANDOFF_HOST_TEST */
