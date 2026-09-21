// SPDX-License-Identifier: GPL-2.0+
/* Host tests for the fail-closed Nothing Tetris SCP inventory validator. */

#define TETRIS_SCP_HANDOFF_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_scp_handoff.c"

#include <stdio.h>
#include <stdlib.h>

#define TEST_FDT_SIZE	16384
#define TEST_PARTITION_SIZE	0x1000000ULL
#define TEST_IMAGE_SIZE		0x400000ULL

static const u32 container_sizes[TETRIS_SCP_CONTAINER_SECTIONS] = {
	0x11c730, 0x6c5, 0x423, 0x924740, 0x6c5, 0x423,
};

struct fixture {
	u8 fdt[TEST_FDT_SIZE];
	struct tetris_scp_range dram[2];
	struct tetris_scp_partition_observation partition;
	struct tetris_scp_region_info_observation region_info;
	int reserved;
	int share;
	int firmware;
};

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

static void set_range(void *fdt, int node, u64 base, u64 size)
{
	fdt32_t reg[4] = {
		cpu_to_fdt32(base >> 32),
		cpu_to_fdt32(base),
		cpu_to_fdt32(size >> 32),
		cpu_to_fdt32(size),
	};

	require(!fdt_setprop(fdt, node, "reg", reg, sizeof(reg)), "set reg");
}

static int add_carveout(void *fdt, int parent, const char *name,
			const char *compatible, u64 base, u64 size)
{
	int node = fdt_add_subnode(fdt, parent, name);

	require(node >= 0, "add carveout");
	require(!fdt_setprop_string(fdt, node, "compatible", compatible),
		"set compatible");
	set_range(fdt, node, base, size);
	return node;
}

static void fixture_init(struct fixture *fixture, enum tetris_scp_slot slot)
{
	size_t i;

	memset(fixture, 0, sizeof(*fixture));
	require(!fdt_create_empty_tree(fixture->fdt, sizeof(fixture->fdt)),
		"create FDT");
	fixture->reserved = fdt_add_subnode(fixture->fdt, 0,
					    "reserved-memory");
	require(fixture->reserved >= 0, "add reserved-memory");
	require(!fdt_setprop_u32(fixture->fdt, fixture->reserved,
				 "#address-cells", 2), "set address cells");
	require(!fdt_setprop_u32(fixture->fdt, fixture->reserved,
				 "#size-cells", 2), "set size cells");
	require(!fdt_setprop(fixture->fdt, fixture->reserved, "ranges", NULL, 0),
		"set ranges");

	fixture->firmware = add_carveout(fixture->fdt, fixture->reserved,
					 "scp-fw", TETRIS_SCP_FW_COMPAT,
					 0xb8000000ULL, 0x2300000ULL);
	fixture->share = add_carveout(fixture->fdt, fixture->reserved,
				      "scp-share", TETRIS_SCP_SHARE_COMPAT,
				      0x8e000000ULL, 0x11c8000ULL);
	fixture->share = fdt_node_offset_by_compatible(fixture->fdt, -1,
						       TETRIS_SCP_SHARE_COMPAT);
	fixture->firmware = fdt_node_offset_by_compatible(fixture->fdt, -1,
							  TETRIS_SCP_FW_COMPAT);

	fixture->dram[0] = (struct tetris_scp_range) {
		.base = 0x40000000ULL,
		.size = 0x50000000ULL,
	};
	fixture->dram[1] = (struct tetris_scp_range) {
		.base = 0xb0000000ULL,
		.size = 0x10000000ULL,
	};
	fixture->partition.active_slot = slot;
	fixture->partition.slot_source = TETRIS_SCP_SLOT_SOURCE_BOOT_CONTROL;
	fixture->partition.slot_verified = true;
	fixture->partition.partition_name = slot == TETRIS_SCP_SLOT_A ?
		"scp_a" : "scp_b";
	fixture->partition.partition_size = TEST_PARTITION_SIZE;
	fixture->partition.image_size = TEST_IMAGE_SIZE;
	fixture->partition.identity_verified = true;
	for (i = 0; i < TETRIS_SCP_IDENTITY_SIZE; i++)
		fixture->partition.image_identity[i] = (u8)(i + 1);

	fixture->region_info.abi_verified = true;
	fixture->region_info.abi_version = 1;
	fixture->region_info.structure_size = 64;
	fixture->region_info.share = (struct tetris_scp_range) {
		.base = 0x8e000000ULL,
		.size = 0x11c8000ULL,
	};
	fixture->region_info.firmware = (struct tetris_scp_range) {
		.base = 0xb8000000ULL,
		.size = 0x2300000ULL,
	};
}

