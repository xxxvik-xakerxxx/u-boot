// SPDX-License-Identifier: GPL-2.0+
/* Focused host tests for the Nothing Tetris LK CCCI handoff validator. */

#define TETRIS_CCCI_HANDOFF_HOST_TEST
#include "../../board/mediatek/mt6878/mt6878_tetris.c"

#define TEST_FDT_SIZE	16384
#define TEST_TAG_SIZE	2048
#define TEST_BASE	0x8f000000ULL
#define TEST_MEMORY_BASE	(TEST_BASE - 0x100000ULL)
#define TEST_MEMORY_SIZE	0x400000U
#define TEST_MD_BASE	(TEST_BASE + 0x10000ULL)
#define TEST_MD_SIZE	0x40000U
#define TEST_SMEM_BASE	(TEST_BASE + 0x60000ULL)
#define TEST_SMEM_SIZE	0x20000U
#define TEST_MD_IMAGE_SIZE	0x18000U
#define TEST_MD_MEMORY_SIZE	0x30000U
#define TEST_MD_TYPE	10U

struct fixture {
	u8 fdt[TEST_FDT_SIZE];
	u8 tags[TEST_TAG_SIZE];
	u64 base;
	u32 descriptor_size;
	u32 header_size;
	u32 name_size;
	u32 tag_count;
	bool allow_range;
	int map_calls;
	int unmap_calls;
};

struct tag_fixture {
	const char *name;
	u32 size;
};

static const struct tag_fixture v2_tags[] = {
	{ "hdr_count", 4 },
	{ "hdr_tbl_inf", 24 },
	{ "md_mem_layout", 48 },
	{ "md1_chk", 512 },
	{ "md1img", 4 },
	{ "smem_layout", 40 },
	{ "nc_smem_info_ext", 16 },
	{ "md1_phy_cap", 4 },
	{ "nc_smem_layout_num", 4 },
	{ "c_smem_layout_num", 4 },
	{ "nc_smem_layout", 80 },
	{ "c_smem_layout", 40 },
};

static const struct tag_fixture legacy_tags[] = {
	{ "hdr_count", 4 },
	{ "hdr_tbl_inf", 24 },
	{ "md_mem_layout", 48 },
	{ "md1_chk", 188 },
	{ "md1img", 4 },
	{ "smem_layout", 40 },
};

