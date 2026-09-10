/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_SCP_HANDOFF_H
#define __TETRIS_SCP_HANDOFF_H

#ifdef TETRIS_SCP_HANDOFF_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef __UINT64_TYPE__ u64;
#else
#include <linux/errno.h>
#include <linux/types.h>
#endif

#define TETRIS_SCP_IDENTITY_SIZE	32U

enum tetris_scp_slot {
	TETRIS_SCP_SLOT_UNKNOWN,
	TETRIS_SCP_SLOT_A,
	TETRIS_SCP_SLOT_B,
};

enum tetris_scp_slot_source {
	TETRIS_SCP_SLOT_SOURCE_UNKNOWN,
	TETRIS_SCP_SLOT_SOURCE_BOOT_CONTROL,
};

enum tetris_scp_failure {
	TETRIS_SCP_OK,
	TETRIS_SCP_BAD_ARGUMENT,
	TETRIS_SCP_BAD_FDT,
	TETRIS_SCP_NO_RESERVED_MEMORY,
	TETRIS_SCP_BAD_CELLS,
	TETRIS_SCP_MISSING_SHARE,
	TETRIS_SCP_DUPLICATE_SHARE,
	TETRIS_SCP_MISSING_FIRMWARE,
	TETRIS_SCP_DUPLICATE_FIRMWARE,
	TETRIS_SCP_BAD_CARVEOUT_PARENT,
	TETRIS_SCP_BAD_SHARE_REG,
	TETRIS_SCP_BAD_FIRMWARE_REG,
	TETRIS_SCP_RANGE_OVERFLOW,
	TETRIS_SCP_SHARE_TOO_SMALL,
	TETRIS_SCP_SHARE_OUT_OF_WINDOW,
	TETRIS_SCP_NOT_IN_DRAM,
	TETRIS_SCP_CARVEOUT_OVERLAP,
	TETRIS_SCP_SLOT_UNVERIFIED,
	TETRIS_SCP_BAD_SLOT,
	TETRIS_SCP_PARTITION_MISMATCH,
	TETRIS_SCP_BAD_PARTITION_SIZE,
	TETRIS_SCP_BAD_IMAGE_SIZE,
	TETRIS_SCP_IDENTITY_UNVERIFIED,
	TETRIS_SCP_BAD_IDENTITY,
	TETRIS_SCP_REGION_INFO_UNVERIFIED,
	TETRIS_SCP_BAD_REGION_INFO,
	TETRIS_SCP_REGION_INFO_MISMATCH,
};

struct tetris_scp_range {
	u64 base;
	u64 size;
};

/* Data supplied by a future, authoritative boot-control/GPT adapter. */
struct tetris_scp_partition_observation {
	enum tetris_scp_slot active_slot;
	enum tetris_scp_slot_source slot_source;
	bool slot_verified;
	const char *partition_name;
	u64 partition_size;
	u64 image_size;
	bool identity_verified;
	u8 image_identity[TETRIS_SCP_IDENTITY_SIZE];
};

/* Decoded only by a future adapter for the still-unconfirmed LK ABI. */
struct tetris_scp_region_info_observation {
	bool abi_verified;
	u32 abi_version;
	u32 structure_size;
	struct tetris_scp_range share;
	struct tetris_scp_range firmware;
};

struct tetris_scp_inventory {
	bool valid;
	enum tetris_scp_failure failure;
	enum tetris_scp_slot active_slot;
	enum tetris_scp_slot_source slot_source;
	u64 partition_size;
	u64 image_size;
	u8 image_identity[TETRIS_SCP_IDENTITY_SIZE];
	u32 region_info_version;
	u32 region_info_size;
	struct tetris_scp_range share;
	struct tetris_scp_range firmware;
};

int tetris_scp_validate_inventory(const void *fdt,
				  const struct tetris_scp_range *dram,
	size_t dram_count,
	const struct tetris_scp_partition_observation *partition,
	const struct tetris_scp_region_info_observation *region_info,
	struct tetris_scp_inventory *inventory);
const char *tetris_scp_failure_name(enum tetris_scp_failure failure);

/* Explicitly fail closed until the LK/SCP handoff ABI is proven. */
#ifdef TETRIS_SCP_HANDOFF_HOST_TEST
int tetris_scp_handoff_inventory_disabled(void);
#elif IS_ENABLED(CONFIG_TETRIS_SCP_HANDOFF_INVENTORY)
int tetris_scp_handoff_inventory_disabled(void);
#else
static inline int tetris_scp_handoff_inventory_disabled(void)
{
	return -EOPNOTSUPP;
}
#endif

#endif
