/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_LOADED_BOOT_H
#define __TETRIS_MODEM_LOADED_BOOT_H
#include "tetris_modem_bootstrap.h"

enum tetris_modem_loaded_stage {
	TETRIS_MD_LOAD_INPUT, TETRIS_MD_LOAD_PROFILE, TETRIS_MD_LOAD_OFF,
	TETRIS_MD_LOAD_FIRMWARE_RESERVE, TETRIS_MD_LOAD_NC_RESERVE,
	TETRIS_MD_LOAD_CACHE_RESERVE, TETRIS_MD_LOAD_SIB_RESERVE,
	TETRIS_MD_LOAD_MAP, TETRIS_MD_LOAD_AUTH_COPY, TETRIS_MD_LOAD_SERVICES,
	TETRIS_MD_LOAD_BOOTSTRAP, TETRIS_MD_LOAD_COMPLETE,
};

struct tetris_modem_loaded_report {
	unsigned int stage, value;
	unsigned long address;
	int error;
	struct tetris_modem_bootstrap_plan bootstrap;
	struct tetris_modem_bootstrap_report hardware;
};

/* Internal CI opt-in caller ONLY, no board registration or public command.
 * slot and CCB/PHY are actual resolved boot policy; preloader_sha256 comes
 * from independent profile measurement, not a chosen hash/auth assertion.
 * Matching NS BL33/ATF scope, sole synchronous primary-CPU AP owner required.
 * Does NOT publish complete CCCI tags or promise SIM/call readiness.
 */
int tetris_modem_loaded_boot_once(void *fdt, struct blk_desc *dev, char slot,
		unsigned int ccb_gear, const char *phy_gear,
		const unsigned char preloader_sha256[32]);
int tetris_modem_loaded_boot_report(struct tetris_modem_loaded_report *out);
#endif