static void put_le32(void *ptr, u32 value)
{
	u8 *p = ptr;

	p[0] = value;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

static void put_le64(void *ptr, u64 value)
{
	put_le32(ptr, value);
	put_le32((u8 *)ptr + 4, value >> 32);
}

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

static int modem_node(const struct fixture *fixture)
{
	return fdt_node_offset_by_compatible(fixture->fdt, -1,
					     "mediatek,mddriver");
}

static int memory_node(const struct fixture *fixture)
{
	return fdt_path_offset(fixture->fdt, "/memory");
}

static void fixture_set_descriptor(struct fixture *fixture, bool v2,
				   u64 base, u32 size, u32 count, u32 version)
{
	u8 raw[TETRIS_CCCI_V2_DESC_SIZE] = { 0 };
	int ret;

	put_le64(raw, base);
	put_le32(raw + 8, size);
	if (v2) {
		put_le32(raw + 16, version);
		put_le32(raw + 20, count);
		ret = fdt_setprop(fixture->fdt, modem_node(fixture),
				  "ccci,modem_info_v2", raw,
				  TETRIS_CCCI_V2_DESC_SIZE);
	} else {
		put_le32(raw + 12, count);
		ret = fdt_setprop(fixture->fdt, modem_node(fixture),
				  "ccci,modem_info", raw,
				  TETRIS_CCCI_V1_DESC_SIZE);
	}
	require(!ret, "set descriptor");
}

static void fixture_build_tags(struct fixture *fixture,
			       const struct tag_fixture *tags, u32 count)
{
	u32 data_offset = 1024;
	unsigned int i;

	memset(fixture->tags, 0, sizeof(fixture->tags));
	fixture->tag_count = count;
	for (i = 0; i < count; i++) {
		u8 *header = fixture->tags + i * fixture->header_size;

		require(strlen(tags[i].name) < fixture->name_size,
			"fixture tag name fits");
		memcpy(header, tags[i].name, strlen(tags[i].name) + 1);
		put_le32(header + fixture->name_size, data_offset);
		put_le32(header + fixture->name_size + 4, tags[i].size);
		put_le32(header + fixture->name_size + 8,
			 i + 1 == count ? 0 : (i + 1) * fixture->header_size);
		data_offset += tags[i].size;
	}
}

static u8 *fixture_tag_data(struct fixture *fixture, const char *name,
			    u32 *sizep)
{
	u32 offset = 0;
	unsigned int i;

	for (i = 0; i < fixture->tag_count; i++) {
		u8 *header = fixture->tags + offset;
		u32 data_offset;

		if (!strcmp((const char *)header, name)) {
			data_offset = get_unaligned_le32(header + fixture->name_size);
			if (sizep)
				*sizep = get_unaligned_le32(header +
							 fixture->name_size + 4);
			return fixture->tags + data_offset;
		}
		offset = get_unaligned_le32(header + fixture->name_size + 8);
	}

	return NULL;
}

static u8 *fixture_tag_header(struct fixture *fixture, const char *name)
{
	u32 offset = 0;
	unsigned int i;

	for (i = 0; i < fixture->tag_count; i++) {
		u8 *header = fixture->tags + offset;

		if (!strcmp((const char *)header, name))
			return header;
		offset = get_unaligned_le32(header + fixture->name_size + 8);
	}

	return NULL;
}

static void fixture_set_v2_descriptor_word(struct fixture *fixture,
					   u32 offset, u32 value)
{
	const u8 *property;
	u8 raw[TETRIS_CCCI_V2_DESC_SIZE];
	int len;

	property = fdt_getprop(fixture->fdt, modem_node(fixture),
			       "ccci,modem_info_v2", &len);
	require(property && len == sizeof(raw), "get v2 descriptor");
	memcpy(raw, property, sizeof(raw));
	put_le32(raw + offset, value);
	require(!fdt_setprop(fixture->fdt, modem_node(fixture),
			     "ccci,modem_info_v2", raw, sizeof(raw)),
		"replace v2 descriptor");
}

static void fixture_put_md_block(u8 *entry, u32 offset, u32 size)
{
	put_le32(entry, offset);
	put_le32(entry + 4, size);
	put_le64(entry + 16, TEST_MD_BASE + offset);
}

static void fixture_put_smem_region(u8 *entry, u64 base, u32 id, u32 offset,
				    u32 size)
{
	put_le64(entry, base);
	put_le32(entry + 16, id);
	put_le32(entry + 20, offset);
	put_le32(entry + 24, size);
	put_le32(entry + 28, 0x1000);
	put_le32(entry + 36, offset);
}

static void fixture_build_payload(struct fixture *fixture)
{
	u8 *data;
	u32 size;

	data = fixture_tag_data(fixture, "hdr_count", NULL);
	require(data, "hdr_count payload exists");
	put_le32(data, 1);

	data = fixture_tag_data(fixture, "hdr_tbl_inf", NULL);
	require(data, "hdr_tbl_inf payload exists");
	put_le64(data, TEST_MD_BASE);
	put_le32(data + 8, TEST_MD_SIZE);
	data[12] = 0;
	data[13] = 0;
	data[14] = TEST_MD_TYPE;
	data[15] = 1;

	data = fixture_tag_data(fixture, "md_mem_layout", &size);
	require(data && (size == 24 || size == 48), "md layout payload exists");
	fixture_put_md_block(data, 0, 0x18000);
	if (size == 48)
		fixture_put_md_block(data + 24, 0x20000, 0x10000);

	data = fixture_tag_data(fixture, "md1_chk", &size);
	require(data, "check header payload exists");
	memcpy(data, TETRIS_CCCI_MD_CHECK_MAGIC,
	       TETRIS_CCCI_MD_CHECK_MAGIC_SIZE);
	put_le32(data + 12, size == TETRIS_CCCI_MD_CHECK_V2_SIZE ? 2 :
		 size == TETRIS_CCCI_MD_CHECK_V5_SIZE ? 5 : 6);
	put_le32(data + 16, 2);
	put_le32(data + 20, TEST_MD_TYPE);
	data[168] = 1;
	put_le32(data + 172, TEST_MD_MEMORY_SIZE);
	put_le32(data + 176, TEST_MD_IMAGE_SIZE);
	put_le32(data + size - 4, size);

	data = fixture_tag_data(fixture, "md1img", NULL);
	require(data, "md1img payload exists");
	put_le32(data, TEST_MD_IMAGE_SIZE);

	data = fixture_tag_data(fixture, "smem_layout", NULL);
	require(data, "smem layout payload exists");
	put_le64(data, TEST_SMEM_BASE);
	put_le32(data + 8, 0);
	put_le32(data + 12, 0x10000);
	put_le32(data + 16, 0x10000);
	put_le32(data + 20, 0x4000);
	put_le32(data + 24, 0x14000);
	put_le32(data + 28, 0x4000);
	put_le32(data + 36, TEST_SMEM_SIZE);

	data = fixture_tag_data(fixture, "nc_smem_layout_num", NULL);
	if (!data)
		return;
	put_le32(data, 2);
	data = fixture_tag_data(fixture, "c_smem_layout_num", NULL);
	require(data, "cache layout count exists");
	put_le32(data, 1);
	data = fixture_tag_data(fixture, "nc_smem_layout", NULL);
	require(data, "non-cache layout exists");
	fixture_put_smem_region(data, TEST_SMEM_BASE, 1, 0, 0x4000);
	fixture_put_smem_region(data + 40, TEST_SMEM_BASE + 0x4000, 2,
				0x4000, 0x4000);
	data = fixture_tag_data(fixture, "c_smem_layout", NULL);
	require(data, "cache layout exists");
	fixture_put_smem_region(data, TEST_SMEM_BASE + 0x10000, 3,
				0x10000, 0x4000);
}

static void fixture_init(struct fixture *fixture, bool v2, bool use_rmem)
{
	int node, parent, ret;
	fdt32_t memory_reg[4];

	memset(fixture, 0, sizeof(*fixture));
	fixture->base = TEST_BASE;
	fixture->allow_range = true;
	fixture->descriptor_size = TEST_TAG_SIZE;
	fixture->header_size = v2 ? TETRIS_CCCI_V2_TAG_SIZE :
		TETRIS_CCCI_V1_TAG_SIZE;
	fixture->name_size = v2 ? TETRIS_CCCI_V2_NAME_SIZE :
		TETRIS_CCCI_V1_NAME_SIZE;
	require(!fdt_create_empty_tree(fixture->fdt, sizeof(fixture->fdt)),
		"create FDT");
	require(!fdt_setprop_u32(fixture->fdt, 0, "#address-cells", 2),
		"set root address cells");
	require(!fdt_setprop_u32(fixture->fdt, 0, "#size-cells", 2),
		"set root size cells");
	node = fdt_add_subnode(fixture->fdt, 0, "memory");
	require(node >= 0, "add memory node");
	require(!fdt_setprop_string(fixture->fdt, node, "device_type", "memory"),
		"set memory device type");
	memory_reg[0] = cpu_to_fdt32(0);
	memory_reg[1] = cpu_to_fdt32(TEST_MEMORY_BASE);
	memory_reg[2] = cpu_to_fdt32(0);
	memory_reg[3] = cpu_to_fdt32(TEST_MEMORY_SIZE);
	require(!fdt_setprop(fixture->fdt, node, "reg", memory_reg,
			     sizeof(memory_reg)), "set memory range");
	require(fdt_add_subnode(fixture->fdt, 0, "chosen") >= 0,
		"add chosen");
	node = fdt_add_subnode(fixture->fdt, 0, "modem");
	require(node >= 0, "add modem node");
	require(!fdt_setprop_string(fixture->fdt, node, "compatible",
				    "mediatek,mddriver"),
		"set modem compatible");

	if (use_rmem) {
		fdt32_t reg[4];

		parent = fdt_add_subnode(fixture->fdt, 0, "reserved-memory");
		require(parent >= 0, "add reserved-memory");
		require(!fdt_setprop_u32(fixture->fdt, parent,
					 "#address-cells", 2),
			"set address cells");
		require(!fdt_setprop_u32(fixture->fdt, parent,
					 "#size-cells", 2),
			"set size cells");
		require(!fdt_setprop(fixture->fdt, parent, "ranges", NULL, 0),
			"set ranges");
		node = fdt_add_subnode(fixture->fdt, parent, "lk-tags");
		require(node >= 0, "add LK tag reservation");
		reg[0] = cpu_to_fdt32(TEST_MEMORY_BASE >> 32);
		reg[1] = cpu_to_fdt32(TEST_MEMORY_BASE);
		reg[2] = cpu_to_fdt32(0);
		reg[3] = cpu_to_fdt32(TEST_MEMORY_SIZE);
		require(!fdt_setprop(fixture->fdt, node, "reg", reg, sizeof(reg)),
			"set LK tag reservation");
	} else {
		ret = fdt_add_mem_rsv(fixture->fdt, TEST_MEMORY_BASE,
				      TEST_MEMORY_SIZE);
		require(!ret, "add memreserve");
	}

	if (v2) {
		fixture_build_tags(fixture, v2_tags, ARRAY_SIZE(v2_tags));
		fixture_build_payload(fixture);
		fixture_set_descriptor(fixture, true, fixture->base,
				       fixture->descriptor_size,
				       ARRAY_SIZE(v2_tags), 2);
	} else {
		fixture_build_tags(fixture, legacy_tags,
				   ARRAY_SIZE(legacy_tags));
		fixture_build_payload(fixture);
		fixture_set_descriptor(fixture, false, fixture->base,
				       fixture->descriptor_size,
				       ARRAY_SIZE(legacy_tags), 1);
	}
}

static const void *fixture_map(u64 base, u32 size, void *context)
{
	struct fixture *fixture = context;

	fixture->map_calls++;
	if (base != fixture->base || size != fixture->descriptor_size)
		return NULL;
	return fixture->tags;
}

static bool fixture_range_allowed(u64 base, u32 size, void *context)
{
	struct fixture *fixture = context;

	return fixture->allow_range &&
	       tetris_ccci_range_contains(TEST_MEMORY_BASE, TEST_MEMORY_SIZE,
					  base, size);
}

static void fixture_unmap(const void *buffer, void *context)
{
	struct fixture *fixture = context;

	require(buffer == fixture->tags, "unmap original tag buffer");
	fixture->unmap_calls++;
}

static enum tetris_ccci_failure observe(struct fixture *fixture, int *retp)
{
	struct tetris_ccci_access access = {
		.range_allowed = fixture_range_allowed,
		.map = fixture_map,
		.unmap = fixture_unmap,
		.context = fixture,
	};
	struct tetris_ccci_result result;

	*retp = tetris_ccci_observe(fixture->fdt, &access, &result);
	return result.failure;
}

static void expect_failure(struct fixture *fixture,
			   enum tetris_ccci_failure expected,
			   bool expect_map, const char *message)
{
	int ret;

	require(observe(fixture, &ret) == expected, message);
	require(ret < 0, "failure returns an error");
	require(!!fixture->map_calls == expect_map,
		"mapper invocation matches containment stage");
	require(!!fixture->unmap_calls == expect_map,
		"mapped tag buffer is always unmapped");
}

static void test_valid(bool v2, bool use_rmem)
{
	struct tetris_ccci_result result;
	struct fixture fixture;
	const fdt32_t *value;
	const char *text;
	int len, node, ret;
	const struct tetris_ccci_access access = {
		.range_allowed = fixture_range_allowed,
		.map = fixture_map,
		.unmap = fixture_unmap,
		.context = &fixture,
	};

	fixture_init(&fixture, v2, use_rmem);
	ret = tetris_ccci_observe(fixture.fdt, &access, &result);
	require(!ret && result.failure == TETRIS_CCCI_OK, "valid handoff");
	require(result.structure_valid && result.payload_valid,
		"valid handoff validates structure and payload");
	require(fixture.map_calls == 1 && fixture.unmap_calls == 1,
		"valid handoff maps exactly once");
	require(!tetris_ccci_publish_status(fixture.fdt, &result),
		"publish valid status");
	node = fdt_path_offset(fixture.fdt,
			       "/chosen/nothing,ccci-handoff-status");
	require(node >= 0, "status node exists");
	text = fdt_getprop(fixture.fdt, node, "observation-status", &len);
	require(text && !strcmp(text, "structure-valid"),
		"structure status is sanitized");
	text = fdt_getprop(fixture.fdt, node, "payload-status", &len);
	require(text && !strcmp(text, "valid"),
		"payload status is sanitized");
	value = fdt_getprop(fixture.fdt, node, "count", &len);
	require(value && len == sizeof(*value) &&
		fdt32_to_cpu(*value) == fixture.tag_count,
		"sanitized tag count");
	require(fdt_getprop(fixture.fdt, node, "structure-validated", &len) &&
		!len, "structure marker present");
	require(fdt_getprop(fixture.fdt, node, "payload-validated", &len) &&
		!len, "payload marker present");
	value = fdt_getprop(fixture.fdt, node, "md-memory-size", &len);
	require(value && len == sizeof(*value) &&
		fdt32_to_cpu(*value) == TEST_MD_MEMORY_SIZE,
		"sanitized modem memory size");
	value = fdt_getprop(fixture.fdt, node, "smem-total-size", &len);
	require(value && len == sizeof(*value) &&
		fdt32_to_cpu(*value) == TEST_SMEM_SIZE,
		"sanitized shared-memory size");
	require(!fdt_getprop(fixture.fdt, node, "runtime-ready", &len),
		"zero payload never claims runtime readiness");
	require(!fdt_getprop(fixture.fdt, node, "base", &len),
		"physical base is never published");
	require(!fdt_getprop(fixture.fdt, node, "md-base", &len),
		"modem physical base is never published");
	require(!fdt_getprop(fixture.fdt, node, "smem-base", &len),
		"shared-memory physical base is never published");
}

static void test_valid_v5(void)
{
	struct fixture fixture;
	u8 *data, *header;
	int ret;

	fixture_init(&fixture, true, false);
	header = fixture_tag_header(&fixture, "md1_chk");
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	require(header && data, "v5 check tag exists");
	put_le32(header + fixture.name_size + 4,
		 TETRIS_CCCI_MD_CHECK_V5_SIZE);
	put_le32(data + 12, 5);
	put_le32(data + TETRIS_CCCI_MD_CHECK_V5_SIZE - 4,
		 TETRIS_CCCI_MD_CHECK_V5_SIZE);
	require(observe(&fixture, &ret) == TETRIS_CCCI_OK && !ret,
		"B4.1 v5 check header payload");
}

static void test_valid_v3_descriptor(void)
{
	struct fixture fixture;
	int ret;

	fixture_init(&fixture, true, false);
	fixture_set_v2_descriptor_word(&fixture, 16, 3);
	require(observe(&fixture, &ret) == TETRIS_CCCI_OK && !ret,
		"version 3 descriptor uses version 2 tag headers");
}

static void test_descriptor_failures(void)
{
	const struct tetris_ccci_access access = { 0 };
	struct tetris_ccci_result result;
	struct fixture fixture;
	fdt32_t memory_reg[4];
	u8 raw[TETRIS_CCCI_V2_DESC_SIZE] = { 0 };
	int ret;

	ret = tetris_ccci_observe(NULL, &access, &result);
	require(ret < 0 && result.failure == TETRIS_CCCI_NO_FDT,
		"missing FDT fails closed");

	fixture_init(&fixture, true, false);
	require(!fdt_delprop(fixture.fdt, modem_node(&fixture),
			     "ccci,modem_info_v2"), "remove descriptor");
	expect_failure(&fixture, TETRIS_CCCI_NO_DESCRIPTOR, false,
		       "missing descriptor");

	fixture_init(&fixture, true, false);
	require(!fdt_setprop(fixture.fdt, modem_node(&fixture),
			     "ccci,modem_info_v2", raw, sizeof(raw) - 1),
		"set short descriptor");
	expect_failure(&fixture, TETRIS_CCCI_BAD_DESCRIPTOR_SIZE, false,
		       "short descriptor");

	fixture_init(&fixture, true, false);
	fixture_set_descriptor(&fixture, true, fixture.base, 0,
			       fixture.tag_count, 2);
	expect_failure(&fixture, TETRIS_CCCI_BAD_DESCRIPTOR, false,
		       "zero buffer size");

	fixture_init(&fixture, true, false);
	fixture_set_descriptor(&fixture, true, fixture.base,
			       TETRIS_CCCI_MAX_INFO_SIZE + 1,
			       fixture.tag_count, 2);
	expect_failure(&fixture, TETRIS_CCCI_BAD_DESCRIPTOR, false,
		       "oversized buffer");

	fixture_init(&fixture, true, false);
	fixture_set_descriptor(&fixture, true, UINT64_MAX - 15, 32,
			       fixture.tag_count, 2);
	expect_failure(&fixture, TETRIS_CCCI_RANGE_OVERFLOW, false,
		       "overflowing physical range");

	fixture_init(&fixture, true, false);
	fixture_set_v2_descriptor_word(&fixture, 12, 1);
	expect_failure(&fixture, TETRIS_CCCI_BAD_DESCRIPTOR_STATUS, false,
		       "LK descriptor error rejects payload before map");

	fixture_init(&fixture, true, false);
	fixture_set_v2_descriptor_word(&fixture, 28, 1);
	expect_failure(&fixture, TETRIS_CCCI_BAD_DESCRIPTOR_STATUS, false,
		       "modem load error rejects payload before map");

	fixture_init(&fixture, true, false);
	fixture_set_descriptor(&fixture, true, fixture.base,
			       fixture.descriptor_size, fixture.tag_count, 4);
	expect_failure(&fixture, TETRIS_CCCI_UNSUPPORTED_DESCRIPTOR_VERSION,
		       false, "unsupported descriptor version");

	fixture_init(&fixture, true, false);
	require(!fdt_del_mem_rsv(fixture.fdt, 0), "remove memreserve");
	expect_failure(&fixture, TETRIS_CCCI_NOT_RESERVED, false,
		       "unreserved buffer rejected before map");

	fixture_init(&fixture, true, false);
	memory_reg[0] = cpu_to_fdt32(0);
	memory_reg[1] = cpu_to_fdt32(TEST_BASE + 0x100000);
	memory_reg[2] = cpu_to_fdt32(0);
	memory_reg[3] = cpu_to_fdt32(0x100000);
	require(!fdt_setprop(fixture.fdt, memory_node(&fixture), "reg",
			     memory_reg, sizeof(memory_reg)),
		"move LK memory range");
	expect_failure(&fixture, TETRIS_CCCI_NOT_DRAM, false,
		       "reserved range outside LK memory rejected before map");

	fixture_init(&fixture, true, false);
	fixture.allow_range = false;
	expect_failure(&fixture, TETRIS_CCCI_NOT_DRAM, false,
		       "range outside U-Boot DRAM rejected before map");
}

static void test_invalid_status(void)
{
	const struct tetris_ccci_access access = {
		.range_allowed = fixture_range_allowed,
		.map = fixture_map,
		.unmap = fixture_unmap,
	};
	struct tetris_ccci_result result;
	struct fixture fixture;
	const char *text;
	int len, node;

	fixture_init(&fixture, true, false);
	require(!fdt_delprop(fixture.fdt, modem_node(&fixture),
			     "ccci,modem_info_v2"), "remove invalid descriptor");
	require(tetris_ccci_observe(fixture.fdt, &access, &result) < 0,
		"invalid observation fails closed");
	require(!tetris_ccci_publish_status(fixture.fdt, &result),
		"publish invalid status");
	node = fdt_path_offset(fixture.fdt,
			       "/chosen/nothing,ccci-handoff-status");
	require(node >= 0, "invalid status node exists");
	text = fdt_getprop(fixture.fdt, node, "observation-status", &len);
	require(text && !strcmp(text, "invalid"), "invalid status published");
	text = fdt_getprop(fixture.fdt, node, "failure", &len);
	require(text && !strcmp(text, "no-descriptor"),
		"invalid status has sanitized failure");
	require(!fdt_getprop(fixture.fdt, node, "structure-validated", &len),
		"invalid status omits structure marker");
	require(!fdt_getprop(fixture.fdt, node, "base", &len),
		"invalid status omits physical base");
	text = fdt_getprop(fixture.fdt, node, "payload-status", &len);
	require(text && !strcmp(text, "not-checked"),
		"structural failure does not claim payload observation");
}

static void test_payload_failures(void)
{
	struct tetris_ccci_result result;
	struct tetris_ccci_access access = {
		.range_allowed = fixture_range_allowed,
		.map = fixture_map,
		.unmap = fixture_unmap,
	};
	struct fixture fixture;
	u8 *data, *header;
	const char *text;
	int len, node;

	fixture_init(&fixture, true, false);
	header = fixture_tag_header(&fixture, "md1img");
	require(header, "required tag header exists");
	memset(header, 0, fixture.name_size);
	memcpy(header, "optional", sizeof("optional"));
	expect_failure(&fixture, TETRIS_CCCI_MISSING_PAYLOAD_TAG, true,
		       "missing required payload tag");

	fixture_init(&fixture, true, false);
	put_le32(fixture_tag_data(&fixture, "hdr_count", NULL), 2);
	expect_failure(&fixture, TETRIS_CCCI_BAD_HDR_COUNT, true,
		       "exactly one modem header is required");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "hdr_tbl_inf", NULL);
	data[13] = 1;
	expect_failure(&fixture, TETRIS_CCCI_BAD_MODEM_INFO, true,
		       "modem header errno rejected");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "hdr_tbl_inf", NULL);
	put_le64(data, TEST_MEMORY_BASE + TEST_MEMORY_SIZE);
	expect_failure(&fixture, TETRIS_CCCI_BAD_MD_RANGE, true,
		       "unreserved modem range rejected without mapping it");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md_mem_layout", NULL) + 24;
	put_le32(data, 0x10000);
	put_le64(data + 16, TEST_MD_BASE + 0x10000);
	expect_failure(&fixture, TETRIS_CCCI_BAD_MD_LAYOUT, true,
		       "overlapping modem layout blocks rejected");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	data[0] = 'X';
	expect_failure(&fixture, TETRIS_CCCI_BAD_CHECK_HEADER, true,
		       "check-header magic rejected");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	data[168] = 2;
	expect_failure(&fixture, TETRIS_CCCI_BAD_CHECK_HEADER, true,
		       "check header must bind MD SYS1");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	put_le32(data + 12, 5);
	expect_failure(&fixture, TETRIS_CCCI_BAD_CHECK_HEADER, true,
		       "check-header version must match its exact layout");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	put_le32(data + TETRIS_CCCI_MD_CHECK_V6_SIZE - 4,
		 TETRIS_CCCI_MD_CHECK_V6_SIZE - 4);
	expect_failure(&fixture, TETRIS_CCCI_BAD_CHECK_HEADER, true,
		       "check-header declared size must match tag size");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "hdr_tbl_inf", NULL);
	data[14] = TEST_MD_TYPE - 1;
	expect_failure(&fixture, TETRIS_CCCI_BAD_CHECK_HEADER, true,
		       "modem type must match check-header image type");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	put_le32(data + 172, TEST_MD_SIZE + 1);
	expect_failure(&fixture, TETRIS_CCCI_BAD_IMAGE_SIZE, true,
		       "check-header memory exceeds reserved modem bank");

	fixture_init(&fixture, true, false);
	put_le32(fixture_tag_data(&fixture, "md1img", NULL),
		 TEST_MD_IMAGE_SIZE - 1);
	expect_failure(&fixture, TETRIS_CCCI_BAD_IMAGE_SIZE, true,
		       "raw image size cannot underflow check header");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "smem_layout", NULL);
	put_le32(data + 36, 0x8000);
	expect_failure(&fixture, TETRIS_CCCI_BAD_SMEM_LAYOUT, true,
		       "shared-memory subrange must fit total size");

	fixture_init(&fixture, true, false);
	header = fixture_tag_header(&fixture, "c_smem_layout");
	require(header, "direct cache layout tag exists");
	memset(header, 0, fixture.name_size);
	memcpy(header, "optional", sizeof("optional"));
	expect_failure(&fixture, TETRIS_CCCI_BAD_SMEM_TAG_SET, true,
		       "direct SMEM tags are all-or-none");

	fixture_init(&fixture, true, false);
	put_le32(fixture_tag_data(&fixture, "nc_smem_layout_num", NULL), 3);
	expect_failure(&fixture, TETRIS_CCCI_BAD_SMEM_TABLE, true,
		       "direct SMEM count must match table length");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "c_smem_layout", NULL);
	put_le64(data, TEST_MEMORY_BASE + TEST_MEMORY_SIZE);
	expect_failure(&fixture, TETRIS_CCCI_BAD_SMEM_TABLE, true,
		       "direct SMEM physical range must be reserved");

	fixture_init(&fixture, true, false);
	data = fixture_tag_data(&fixture, "md1_chk", NULL);
	data[0] = 'X';
	access.context = &fixture;
	require(tetris_ccci_observe(fixture.fdt, &access, &result) < 0,
		"payload failure is observable");
	require(result.structure_valid && !result.payload_valid,
		"payload failure preserves structural evidence");
	require(!tetris_ccci_publish_status(fixture.fdt, &result),
		"publish payload failure");
	node = fdt_path_offset(fixture.fdt,
			       "/chosen/nothing,ccci-handoff-status");
	text = fdt_getprop(fixture.fdt, node, "observation-status", &len);
	require(text && !strcmp(text, "structure-valid"),
		"payload failure retains structure-valid status");
	text = fdt_getprop(fixture.fdt, node, "payload-status", &len);
	require(text && !strcmp(text, "invalid"),
		"payload failure has explicit invalid status");
	text = fdt_getprop(fixture.fdt, node, "payload-failure", &len);
	require(text && !strcmp(text, "bad-check-header"),
		"payload failure has sanitized reason");
	require(fdt_getprop(fixture.fdt, node, "structure-validated", &len) &&
		!len, "payload failure retains structure marker");
	require(!fdt_getprop(fixture.fdt, node, "payload-validated", &len),
		"payload failure omits payload marker");
}

