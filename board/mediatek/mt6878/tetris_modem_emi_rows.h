/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_EMI_ROWS_H
#define __TETRIS_MODEM_EMI_ROWS_H
#include "tetris_modem_layout.h"
#include "tetris_modem_emi.h"

struct tetris_modem_emi_window {
	unsigned long long base, capacity;
};

struct tetris_modem_emi_resources {
	struct tetris_modem_emi_window firmware, nc, cache, sib;
	unsigned long long dram_base, dram_size;
};

enum tetris_modem_emi_row_kind {
	TETRIS_MD_EMI_ABSENT,
	TETRIS_MD_EMI_RANGE,
	TETRIS_MD_EMI_PRESET_RANGE,
};

struct tetris_modem_emi_row {
	unsigned int kind, role, slot;
	unsigned long long start, size;
	struct tetris_modem_emi_window reservation;
	unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS];
};

struct tetris_modem_emi_rows {
	struct tetris_modem_memory_map memory;
	struct tetris_modem_smem_inputs smem_inputs;
	struct tetris_modem_smem_plan smem;
	struct tetris_modem_emi_row row[12];
};

/* Actual immutable signed ROM source from the existing authenticated loader.
 * Resources are its actual LMB allocations, not permission/ownership booleans.
 * phy_gear is the actual resolved md1_phy_cap_gear option text; NULL/zero are
 * absent. Produce all rows from source data, including dynamic preset slot40.
 * Does not allocate/free RAM, access protected RAM, issue SMC or publish DT.
 * Atomic output; no input/output aliases. Signature verification stays in the
 * existing manufacturer-root loader; this planner is not an AUTH shortcut.
 */
int tetris_modem_emi_rows_b41(const void *rom, size_t rom_size, size_t dsp_size,
		const struct tetris_modem_emi_resources *resources,
		unsigned int ccb_gear, const char *phy_gear,
		const unsigned char preloader_sha256[32],
		struct tetris_modem_emi_rows *out);

struct tetris_modem_emi_rows_transaction {
	struct tetris_modem_emi_transaction ranges[12];
	struct tetris_modem_emi_observation padding;
	unsigned int attempted, slot, operation;
	unsigned long long reply;
	int error;
};

/* Exact fixed B4.1 policy: normal preloader table or ATF's four presets. */
int tetris_modem_emi_row_policy_b41(unsigned int slot, unsigned int role,
		unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS]);

/* Uses real supplied secure transport; no flags can substitute for replies.
 * One attempt: preset40 read/validate -> exact command6 -> readback -> range0.
 * Command0 is authoritative for consumed slot guards, including -4. Never
 * reset/retry a guard or roll back OR-written permissions on partial failure.
 */
int tetris_modem_emi_rows_program(const struct tetris_modem_emi_rows *rows,
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_rows_transaction *transaction);

/* Direct BL33 transport for this owned component, including actual preset6.
 * Same pinned NS BL33 boot/profile scope as the frozen bootstrap caller.
 */
extern const struct tetris_modem_emi_ops tetris_modem_emi_rows_hw_ops;
#endif
