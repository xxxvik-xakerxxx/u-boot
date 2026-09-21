// SPDX-License-Identifier: GPL-2.0+

#ifdef TETRIS_SCP_HANDOFF_HOST_TEST
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <libfdt.h>
#include <zlib.h>
#else
#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <u-boot/crc.h>
#endif

#include "tetris_scp_handoff.h"

#define TETRIS_SCP_SHARE_COMPAT	"mediatek,reserve-memory-scp_share"
#define TETRIS_SCP_FW_COMPAT	"mediatek,SCP-reserved"
#define TETRIS_SCP_SHARE_MIN	0x11a9b00ULL
#define TETRIS_SCP_SHARE_LIMIT	0x90000000ULL
#define TETRIS_SCP_CONTAINER_MAGIC	0x58881688U
#define TETRIS_SCP_CONTAINER_EXT_MAGIC	0x58891689U

int tetris_scp_decode_boot_control(const u8 *data, size_t size,
				   enum tetris_scp_slot *slot)
{
	u32 stored_crc;
	u8 metadata, other;
	unsigned int index;

	if (!slot)
		return -EINVAL;
	*slot = TETRIS_SCP_SLOT_UNKNOWN;
	if (!data || size != TETRIS_SCP_BOOT_CONTROL_SIZE)
		return -EINVAL;
	stored_crc = (u32)data[28] | (u32)data[29] << 8 |
		     (u32)data[30] << 16 | (u32)data[31] << 24;
	if ((u32)crc32(0, data, 28) != stored_crc)
		return -EBADMSG;
	if (memcmp(data + 4, "BCAB", 4) || data[8] != 1 ||
	    (data[9] & 7) != 2)
		return -EPROTONOSUPPORT;
	if (data[0] != '_' || (data[1] != 'a' && data[1] != 'b') ||
	    data[2] || data[3])
		return -ENODATA;
	index = data[1] - 'a';
	metadata = data[12 + index * 2];
	other = data[12 + (1 - index) * 2];
	if (!(metadata & 15) || !(metadata & 0xf0) ||
	    (data[13 + index * 2] & 1))
		return -ENODATA;
	/* Do not choose the other slot when the stored suffix is stale. */
	if (!(data[13 + (1 - index) * 2] & 1) && (other & 0xf0) &&
	    (other & 15) > (metadata & 15))
		return -ESTALE;
	*slot = index ? TETRIS_SCP_SLOT_B : TETRIS_SCP_SLOT_A;
	return 0;
}

static const char *const tetris_scp_container_names[] = {
	"tinysys-scp-RV55_A", "cert1", "cert2",
	"tinysys-scp-RV55_A_dram", "cert1", "cert2",
};

static const char *const tetris_scp_container_failure_names[] = {
	[TETRIS_SCP_CONTAINER_OK] = "ok",
	[TETRIS_SCP_CONTAINER_BAD_ARGUMENT] = "bad-argument",
	[TETRIS_SCP_CONTAINER_BAD_MAGIC] = "bad-magic",
	[TETRIS_SCP_CONTAINER_BAD_EXT_MAGIC] = "bad-ext-magic",
	[TETRIS_SCP_CONTAINER_BAD_HEADER_SIZE] = "bad-header-size",
	[TETRIS_SCP_CONTAINER_BAD_NAME] = "bad-name",
	[TETRIS_SCP_CONTAINER_BAD_SIZE] = "bad-size",
	[TETRIS_SCP_CONTAINER_RANGE_OVERFLOW] = "range-overflow",
};

static const char *const tetris_scp_region_failure_names[] = {
	[TETRIS_SCP_REGION_OK] = "ok",
	[TETRIS_SCP_REGION_ZERO] = "zero",
	[TETRIS_SCP_REGION_TOO_SMALL] = "too-small",
	[TETRIS_SCP_REGION_BAD_LOADER] = "bad-loader",
	[TETRIS_SCP_REGION_BAD_FIRMWARE] = "bad-firmware",
	[TETRIS_SCP_REGION_BAD_DRAM] = "bad-dram",
};