static void test_publish_no_space(void)
{
	struct fixture fixture;
	struct tetris_ccci_result result;
	const struct tetris_ccci_access access = {
		.range_allowed = fixture_range_allowed,
		.map = fixture_map,
		.unmap = fixture_unmap,
		.context = &fixture,
	};
	int ret;

	fixture_init(&fixture, true, false);
	require(!tetris_ccci_observe(fixture.fdt, &access, &result),
		"observe before packed-FDT test");
	require(!fdt_pack(fixture.fdt), "pack FDT without free space");
	ret = tetris_ccci_publish_status(fixture.fdt, &result);
	require(ret == -FDT_ERR_NOSPACE, "packed FDT rejects status publication");
	require(fdt_path_offset(fixture.fdt,
				"/chosen/nothing,ccci-handoff-status") ==
		-FDT_ERR_NOTFOUND, "failed publication leaves no partial node");
}

static void test_tag_failures(void)
{
	struct fixture fixture;
	u8 *header;
	int ret;

	fixture_init(&fixture, true, false);
	header = fixture.tags;
	put_le32(header + fixture.name_size, fixture.descriptor_size - 1);
	put_le32(header + fixture.name_size + 4, 4);
	expect_failure(&fixture, TETRIS_CCCI_BAD_TAG_DATA, true,
		       "out-of-range tag data");

	fixture_init(&fixture, true, false);
	header = fixture.tags;
	put_le32(header + fixture.name_size + 8,
		 fixture.descriptor_size - 1);
	expect_failure(&fixture, TETRIS_CCCI_BAD_TAG_HEADER, true,
		       "out-of-range next header");

	fixture_init(&fixture, true, false);
	header = fixture.tags + fixture.header_size * 2;
	put_le32(header + fixture.name_size + 8, fixture.header_size);
	fixture_set_descriptor(&fixture, true, fixture.base,
			       fixture.descriptor_size, 4, 2);
	expect_failure(&fixture, TETRIS_CCCI_TAG_CYCLE, true,
		       "tag cycle");

	fixture_init(&fixture, true, false);
	fixture_set_descriptor(&fixture, true, fixture.base,
			       fixture.descriptor_size,
			       fixture.tag_count - 1, 2);
	expect_failure(&fixture, TETRIS_CCCI_TAG_COUNT_MISMATCH, true,
		       "declared count must terminate chain");

	fixture_init(&fixture, true, false);
	header = fixture.tags + fixture.header_size * 7;
	memset(header, 0, fixture.name_size);
	memcpy(header, "optional", sizeof("optional"));
	require(observe(&fixture, &ret) == TETRIS_CCCI_OK && !ret,
		"optional tag may be absent from a structure-valid handoff");

	fixture_init(&fixture, true, false);
	header = fixture.tags;
	memset(header, 'x', fixture.name_size);
	expect_failure(&fixture, TETRIS_CCCI_BAD_TAG_NAME, true,
		       "unterminated tag name");
}