static void expect_failure(struct fixture *fixture,
			   enum tetris_scp_failure expected)
{
	struct tetris_scp_inventory inventory;
	int ret;

	ret = tetris_scp_validate_inventory(fixture->fdt, fixture->dram, 2,
					    &fixture->partition,
					    &fixture->region_info, &inventory);
	require(ret < 0, "invalid observation rejected");
	require(!inventory.valid, "invalid observation not marked valid");
	if (inventory.failure != expected)
		fprintf(stderr, "failure mismatch: expected=%s actual=%s\n",
			tetris_scp_failure_name(expected),
			tetris_scp_failure_name(inventory.failure));
	require(inventory.failure == expected, "expected failure code");
}

static void test_valid_slots_and_read_only_fdt(void)
{
	struct tetris_scp_inventory inventory;
	struct fixture fixture;
	u8 before[TEST_FDT_SIZE];
	int ret;
	enum tetris_scp_slot slot;

	for (slot = TETRIS_SCP_SLOT_A; slot <= TETRIS_SCP_SLOT_B; slot++) {
		fixture_init(&fixture, slot);
		memcpy(before, fixture.fdt, sizeof(before));
		ret = tetris_scp_validate_inventory(fixture.fdt, fixture.dram, 2,
						    &fixture.partition,
						    &fixture.region_info,
						    &inventory);
		require(!ret, "valid inventory");
		require(inventory.valid && inventory.failure == TETRIS_SCP_OK,
			"inventory marked valid");
		require(inventory.active_slot == slot, "active slot retained");
		require(inventory.partition_size == TEST_PARTITION_SIZE,
			"partition size retained");
		require(inventory.image_size == TEST_IMAGE_SIZE,
			"image size retained");
		require(!memcmp(inventory.image_identity,
				fixture.partition.image_identity,
				sizeof(inventory.image_identity)),
			"identity retained");
		require(!memcmp(before, fixture.fdt, sizeof(before)),
			"validator did not mutate FDT");
	}
}

static void test_partition_evidence(void)
{
	struct fixture fixture;

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.slot_verified = false;
	expect_failure(&fixture, TETRIS_SCP_SLOT_UNVERIFIED);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.slot_source = TETRIS_SCP_SLOT_SOURCE_UNKNOWN;
	expect_failure(&fixture, TETRIS_SCP_SLOT_UNVERIFIED);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.slot_source = (enum tetris_scp_slot_source)99;
	expect_failure(&fixture, TETRIS_SCP_SLOT_UNVERIFIED);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.active_slot = TETRIS_SCP_SLOT_UNKNOWN;
	expect_failure(&fixture, TETRIS_SCP_BAD_SLOT);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.partition_name = "scp_b";
	expect_failure(&fixture, TETRIS_SCP_PARTITION_MISMATCH);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.partition_size = 0;
	expect_failure(&fixture, TETRIS_SCP_BAD_PARTITION_SIZE);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.image_size = TEST_PARTITION_SIZE + 1;
	expect_failure(&fixture, TETRIS_SCP_BAD_IMAGE_SIZE);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.partition.identity_verified = false;
	expect_failure(&fixture, TETRIS_SCP_IDENTITY_UNVERIFIED);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	memset(fixture.partition.image_identity, 0,
	       sizeof(fixture.partition.image_identity));
	expect_failure(&fixture, TETRIS_SCP_BAD_IDENTITY);
}

