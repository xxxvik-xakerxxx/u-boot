/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_SCP_SECURE_H
#define __TETRIS_SCP_SECURE_H
#include "tetris_scp_handoff.h"

struct tetris_scp_secure_plan {
	u64 firmware, capacity, shared, shared_size;
	u32 dram_size, dump_size, offsets[25], sizes[25], dumps[6];
	u32 state, last_function, last_operation;
	u64 secure_error;
	u64 audio, audio_size;
	u32 audio_offsets[4], audio_sizes[4];
};

struct tetris_scp_secure_ops {
	u64 (*smc)(u32 function, u64 op, u64 a, u64 b, u64 c);
	void (*write)(u64 address, u32 value);
	void (*sync)(void);
};

int tetris_scp_secure_plan(struct tetris_scp_secure_plan *p,
			   u64 firmware, u64 capacity, u64 shared, u64 shared_size,
			   u32 dram_size, const u32 *table, size_t cells,
			   const u32 dumps[5]);
int tetris_scp_secure_begin(struct tetris_scp_secure_plan *p,
			    const struct tetris_scp_secure_ops *ops);
int tetris_scp_secure_audio_plan(struct tetris_scp_secure_plan *p,
				u64 base, u64 capacity, const u32 sizes[4]);
int tetris_scp_secure_finish(struct tetris_scp_secure_plan *p,
			     const struct tetris_scp_secure_ops *ops);
#endif