static const char *const tetris_scp_failure_names[] = {
	[TETRIS_SCP_OK] = "ok",
	[TETRIS_SCP_BAD_ARGUMENT] = "bad-argument",
	[TETRIS_SCP_BAD_FDT] = "bad-fdt",
	[TETRIS_SCP_NO_RESERVED_MEMORY] = "no-reserved-memory",
	[TETRIS_SCP_BAD_CELLS] = "bad-cells",
	[TETRIS_SCP_MISSING_SHARE] = "missing-share",
	[TETRIS_SCP_DUPLICATE_SHARE] = "duplicate-share",
	[TETRIS_SCP_MISSING_FIRMWARE] = "missing-firmware",
	[TETRIS_SCP_DUPLICATE_FIRMWARE] = "duplicate-firmware",
	[TETRIS_SCP_BAD_CARVEOUT_PARENT] = "bad-carveout-parent",
	[TETRIS_SCP_BAD_SHARE_REG] = "bad-share-reg",
	[TETRIS_SCP_BAD_FIRMWARE_REG] = "bad-firmware-reg",
	[TETRIS_SCP_RANGE_OVERFLOW] = "range-overflow",
	[TETRIS_SCP_SHARE_TOO_SMALL] = "share-too-small",
	[TETRIS_SCP_SHARE_OUT_OF_WINDOW] = "share-out-of-window",
	[TETRIS_SCP_NOT_IN_DRAM] = "not-in-dram",
	[TETRIS_SCP_CARVEOUT_OVERLAP] = "carveout-overlap",
	[TETRIS_SCP_SLOT_UNVERIFIED] = "slot-unverified",
	[TETRIS_SCP_BAD_SLOT] = "bad-slot",
	[TETRIS_SCP_PARTITION_MISMATCH] = "partition-mismatch",
	[TETRIS_SCP_BAD_PARTITION_SIZE] = "bad-partition-size",
	[TETRIS_SCP_BAD_IMAGE_SIZE] = "bad-image-size",
	[TETRIS_SCP_IDENTITY_UNVERIFIED] = "identity-unverified",
	[TETRIS_SCP_BAD_IDENTITY] = "bad-identity",
	[TETRIS_SCP_REGION_INFO_UNVERIFIED] = "region-info-unverified",
	[TETRIS_SCP_BAD_REGION_INFO] = "bad-region-info",
	[TETRIS_SCP_REGION_INFO_MISMATCH] = "region-info-mismatch",
};

const char *tetris_scp_failure_name(enum tetris_scp_failure failure)
{
	if (failure < 0 || failure >= (int)(sizeof(tetris_scp_failure_names) /
					      sizeof(tetris_scp_failure_names[0])) ||
	    !tetris_scp_failure_names[failure])
		return "unknown";

	return tetris_scp_failure_names[failure];
}

const char *tetris_scp_region_failure_name(
	enum tetris_scp_region_failure failure)
{
	if (failure < 0 ||
	    failure >= (int)(sizeof(tetris_scp_region_failure_names) /
			     sizeof(tetris_scp_region_failure_names[0])) ||
	    !tetris_scp_region_failure_names[failure])
		return "unknown";

	return tetris_scp_region_failure_names[failure];
}

const char *tetris_scp_container_failure_name(
	enum tetris_scp_container_failure failure)
{
	if (failure < 0 ||
	    failure >= (int)(sizeof(tetris_scp_container_failure_names) /
			     sizeof(tetris_scp_container_failure_names[0])) ||
	    !tetris_scp_container_failure_names[failure])
		return "unknown";

	return tetris_scp_container_failure_names[failure];
}

static int tetris_scp_container_fail(
	struct tetris_scp_container *container,
	enum tetris_scp_container_failure failure)
{
	container->failure = failure;
	return -EINVAL;
}

static u32 tetris_scp_get_le32(const u8 *bytes)
{
	return (u32)bytes[0] | (u32)bytes[1] << 8 |
	       (u32)bytes[2] << 16 | (u32)bytes[3] << 24;
}

