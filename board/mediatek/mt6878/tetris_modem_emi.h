/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_EMI_H
#define __TETRIS_MODEM_EMI_H

enum tetris_modem_emi_state {
	TETRIS_MODEM_EMI_FRESH,
	TETRIS_MODEM_EMI_ATTEMPTED,
	TETRIS_MODEM_EMI_RANGE_VERIFIED,
	TETRIS_MODEM_EMI_FAILED,
};

#define TETRIS_MODEM_EMI_POLICY_WORDS 8
#define TETRIS_MODEM_EMI_STEPS 21

struct tetris_modem_emi_transaction {
	unsigned int state;
	unsigned int step;
	unsigned int slot;
	unsigned long long reply[TETRIS_MODEM_EMI_STEPS];
};

struct tetris_modem_emi_ops {
	int (*smc)(void *context, unsigned int function, unsigned int operation,
		   unsigned long long a, unsigned long long b,
		   unsigned long long c, unsigned long long *reply);
	void *context;
};

#define TETRIS_MODEM_EMI_READ_WORDS 11

struct tetris_modem_emi_observation {
	unsigned int attempted;
	unsigned int step;
	unsigned long long words[TETRIS_MODEM_EMI_READ_WORDS];
};

/* Read enable, raw endpoints and eight policy groups, never program a slot.
 * Callback success must supply a reply. Consume the observation on the first
 * call; stop on the first error. Words are published only on complete success.
 * Observed policy is NOT an approved policy or evidence of slot ownership.
 */
int tetris_modem_read_emi_slot(unsigned int slot,
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_observation *observation);

int tetris_modem_observe_diagnostic(void *fdt);

/*
 * Candidate normal-path policy for core slots 32..38 and shared slots 41..43,
 * from preloader
 * image SHA256 5d2bedd0...8d2c50f (GFH header plus declared image).
 * Caller supplies a verified image digest, not an assumed firmware version.
 * Unknown images, auxiliary slot 39 and dynamic padding slot 40 fail closed.
 * The normal/AEE branch was traced through the pinned preloader predicate;
 * this is NOT a cold/warm distinction. The AEE second list is not combined
 * with the first. Caller must independently establish normal-path state and
 * exclusive ownership of each supplied range, including shared-memory peers.
 * Unlisted AIDs are expected denied: this is an acceptance condition, NOT
 * evidence of reset defaults. The transaction must verify every field.
 * No hardware writes; output remains unchanged on failure.
 */
int tetris_modem_plan_emi_policy(const unsigned char preloader_sha256[32],
		unsigned int slot,
		unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS]);

/*
 * Range transaction for pinned BL_EMIMPU_CONTROL; no permission preset writes.
 * Policy is eight packed words, two bits per domain, supplied independently
 * by a verified platform policy. Never learn/approve it from current readback.
 * Compare all 256 domain fields before programming and again afterwards.
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
		const unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS],
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_transaction *transaction);

#endif