static void test_region_info_evidence(void)
{
	struct fixture fixture;

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.region_info.abi_verified = false;
	expect_failure(&fixture, TETRIS_SCP_REGION_INFO_UNVERIFIED);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.region_info.structure_size = 0;
	expect_failure(&fixture, TETRIS_SCP_BAD_REGION_INFO);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.region_info.share.size++;
	expect_failure(&fixture, TETRIS_SCP_REGION_INFO_MISMATCH);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	fixture.region_info.firmware.base++;
	expect_failure(&fixture, TETRIS_SCP_REGION_INFO_MISMATCH);
}

static void test_carveout_evidence(void)
{
	struct fixture fixture;

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	require(!fdt_del_node(fixture.fdt, fixture.share), "delete share");
	expect_failure(&fixture, TETRIS_SCP_MISSING_SHARE);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	add_carveout(fixture.fdt, fixture.reserved, "duplicate-share",
		     TETRIS_SCP_SHARE_COMPAT, 0x8d000000ULL, 0x11a9b00ULL);
	expect_failure(&fixture, TETRIS_SCP_DUPLICATE_SHARE);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	require(!fdt_setprop_u32(fixture.fdt, fixture.reserved,
				 "#size-cells", 1), "set bad cells");
	expect_failure(&fixture, TETRIS_SCP_BAD_CELLS);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	set_range(fixture.fdt, fixture.share, 0x8e000000ULL,
		  TETRIS_SCP_SHARE_MIN - 1);
	expect_failure(&fixture, TETRIS_SCP_SHARE_TOO_SMALL);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	set_range(fixture.fdt, fixture.share, 0x8f000000ULL,
		  TETRIS_SCP_SHARE_MIN);
	expect_failure(&fixture, TETRIS_SCP_SHARE_OUT_OF_WINDOW);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	set_range(fixture.fdt, fixture.firmware, 0xa0000000ULL, 0x1000ULL);
	expect_failure(&fixture, TETRIS_SCP_NOT_IN_DRAM);

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	set_range(fixture.fdt, fixture.firmware, 0x8e100000ULL, 0x1000ULL);
	expect_failure(&fixture, TETRIS_SCP_CARVEOUT_OVERLAP);
}

static void test_bad_arguments_and_disabled_call(void)
{
	struct tetris_scp_inventory inventory;
	struct fixture fixture;

	fixture_init(&fixture, TETRIS_SCP_SLOT_A);
	require(tetris_scp_validate_inventory(fixture.fdt, fixture.dram, 2,
					      &fixture.partition,
					      &fixture.region_info, NULL) == -EINVAL,
		"null result rejected");
	require(tetris_scp_validate_inventory(NULL, fixture.dram, 2,
					      &fixture.partition,
					      &fixture.region_info,
					      &inventory) == -EINVAL,
		"null FDT rejected");
	require(inventory.failure == TETRIS_SCP_BAD_ARGUMENT,
		"bad argument classified");
	require(tetris_scp_handoff_inventory_disabled() == -EOPNOTSUPP,
		"disabled call site fails closed");
	require(!strcmp(tetris_scp_failure_name(TETRIS_SCP_OK), "ok"),
		"known failure name");
	require(!strcmp(tetris_scp_failure_name(1000), "unknown"),
		"unknown failure name");
}