static void test_gnss_emi_handoff(void)
{
	fdt32_t zero = 0;
	struct fixture fixture;
	const fdt32_t *cells;
	int gps, len;

	fixture_init(&fixture, true, false);
	gps = fdt_add_subnode(fixture.fdt, 0, "gps");
	require(gps >= 0, "add GPS node");
	require(!fdt_setprop_string(fixture.fdt, gps, "compatible",
				    "mediatek,mt6878-gps"),
		"set GPS compatible");
	require(!fdt_setprop(fixture.fdt, gps, "emi-addr", &zero,
			     sizeof(zero)),
		"set vendor one-cell GPS placeholder");

	require(!tetris_publish_gps_emi_addr(fixture.fdt, gps),
		"publish fixed GPS EMI address");
	cells = fdt_getprop(fixture.fdt, gps, "emi-addr", &len);
	require(cells && len == 2 * sizeof(*cells),
		"GPS EMI address is exactly two FDT cells");
	require(fdt32_to_cpu(cells[0]) == (TETRIS_GPS_EMI_BASE >> 32) &&
		fdt32_to_cpu(cells[1]) == (u32)TETRIS_GPS_EMI_BASE,
		"GPS EMI cells encode the board constant");
}

int main(void)
{
	test_valid(true, false);
	test_valid(true, true);
	test_valid(false, false);
	test_valid_v5();
	test_valid_v3_descriptor();
	test_descriptor_failures();
	test_invalid_status();
	test_payload_failures();
	test_publish_no_space();
	test_tag_failures();
	test_gnss_emi_handoff();
	puts("Tetris CCCI handoff host tests: PASS");
	return 0;
}
