/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_SCP_TCM_H
#define __TETRIS_SCP_TCM_H
#include "tetris_scp_handoff.h"

struct tetris_scp_tcm_ops {
	u32 (*read)(u64 address);
	void (*write)(u64 address, u32 value);
	void (*barrier)(void);
};

/* Caller authenticates the firmware and establishes exclusive ownership. */
int tetris_scp_tcm_prepare(const u8 *core, u32 core_size, u64 firmware,
			   u64 capacity, u32 dram_size, u32 scpctl,
			   const struct tetris_scp_tcm_ops *ops);
#endif