static void test_region_info_decoder(void)
{
	struct tetris_scp_region_snapshot snapshot;
	u32 words[TETRIS_SCP_REGION_INFO_WORDS] = {
		0x1c400000, 0x20000, 0xb8000000, 0x400000,
		0x8e000000, 0x100000, 0x8e400000,
		TETRIS_SCP_REGION_INFO_SIZE,
	};

	require(!tetris_scp_decode_region_info(words, &snapshot),
		"valid region-info decoded");
	require(snapshot.valid && snapshot.failure == TETRIS_SCP_REGION_OK,
		"valid region-info classified");
	require(snapshot.loader.base == 0x1c400000 &&
		snapshot.firmware.base == 0xb8000000 &&
		snapshot.dram.base == 0x8e000000,
		"region-info ranges retained");

	memset(words, 0, sizeof(words));
	require(tetris_scp_decode_region_info(words, &snapshot) == -ENODATA &&
		snapshot.failure == TETRIS_SCP_REGION_ZERO,
		"zero handoff rejected");

	words[7] = TETRIS_SCP_REGION_INFO_SIZE - 4;
	require(tetris_scp_decode_region_info(words, &snapshot) == -EPROTO &&
		snapshot.failure == TETRIS_SCP_REGION_TOO_SMALL,
		"short handoff rejected");

	words[0] = 0xfffffff0;
	words[1] = 0x100;
	words[2] = 0xb8000000;
	words[3] = 0x400000;
	words[7] = TETRIS_SCP_REGION_INFO_SIZE;
	require(tetris_scp_decode_region_info(words, &snapshot) == -EINVAL &&
		snapshot.failure == TETRIS_SCP_REGION_BAD_LOADER,
		"wrapping loader rejected");

	words[0] = 0x1c400000;
	words[1] = 0x20000;
	words[4] = 0x8e000000;
	words[5] = 0x40000001;
	words[6] = 0x8e400000;
	require(tetris_scp_decode_region_info(words, &snapshot) == -EINVAL &&
		snapshot.failure == TETRIS_SCP_REGION_BAD_DRAM,
		"overflowing recovery span rejected");

	require(tetris_scp_decode_region_info(NULL, &snapshot) == -EINVAL,
		"null words rejected");
	require(tetris_scp_decode_region_info(words, NULL) == -EINVAL,
		"null snapshot rejected");
}

static void init_container_headers(
	u8 headers[TETRIS_SCP_CONTAINER_SECTIONS]
		  [TETRIS_SCP_CONTAINER_HEADER_SIZE])
{
	size_t i;

	memset(headers, 0xff, TETRIS_SCP_CONTAINER_SECTIONS *
	       TETRIS_SCP_CONTAINER_HEADER_SIZE);
	for (i = 0; i < TETRIS_SCP_CONTAINER_SECTIONS; i++) {
		u8 *header = headers[i];

		memcpy(header, (u32[]){ TETRIS_SCP_CONTAINER_MAGIC }, 4);
		memcpy(header + 4, &container_sizes[i], 4);
		memset(header + 8, 0, 32);
		strcpy((char *)header + 8, tetris_scp_container_names[i]);
		memcpy(header + 48,
		       (u32[]){ TETRIS_SCP_CONTAINER_EXT_MAGIC }, 4);
		memcpy(header + 52,
		       (u32[]){ TETRIS_SCP_CONTAINER_HEADER_SIZE }, 4);
	}
}

static void test_scp_container_parser(void)
{
	u8 headers[TETRIS_SCP_CONTAINER_SECTIONS]
		  [TETRIS_SCP_CONTAINER_HEADER_SIZE];
	struct tetris_scp_container container;
	u32 value;

	init_container_headers(headers);
	require(!tetris_scp_parse_container_headers(headers, 0x1000000,
						    &container),
		"valid SCP container accepted");
	require(container.valid &&
		container.failure == TETRIS_SCP_CONTAINER_OK,
		"valid SCP container classified");
	require(container.image_size == 0xa43070,
		"observed SCP container size retained");
	require(container.sections[3].offset == 0x11d830,
		"DRAM section offset retained");

	init_container_headers(headers);
	headers[0][0] = 0;
	require(tetris_scp_parse_container_headers(headers, 0x1000000,
						   &container) == -EINVAL &&
		container.failure == TETRIS_SCP_CONTAINER_BAD_MAGIC,
		"bad container magic rejected");

	init_container_headers(headers);
	strcpy((char *)headers[3] + 8, "tinysys-scp-wrong");
	require(tetris_scp_parse_container_headers(headers, 0x1000000,
						   &container) == -EINVAL &&
		container.failure == TETRIS_SCP_CONTAINER_BAD_NAME,
		"wrong section order rejected");

	init_container_headers(headers);
	value = 0x100;
	memcpy(headers[2] + 52, &value, sizeof(value));
	require(tetris_scp_parse_container_headers(headers, 0x1000000,
						   &container) == -EINVAL &&
		container.failure == TETRIS_SCP_CONTAINER_BAD_HEADER_SIZE,
		"short section header rejected");

	init_container_headers(headers);
	require(tetris_scp_parse_container_headers(headers, 0xa00000,
						   &container) == -EINVAL &&
		container.failure == TETRIS_SCP_CONTAINER_BAD_SIZE,
		"partition overrun rejected");
	require(tetris_scp_parse_container_headers(NULL, 0x1000000,
						   &container) == -EINVAL &&
		container.failure == TETRIS_SCP_CONTAINER_BAD_ARGUMENT,
		"null section headers rejected");
	require(tetris_scp_parse_container_headers(headers, 0x1000000,
						   NULL) == -EINVAL,
		"null container result rejected");
}

