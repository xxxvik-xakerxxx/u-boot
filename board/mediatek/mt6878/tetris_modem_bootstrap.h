/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_BOOTSTRAP_H
#define __TETRIS_MODEM_BOOTSTRAP_H

#include "tetris_modem_boot_secure.h"

/* Slot order is the actual LK table: 32..43. Zero size means inactive.
 * This is a transaction input, NOT an authentication/ownership certificate.
 * Construct it inside the existing authenticated loader/LMB/cache lifetime.
 * Do not expose it through a command, chosen properties or userspace input.
 */
struct tetris_modem_bootstrap_range {
	unsigned long long start, size;
};

struct tetris_modem_bootstrap_plan {
	unsigned long long base, capacity, dram_base, dram_size;
	unsigned char preloader_sha256[32];
	struct tetris_modem_bootstrap_range ranges[12];
};

enum tetris_modem_bootstrap_stage {
	TETRIS_MD_BOOT_INPUT,
	TETRIS_MD_BOOT_PROFILE,
	TETRIS_MD_BOOT_COLD_OFF,
	TETRIS_MD_BOOT_EMI,
	TETRIS_MD_BOOT_REMAP,
	TETRIS_MD_BOOT_REMAP_LOCK,
	TETRIS_MD_BOOT_CLOCK,
	TETRIS_MD_BOOT_ISOLATION,
	TETRIS_MD_BOOT_POWER,
	TETRIS_MD_BOOT_BUS_NEMI,
	TETRIS_MD_BOOT_BUS_IFR11,
	TETRIS_MD_BOOT_BUS_IFR9,
	TETRIS_MD_BOOT_RELEASE,
	TETRIS_MD_BOOT_BROM,
	TETRIS_MD_BOOT_COMPLETE,
};

enum tetris_modem_bootstrap_cleanup_stage {
	TETRIS_MD_CLEANUP_NONE,
	TETRIS_MD_CLEANUP_IFR9,
	TETRIS_MD_CLEANUP_IFR11,
	TETRIS_MD_CLEANUP_NEMI,
	TETRIS_MD_CLEANUP_POWER,
	TETRIS_MD_CLEANUP_ISOLATION,
	TETRIS_MD_CLEANUP_CLOCK,
	TETRIS_MD_CLEANUP_COMPLETE,
};

struct tetris_modem_bootstrap_cleanup {
	unsigned int stage, polls, value;
	unsigned long address;
	int error;
};

struct tetris_modem_bootstrap_report {
	unsigned int stage, slot, polls;
	unsigned long address;
	unsigned int value;
	unsigned long long reply[4];
	int error;
	struct tetris_modem_bootstrap_cleanup cleanup;
};

/* One attempt per AP boot, on the primary CPU before Linux/idle/VCOREFS entry.
 * Requires the matching stock ATF/BL2 -> NS BL33 chain and existing loader's
 * authenticated, cache-clean, exclusively LMB-reserved images/service banks.
 * No permission booleans, guessed reset writes or automatic DT publication.
 * Every active slot must be resolved: unsupported 39/40 fail BEFORE mutation.
 * Success means actual four-one BROM replies and stock-style powered handoff,
 * not SIM registration or a transport READY event. After our own MMIO writes,
 * failure attempts LK's bounded protect/off/isolate/gate cleanup. First fault
 * and any cleanup fault are distinct; caller must not retry this boot.
 */
int tetris_modem_bootstrap_once(struct blk_desc *dev,
		const struct tetris_modem_bootstrap_plan *plan);
/* Snapshot remains available after any error; no additional hardware access. */
int tetris_modem_bootstrap_report(struct tetris_modem_bootstrap_report *out);

#endif
