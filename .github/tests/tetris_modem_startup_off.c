/* SPDX-License-Identifier: GPL-2.0+ */
/* Actual helpers, synthetic MMIO only; not physical NS access or BROM proof. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct {
	struct { unsigned int stage, value; unsigned long address; int error; } report;
} owner;
static const unsigned long status[] = {
	0x10001c5cUL, 0x10001c4cUL, 0x1027008cUL,
	0x1c001e00UL, 0x1c001f24UL, 0x10000000UL,
};
static const unsigned long target[] = {
	0x10001c54UL, 0x10001c44UL, 0x10270084UL,
	0x1c001e00UL, 0x1c001f24UL, 0x10000000UL,
};
static const unsigned int masks[] = { 0x200, 0x800, 0xc0, 0x40000004, 3, 0x300 };
static unsigned int regs[6], reads[6], delays, writes, written[6], cases;
static int fault, race;

static unsigned int readl(const volatile void *pointer)
{
	unsigned long address = (uintptr_t)pointer;
	for (unsigned int i = 0; i < 6; i++) {
		if (address != status[i]) continue;
		reads[i]++;
		if (i == 3 && race && reads[i] == 2) regs[i] &= ~4U;
		return regs[i];
	}
	assert(0);
	return 0;
}

static void writel(unsigned int value, volatile void *pointer)
{
	assert(writes < 6 && (uintptr_t)pointer == target[writes]);
	unsigned int i = writes++;
	written[i] = value;
	if (i < 3) {
		assert(value == masks[i]);
		if (fault != (int)i) regs[i] |= value;
	} else if (i == 3) {
		assert(value == (regs[i] & ~4U));
		regs[i] = value;
		if (fault != (int)i) regs[i] &= ~(1U << 30);
	} else if (fault != (int)i) {
		assert(value == (regs[i] | masks[i]));
		regs[i] = value;
	}
}

static void udelay(unsigned int delay)
{
	assert(delay == 10);
	delays++;
}

#include "producer.inc"

static void reset(unsigned int power)
{
	memset(&owner, 0, sizeof(owner));
	memset(regs, 0, sizeof(regs));
	memset(reads, 0, sizeof(reads));
	memset(written, 0, sizeof(written));
	regs[3] = power; regs[4] = 0x80; regs[5] = 0xa0000400;
	delays = writes = 0; fault = -1; race = 0;
	cases++;
}

int main(void)
{
	for (unsigned int secondary = 0; secondary < 2; secondary++) {
		unsigned int ack31 = secondary ? 1U << 31 : 0;
		reset(0x4200000dU | ack31);
		assert(!startup_off() && writes == 6 && !delays);
		assert(regs[3] == (0x02000009U | ack31));
		assert(written[3] == (0x42000009U | ack31)); /* No reset or bit3 clear. */
		assert(regs[4] == 0x83 && regs[5] == 0xa0000700);
		assert(owner.report.stage == TETRIS_MD_LOAD_STARTUP_CLOCK);
		reset(0x02000009U | ack31);
		assert(!startup_off() && !writes && !delays); /* OFF admission stays separate. */
	}
	for (unsigned int partial = 0; partial < 2; partial++) {
		reset(partial ? 1U << 30 : 4U);
		assert(startup_off() == -EBUSY && !writes && !delays);
		assert(owner.report.stage == TETRIS_MD_LOAD_STARTUP_STATE);
	}
	for (int stuck = 0; stuck < 6; stuck++) {
		reset(0x4200000d);
		fault = stuck;
		assert(fail(startup_off()) == -ETIMEDOUT);
		assert(fail(-EINVAL) == -ETIMEDOUT); /* First operational error retained. */
		assert(writes == (unsigned int)stuck + 1 && delays == 9999);
		assert(reads[stuck] == (stuck >= 3 ? 10002U - (stuck != 3) : 10000U));
		assert(owner.report.stage == (unsigned int)TETRIS_MD_LOAD_STARTUP_IFR9 + stuck);
		assert(owner.report.address == status[stuck] && owner.report.value == regs[stuck]);
	}
	reset(0x4200000d);
	race = 1;
	assert(startup_off() == -EBUSY && writes == 3 && !delays);
	assert(owner.report.stage == TETRIS_MD_LOAD_STARTUP_POWER);
	puts("initial MD startup-OFF production helpers: 13 cases PASS (synthetic MMIO only)");
	assert(cases == 13);
	return 0;
}
