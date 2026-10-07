/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_REMAP_H
#define __TETRIS_MODEM_REMAP_H

enum tetris_modem_remap_state {
	TETRIS_MODEM_REMAP_FRESH,
	TETRIS_MODEM_REMAP_ATTEMPTED,
	TETRIS_MODEM_REMAP_VERIFIED,
	TETRIS_MODEM_REMAP_FAILED,
};

struct tetris_modem_remap_transaction {
	unsigned int state;
	unsigned int operation;
	unsigned long long reply[2][4];
};

struct tetris_modem_remap_ops {
	int (*smc)(void *context, unsigned int function, unsigned int operation,
		   unsigned int low, unsigned int high, unsigned long long reply[4]);
	void *context;
};

/*
 * Caller must first authenticate the firmware/platform, hold the modem in
 * reset and exclusively reserve the entire window. DRAM bounds alone do not
 * prove ownership. No production SMC adapter or boot caller is installed.
 * Keep one zero-initialized transaction for the lifetime of that reservation:
 * any attempted call consumes it, including transport/readback failures.
 * Never clear/retry it to recover partially programmed hardware.
 */
int tetris_modem_program_remap(unsigned long long base,
			       unsigned long long capacity,
			       unsigned long long dram_base,
			       unsigned long long dram_size,
			       const struct tetris_modem_remap_ops *ops,
			       struct tetris_modem_remap_transaction *transaction);

#endif
