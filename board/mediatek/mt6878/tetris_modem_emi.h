/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_EMI_H
#define __TETRIS_MODEM_EMI_H

enum tetris_modem_emi_state {
	TETRIS_MODEM_EMI_FRESH,
	TETRIS_MODEM_EMI_ATTEMPTED,
	TETRIS_MODEM_EMI_RANGE_VERIFIED,
	TETRIS_MODEM_EMI_FAILED,
};

struct tetris_modem_emi_transaction {
	unsigned int state;
	unsigned int step;
	unsigned int slot;
	unsigned long long reply[5];
};

struct tetris_modem_emi_ops {
	int (*smc)(void *context, unsigned int function, unsigned int operation,
		   unsigned long long a, unsigned long long b,
		   unsigned long long c, unsigned long long *reply);
	void *context;
};

/*
 * Range-only transaction for pinned BL_EMIMPU_CONTROL; no permission preset.
 * Caller owns the reservation AND slot, has authenticated the firmware/platform,
 * established the boot-stage/permission policy, and holds the modem in reset.
 * A disabled slot does not prove ownership or an unused ATF one-shot guard.
 * The start+size endpoint follows LK; hardware endpoint semantics remain an
 * integration prerequisite. RANGE_VERIFIED does not mean protected or bootable.
 * No production transport or boot caller exists. Any callback attempt consumes
 * this transaction, even a read failure; never clear it to retry partial state.
 */
int tetris_modem_program_emi_range(unsigned long long start,
		unsigned long long size, unsigned int slot,
		unsigned long long reserved_base, unsigned long long reserved_size,
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_transaction *transaction);

#endif
