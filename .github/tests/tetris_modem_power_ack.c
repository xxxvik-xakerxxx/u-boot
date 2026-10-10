// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const unsigned long addresses[] = {
	0x1c001e00UL, 0x1c001f24UL, 0x10001c5cUL, 0x10001c4cUL, 0x1027008cUL,
};
static unsigned int values[5], reads, delays;
static struct {
	struct {
		unsigned long address;
		unsigned int value, polls;
		int error;
		struct {
			unsigned long address;
			unsigned int value, polls;
			int error;
		} cleanup;
	} report;
} boot;
static struct { struct { unsigned long address; unsigned int value; } report; } owner;

static unsigned int readl(const volatile void *address)
{
	unsigned int i;
	reads++;
	for (i = 0; i < 5; i++)
		if ((unsigned long)address == addresses[i])
			return values[i];
	assert(0);
	return 0;
}
static void udelay(unsigned int interval) { assert(interval == 10); delays++; }
/* Only the error-latch boundary is mocked; no simulated reset or cleanup. */
static int md_boot_fail(int error)
{
	if (!boot.report.error)
		boot.report.error = error;
	return boot.report.error;
}
#include "producer.inc"

static void reset(unsigned int power)
{
	values[0] = power; values[1] = 3; values[2] = 0x200;
	values[3] = 0x800; values[4] = 0xc0;
	reads = delays = 0;
	memset(&boot, 0, sizeof(boot));
	memset(&owner, 0, sizeof(owner));
}

int main(void)
{
	unsigned int secondary, i, power, cases = 0;
	/* ACK31 is unrelated to the MD_OPS completion predicate. */
	for (secondary = 0; secondary < 2; secondary++) {
		power = 0x02000009U | (secondary << 31);
		reset(power);
		assert(!cold_off() && reads == 5);
		assert(!md_boot_cold_off() && reads == 10);
		cases++;
		for (i = 0; i < 5; i++) {
			reset(power);
			values[i] = i ? 0 : power | MD_ON;
			assert(cold_off() == -EBUSY && reads == i + 1);
			assert(owner.report.address == addresses[i] && owner.report.value == values[i]);
			reset(power);
			values[i] = i ? 0 : power | MD_ACK;
			assert(md_boot_cold_off() == -EBUSY && reads == i + 1);
			assert(boot.report.address == addresses[i] && boot.report.value == values[i]);
			cases++;
		}
		reset(power | MD_ON | MD_ACK);
		assert(!md_boot_wait(MD_POWER, MD_ON | MD_ACK, MD_ON | MD_ACK));
		assert(reads == 1 && !delays && boot.report.polls == 1);
		cases++;
		reset(power);
		assert(!md_cleanup_wait(MD_POWER, MD_ON | MD_ACK, 0));
		assert(reads == 1 && !delays && boot.report.cleanup.polls == 1);
		cases++;
		reset(power | MD_ON);
		assert(md_boot_wait(MD_POWER, MD_ON | MD_ACK, MD_ON | MD_ACK) == -ETIMEDOUT);
		assert(reads == MD_POLL_COUNT && delays == MD_POLL_COUNT - 1);
		assert(boot.report.value == values[0] && boot.report.polls == MD_POLL_COUNT);
		cases++;
		reset(power | MD_ACK);
		assert(md_cleanup_wait(MD_POWER, MD_ON | MD_ACK, 0) == -ETIMEDOUT);
		assert(reads == MD_POLL_COUNT && delays == MD_POLL_COUNT - 1);
		assert(boot.report.cleanup.error == -ETIMEDOUT);
		cases++;
	}
	/* The actual handset sample must still refuse, with no corrective operation. */
	reset(0x4200000d);
	assert(cold_off() == -EBUSY && reads == 1 && !delays);
	assert(owner.report.address == MD_POWER && owner.report.value == 0x4200000d);
	cases++;
	printf("MD ACK30 production helpers: %u cases PASS (mocked reads/timer only)\n", cases);
	return 0;
}