int tetris_scp_parse_container_headers(
	const u8 headers[TETRIS_SCP_CONTAINER_SECTIONS]
			[TETRIS_SCP_CONTAINER_HEADER_SIZE],
	u64 partition_size, struct tetris_scp_container *container)
{
	u64 offset = 0;
	size_t i;

	if (!container)
		return -EINVAL;
	memset(container, 0, sizeof(*container));
	container->failure = TETRIS_SCP_CONTAINER_BAD_ARGUMENT;
	if (!headers || !partition_size)
		return -EINVAL;

	for (i = 0; i < TETRIS_SCP_CONTAINER_SECTIONS; i++) {
		const u8 *header = headers[i];
		u32 payload_size = tetris_scp_get_le32(header + 4);
		u32 header_size = tetris_scp_get_le32(header + 52);
		u64 next;

		if (tetris_scp_get_le32(header) != TETRIS_SCP_CONTAINER_MAGIC)
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_BAD_MAGIC);
		if (tetris_scp_get_le32(header + 48) !=
		    TETRIS_SCP_CONTAINER_EXT_MAGIC)
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_BAD_EXT_MAGIC);
		if (header_size != TETRIS_SCP_CONTAINER_HEADER_SIZE)
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_BAD_HEADER_SIZE);
		if (strnlen((const char *)header + 8, 32) == 32 ||
		    strcmp((const char *)header + 8,
			   tetris_scp_container_names[i]))
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_BAD_NAME);
		if (!payload_size)
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_BAD_SIZE);
		if (offset > ~(u64)0 - header_size ||
		    offset + header_size > ~(u64)0 - payload_size)
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_RANGE_OVERFLOW);
		next = offset + header_size + payload_size;
		if (next > ~(u64)0 - (TETRIS_SCP_CONTAINER_ALIGNMENT - 1))
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_RANGE_OVERFLOW);
		next = (next + TETRIS_SCP_CONTAINER_ALIGNMENT - 1) &
		       ~(u64)(TETRIS_SCP_CONTAINER_ALIGNMENT - 1);
		if (next > partition_size)
			return tetris_scp_container_fail(container,
					TETRIS_SCP_CONTAINER_BAD_SIZE);

		container->sections[i].offset = offset;
		container->sections[i].payload_size = payload_size;
		offset = next;
	}

	container->image_size = offset;
	container->failure = TETRIS_SCP_CONTAINER_OK;
	container->valid = true;
	return 0;
}

static bool tetris_scp_u32_range_valid(u32 start, u32 size)
{
	return start && size && start <= ~(u32)0 - size;
}

int tetris_scp_decode_region_info(
	const u32 words[TETRIS_SCP_REGION_INFO_WORDS],
	struct tetris_scp_region_snapshot *snapshot)
{
	u32 rounded, span;

	if (!words || !snapshot)
		return -EINVAL;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->failure = TETRIS_SCP_REGION_ZERO;
	snapshot->loader.base = words[0];
	snapshot->loader.size = words[1];
	snapshot->firmware.base = words[2];
	snapshot->firmware.size = words[3];
	snapshot->dram.base = words[4];
	snapshot->dram.size = words[5];
	snapshot->dram_backup_start = words[6];
	snapshot->structure_size = words[7];

	if (!snapshot->structure_size)
		return -ENODATA;
	if (snapshot->structure_size < TETRIS_SCP_REGION_INFO_SIZE) {
		snapshot->failure = TETRIS_SCP_REGION_TOO_SMALL;
		return -EPROTO;
	}
	if (!tetris_scp_u32_range_valid(snapshot->loader.base,
					snapshot->loader.size)) {
		snapshot->failure = TETRIS_SCP_REGION_BAD_LOADER;
		return -EINVAL;
	}
	if (!tetris_scp_u32_range_valid(snapshot->firmware.base,
					snapshot->firmware.size)) {
		snapshot->failure = TETRIS_SCP_REGION_BAD_FIRMWARE;
		return -EINVAL;
	}
	if (snapshot->dram.base || snapshot->dram.size ||
	    snapshot->dram_backup_start) {
		if (!tetris_scp_u32_range_valid(snapshot->dram.base,
						snapshot->dram.size) ||
		    snapshot->dram.size > ~(u32)0 - 1023U) {
			snapshot->failure = TETRIS_SCP_REGION_BAD_DRAM;
			return -EINVAL;
		}
		rounded = (snapshot->dram.size + 1023U) & ~1023U;
		if (rounded > ~(u32)0 / 4U) {
			snapshot->failure = TETRIS_SCP_REGION_BAD_DRAM;
			return -EINVAL;
		}
		span = rounded * 4U;
		if (!snapshot->dram_backup_start ||
		    snapshot->dram_backup_start > ~(u32)0 - span) {
			snapshot->failure = TETRIS_SCP_REGION_BAD_DRAM;
			return -EINVAL;
		}
	}