static void boot_control_crc(u8 record[32])
{
	u32 crc = crc32(0, record, 28);
	unsigned int i;

	for (i = 0; i < 4; i++)
		record[28 + i] = crc >> (8 * i);
}

static void test_boot_control(void)
{
	u8 record[32] = { '_', 'a', 0, 0, 'B', 'C', 'A', 'B', 1, 2 };
	u8 saved[32];
	enum tetris_scp_slot slot;
	unsigned int i;

	record[12] = 0xff;
	boot_control_crc(record);
	memcpy(saved, record, sizeof(record));
	require(!tetris_scp_decode_boot_control(record, sizeof(record), &slot) &&
		slot == TETRIS_SCP_SLOT_A, "valid recorded slot A");
	require(!memcmp(record, saved, sizeof(record)), "boot control read-only");
	for (i = 0; i < sizeof(record); i++) {
		record[i] ^= 1;
		require(tetris_scp_decode_boot_control(record, sizeof(record), &slot) ==
			-EBADMSG && slot == TETRIS_SCP_SLOT_UNKNOWN,
			"corrupt metadata cannot select a slot");
		record[i] ^= 1;
	}
	record[1] = 'b';
	record[12] = 0;
	record[14] = 0x8f; /* Successful, zero tries: still bootable. */
	boot_control_crc(record);
	require(!tetris_scp_decode_boot_control(record, sizeof(record), &slot) &&
		slot == TETRIS_SCP_SLOT_B, "successful B with no retries");
	record[15] = 1;
	boot_control_crc(record);
	require(tetris_scp_decode_boot_control(record, sizeof(record), &slot) ==
		-ENODATA, "corrupt recorded slot rejected without fallback");
	record[15] = 0;
	record[14] = 0x0f;
	boot_control_crc(record);
	require(tetris_scp_decode_boot_control(record, sizeof(record), &slot) ==
		-ENODATA, "exhausted unsuccessful slot rejected");
	record[14] = 0x8e;
	record[12] = 0xff;
	boot_control_crc(record);
	require(tetris_scp_decode_boot_control(record, sizeof(record), &slot) ==
		-ESTALE, "stale suffix rejected without selecting higher priority");
	record[8] = 2;
	boot_control_crc(record);
	require(tetris_scp_decode_boot_control(record, sizeof(record), &slot) ==
		-EPROTONOSUPPORT, "unknown boot control version rejected");
	require(tetris_scp_decode_boot_control(NULL, 32, &slot) == -EINVAL &&
		slot == TETRIS_SCP_SLOT_UNKNOWN, "missing metadata clears slot");
	require(tetris_scp_decode_boot_control(record, 31, &slot) == -EINVAL,
		"short metadata rejected");
}

int main(void)
{
	test_valid_slots_and_read_only_fdt();
	test_partition_evidence();
	test_region_info_evidence();
	test_carveout_evidence();
	test_bad_arguments_and_disabled_call();
	test_region_info_decoder();
	test_scp_container_parser();
	test_boot_control();
	puts("tetris SCP handoff inventory tests: PASS");
	return 0;
}
