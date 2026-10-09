/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_BOOT_SECURE_H
#define __TETRIS_MODEM_BOOT_SECURE_H

#include "tetris_modem_emi.h"
#include "tetris_modem_remap.h"

struct blk_desc;
struct tetris_modem_boot_secure;

enum tetris_modem_boot_secure_stage {
	TETRIS_MD_SECURE_PROFILE,
	TETRIS_MD_SECURE_OBSERVE,
	TETRIS_MD_SECURE_EMI,
	TETRIS_MD_SECURE_REMAP,
};

struct tetris_modem_boot_secure_report {
	unsigned int stage, operation, slot;
	int error;
	unsigned long long reply[4];
};

/* Draft transport only, NOT authenticated MD ownership or startup permission.
 * One boot-lifetime session. Reuse actual stored-ATF profile verifier, then
 * actual SMC replies establish stage admission/programming results. Stored
 * ATF hash is NOT loaded-ATF attestation. No destroy/reset/retry API.
 */
int tetris_modem_boot_secure_open(struct blk_desc *dev,
				struct tetris_modem_boot_secure **out);
int tetris_modem_boot_secure_observe(struct tetris_modem_boot_secure *session,
		unsigned int slot, struct tetris_modem_emi_observation *out);

/* Use only under a real execution-inhibited reservation/platform owner.
 * Parameters come from authenticated layout + owned LMB reservation and the
 * matching independently established policy, NEVER approval from readback.
 * ATF command0 owns the one-shot check: -4 aborts, never reset/retry a guard.
 * These calls do not establish NS write access to SPM/IFR/NEMI or MD readiness.
 */
int tetris_modem_boot_secure_range(struct tetris_modem_boot_secure *session,
		unsigned long long start, unsigned long long size, unsigned int slot,
		unsigned long long reserved_base, unsigned long long reserved_size,
		const unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS]);
int tetris_modem_boot_secure_remap(struct tetris_modem_boot_secure *session,
		unsigned long long base, unsigned long long capacity,
		unsigned long long dram_base, unsigned long long dram_size);
/* Inspect first failure/last actual reply without any further hardware call. */
int tetris_modem_boot_secure_report(const struct tetris_modem_boot_secure *session,
				  struct tetris_modem_boot_secure_report *out);
#endif