	snapshot->failure = TETRIS_SCP_REGION_OK;
	snapshot->valid = true;
	return 0;
}

static bool tetris_scp_range_end(const struct tetris_scp_range *range,
				 u64 *end)
{
	if (!range->size || range->base > ~(u64)0 - range->size)
		return false;

	*end = range->base + range->size;
	return true;
}

static bool tetris_scp_range_equal(const struct tetris_scp_range *left,
				   const struct tetris_scp_range *right)
{
	return left->base == right->base && left->size == right->size;
}

static bool tetris_scp_range_contains(const struct tetris_scp_range *outer,
				      const struct tetris_scp_range *inner)
{
	u64 inner_end, outer_end;

	if (!tetris_scp_range_end(outer, &outer_end) ||
	    !tetris_scp_range_end(inner, &inner_end))
		return false;

	return inner->base >= outer->base && inner_end <= outer_end;
}

static bool tetris_scp_in_dram(const struct tetris_scp_range *range,
			       const struct tetris_scp_range *dram,
			       size_t dram_count)
{
	size_t i;

	for (i = 0; i < dram_count; i++)
		if (tetris_scp_range_contains(&dram[i], range))
			return true;

	return false;
}

static int tetris_scp_unique_node(const void *fdt, const char *compatible,
				  enum tetris_scp_failure missing,
				  enum tetris_scp_failure duplicate,
				  enum tetris_scp_failure *failure)
{
	int first, second;

	first = fdt_node_offset_by_compatible(fdt, -1, compatible);
	if (first < 0) {
		*failure = missing;
		return first;
	}

	second = fdt_node_offset_by_compatible(fdt, first, compatible);
	if (second >= 0) {
		*failure = duplicate;
		return -FDT_ERR_EXISTS;
	}

	return first;
}

static int tetris_scp_read_range(const void *fdt, int node,
				 struct tetris_scp_range *range)
{
	const fdt32_t *reg;
	int len;

	reg = fdt_getprop(fdt, node, "reg", &len);
	if (!reg || len != 4 * sizeof(*reg))
		return -FDT_ERR_BADVALUE;

	range->base = (u64)fdt32_to_cpu(reg[0]) << 32 |
		      fdt32_to_cpu(reg[1]);
	range->size = (u64)fdt32_to_cpu(reg[2]) << 32 |
		      fdt32_to_cpu(reg[3]);
	return 0;
}

static bool tetris_scp_identity_present(const u8 *identity)
{
	size_t i;

	for (i = 0; i < TETRIS_SCP_IDENTITY_SIZE; i++)
		if (identity[i])
			return true;

	return false;
}

static int tetris_scp_fail(struct tetris_scp_inventory *inventory,
			   enum tetris_scp_failure failure)
{
	inventory->failure = failure;
	return -EINVAL;
}

int tetris_scp_validate_inventory(const void *fdt,
				  const struct tetris_scp_range *dram,
	size_t dram_count,
	const struct tetris_scp_partition_observation *partition,
	const struct tetris_scp_region_info_observation *region_info,
	struct tetris_scp_inventory *inventory)
{
	const char *expected_partition;
	u64 firmware_end, share_end;
	int firmware, reserved, share;

	if (!inventory)
		return -EINVAL;

