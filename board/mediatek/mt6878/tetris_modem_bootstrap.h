/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_BOOTSTRAP_H
#define __TETRIS_MODEM_BOOTSTRAP_H

#include "tetris_modem_boot_secure.h"
#include "tetris_modem_emi_rows.h"

/* Pointer-free source-derived rows cached BEFORE authenticated snapshot release.
 * Cache-clean placed images and initialized service banks belong to the loader
 * owner. No API accepting mutable placed RAM or caller readiness booleans.
 */
struct tetris_modem_bootstrap_plan {
	struct tetris_modem_emi_resources resources;
	struct tetris_modem_emi_rows rows;
	unsigned char preloader_sha256[32];
};

enum tetris_modem_bootstrap_stage {
	TETRIS_MD_BOOT_INPUT,
	TETRIS_MD_BOOT_PROFILE,
	TETRIS_MD_BOOT_COLD_OFF,
	TETRIS_MD_BOOT_EMI,
	TETRIS_MD_BOOT_REMAP,
	TETRIS_MD_BOOT_SMEM_REMAP,
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
	struct tetris_modem_emi_rows_transaction emi;
	unsigned int bank_index, bank_call;
	unsigned long long bank_reply[12][4];
};

/* One attempt per AP boot, on the primary CPU before Linux/idle/VCOREFS entry.
 * Requires the matching stock ATF/BL2 -> NS BL33 chain and existing loader's
 * authenticated, cache-clean, exclusively LMB-reserved images/service banks.
 * No permission booleans, guessed reset writes or automatic DT publication.
 * All rows (including PHY39/preset40) are derived before mutation, not supplied
 * as raw ranges. NC/cache final 128MiB mappings must fit owned reservations.
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
