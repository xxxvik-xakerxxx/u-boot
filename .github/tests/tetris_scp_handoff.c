// SPDX-License-Identifier: GPL-2.0+
/* Host tests for the fail-closed Nothing Tetris SCP inventory validator. */

#define TETRIS_SCP_HANDOFF_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_scp_handoff.c"

#include <stdio.h>
#include <stdlib.h>

#define TEST_FDT_SIZE	16384
#define TEST_PARTITION_SIZE	0x1000000ULL
#define TEST_IMAGE_SIZE		0x400000ULL

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

int main(void)
{
	test_valid_slots_and_read_only_fdt();
	test_partition_evidence();
	test_region_info_evidence();
	test_carveout_evidence();
	test_bad_arguments_and_disabled_call();
	puts("tetris SCP handoff inventory tests: PASS");
	return 0;
}