	memset(inventory, 0, sizeof(*inventory));
	inventory->failure = TETRIS_SCP_BAD_ARGUMENT;
	if (!fdt || !dram || !dram_count || !partition || !region_info)
		return -EINVAL;
	if (fdt_check_header(fdt))
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_FDT);

	reserved = fdt_path_offset(fdt, "/reserved-memory");
	if (reserved < 0)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_NO_RESERVED_MEMORY);
	if (fdt_address_cells(fdt, reserved) != 2 ||
	    fdt_size_cells(fdt, reserved) != 2)
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_CELLS);

	share = tetris_scp_unique_node(fdt, TETRIS_SCP_SHARE_COMPAT,
				       TETRIS_SCP_MISSING_SHARE,
				       TETRIS_SCP_DUPLICATE_SHARE,
				       &inventory->failure);
	if (share < 0)
		return -EINVAL;
	firmware = tetris_scp_unique_node(fdt, TETRIS_SCP_FW_COMPAT,
					  TETRIS_SCP_MISSING_FIRMWARE,
					  TETRIS_SCP_DUPLICATE_FIRMWARE,
					  &inventory->failure);
	if (firmware < 0)
		return -EINVAL;
	if (fdt_parent_offset(fdt, share) != reserved ||
	    fdt_parent_offset(fdt, firmware) != reserved)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_BAD_CARVEOUT_PARENT);

	if (tetris_scp_read_range(fdt, share, &inventory->share))
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_SHARE_REG);
	if (tetris_scp_read_range(fdt, firmware, &inventory->firmware))
		return tetris_scp_fail(inventory,
					TETRIS_SCP_BAD_FIRMWARE_REG);
	if (!tetris_scp_range_end(&inventory->share, &share_end) ||
	    !tetris_scp_range_end(&inventory->firmware, &firmware_end))
		return tetris_scp_fail(inventory, TETRIS_SCP_RANGE_OVERFLOW);
	if (inventory->share.size < TETRIS_SCP_SHARE_MIN)
		return tetris_scp_fail(inventory, TETRIS_SCP_SHARE_TOO_SMALL);
	if (inventory->share.base >= TETRIS_SCP_SHARE_LIMIT ||
	    share_end > TETRIS_SCP_SHARE_LIMIT)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_SHARE_OUT_OF_WINDOW);
	if (!tetris_scp_in_dram(&inventory->share, dram, dram_count) ||
	    !tetris_scp_in_dram(&inventory->firmware, dram, dram_count))
		return tetris_scp_fail(inventory, TETRIS_SCP_NOT_IN_DRAM);
	if (inventory->share.base < firmware_end &&
	    inventory->firmware.base < share_end)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_CARVEOUT_OVERLAP);

	if (!partition->slot_verified ||
	    partition->slot_source != TETRIS_SCP_SLOT_SOURCE_BOOT_CONTROL)
		return tetris_scp_fail(inventory, TETRIS_SCP_SLOT_UNVERIFIED);
	if (partition->active_slot == TETRIS_SCP_SLOT_A)
		expected_partition = "scp_a";
	else if (partition->active_slot == TETRIS_SCP_SLOT_B)
		expected_partition = "scp_b";
	else
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_SLOT);
	if (!partition->partition_name ||
	    strcmp(partition->partition_name, expected_partition))
		return tetris_scp_fail(inventory,
					TETRIS_SCP_PARTITION_MISMATCH);
	if (!partition->partition_size)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_BAD_PARTITION_SIZE);
	if (!partition->image_size ||
	    partition->image_size > partition->partition_size)
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_IMAGE_SIZE);
	if (!partition->identity_verified)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_IDENTITY_UNVERIFIED);
	if (!tetris_scp_identity_present(partition->image_identity))
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_IDENTITY);

	if (!region_info->abi_verified)
		return tetris_scp_fail(inventory,
					TETRIS_SCP_REGION_INFO_UNVERIFIED);
	if (!region_info->abi_version || !region_info->structure_size)
		return tetris_scp_fail(inventory, TETRIS_SCP_BAD_REGION_INFO);
	if (!tetris_scp_range_equal(&region_info->share, &inventory->share) ||
	    !tetris_scp_range_equal(&region_info->firmware,
				    &inventory->firmware))
		return tetris_scp_fail(inventory,
					TETRIS_SCP_REGION_INFO_MISMATCH);

	inventory->active_slot = partition->active_slot;
	inventory->slot_source = partition->slot_source;
	inventory->partition_size = partition->partition_size;
	inventory->image_size = partition->image_size;
	memcpy(inventory->image_identity, partition->image_identity,
	       sizeof(inventory->image_identity));
	inventory->region_info_version = region_info->abi_version;
	inventory->region_info_size = region_info->structure_size;
	inventory->failure = TETRIS_SCP_OK;
	inventory->valid = true;
	return 0;
}

int tetris_scp_handoff_inventory_disabled(void)
{
	return -EOPNOTSUPP;
}
