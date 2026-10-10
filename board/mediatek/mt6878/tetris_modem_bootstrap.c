// SPDX-License-Identifier: GPL-2.0+
#include <asm/io.h>
#include <asm/system.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_modem_layout.h"
#include "tetris_modem_bootstrap.h"

/* Matching B4.1 LK physical resources, not addresses from a handset log. */
#define MD_TOP 0x10000000UL
#define MD_POWER 0x1c001e00UL
#define MD_EXTISO 0x1c001f24UL
#define MD_IFR9_CLR 0x10001c58UL
#define MD_IFR9_SET 0x10001c54UL
#define MD_IFR9_STA 0x10001c5cUL
#define MD_IFR11_CLR 0x10001c48UL
#define MD_IFR11_SET 0x10001c44UL
#define MD_IFR11_STA 0x10001c4cUL
#define MD_NEMI_CLR 0x10270088UL
#define MD_NEMI_SET 0x10270084UL
#define MD_NEMI_STA 0x1027008cUL
#define MD_ON 4U
#define MD_ACK (3U << 30)
#define MD_CLOCK 0x300U
#define MD_POLL_US 10
#define MD_POLL_COUNT 10000
#define MD_BROM_COUNT 100

static struct {
	struct tetris_modem_bootstrap_plan input;
	struct tetris_modem_bootstrap_report report;
	unsigned int attempted;
	unsigned int mutated, cleanup_attempted;
} boot;

static void md_boot_cleanup(void);

static int md_boot_fail(int error)
{
	if (!boot.report.error)
		boot.report.error = error < 0 ? error : -EPROTO;
	if (boot.mutated && !boot.cleanup_attempted)
		md_boot_cleanup();
	return boot.report.error;
}

static unsigned int md_cleanup_read(unsigned long address)
{
	boot.report.cleanup.address = address;
	boot.report.cleanup.value = readl((const volatile void *)address);
	return boot.report.cleanup.value;
}

static int md_cleanup_wait(unsigned long address, unsigned int mask,
			   unsigned int expected)
{
	unsigned int i;

	for (i = 0; i < MD_POLL_COUNT; i++) {
		boot.report.cleanup.polls = i + 1;
		if ((md_cleanup_read(address) & mask) == expected)
			return 0;
		if (i + 1 < MD_POLL_COUNT)
			udelay(MD_POLL_US);
	}
	boot.report.cleanup.error = -ETIMEDOUT;
	return -ETIMEDOUT;
}

static void md_boot_cleanup(void)
{
	unsigned int value;

	/* Initial strict OFF and our first MMIO write precede this lifecycle flag.
	 * Never run for inherited state, profile/input/EMI/remap admission failure.
	 * Stop at the first teardown fault; never hide it with a reset or retry.
	 */
	boot.cleanup_attempted = 1;
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_IFR9;
	writel(0x200, (volatile void *)MD_IFR9_SET);
	if (md_cleanup_wait(MD_IFR9_STA, 0x200, 0x200))
		return;
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_IFR11;
	writel(0x800, (volatile void *)MD_IFR11_SET);
	if (md_cleanup_wait(MD_IFR11_STA, 0x800, 0x800))
		return;
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_NEMI;
	writel(0xc0, (volatile void *)MD_NEMI_SET);
	if (md_cleanup_wait(MD_NEMI_STA, 0xc0, 0xc0))
		return;
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_POWER;
	value = md_cleanup_read(MD_POWER);
	writel(value & ~MD_ON, (volatile void *)MD_POWER);
	if (md_cleanup_wait(MD_POWER, MD_ON | MD_ACK, 0))
		return;
	/* LK isolation-up is AFTER acknowledged shutdown, never before it. */
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_ISOLATION;
	value = md_cleanup_read(MD_EXTISO);
	writel(value | 3U, (volatile void *)MD_EXTISO);
	if (md_cleanup_wait(MD_EXTISO, 3, 3))
		return;
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_CLOCK;
	value = md_cleanup_read(MD_TOP);
	writel(value | MD_CLOCK, (volatile void *)MD_TOP);
	if (md_cleanup_wait(MD_TOP, MD_CLOCK, MD_CLOCK))
		return;
	boot.report.cleanup.stage = TETRIS_MD_CLEANUP_COMPLETE;
}

static unsigned int md_boot_read(unsigned long address)
{
	boot.report.address = address;
	boot.report.value = readl((const volatile void *)address);
	return boot.report.value;
}

static int md_boot_wait(unsigned long address, unsigned int mask,
			unsigned int expected)
{
	unsigned int i;

	for (i = 0; i < MD_POLL_COUNT; i++) {
		boot.report.polls = i + 1;
		if ((md_boot_read(address) & mask) == expected)
			return 0;
		if (i + 1 < MD_POLL_COUNT)
			udelay(MD_POLL_US);
	}
	return md_boot_fail(-ETIMEDOUT);
}

static int md_boot_cold_off(void)
{
	static const unsigned long address[] = {
		MD_POWER, MD_EXTISO, MD_IFR9_STA, MD_IFR11_STA, MD_NEMI_STA,
	};
	static const unsigned int mask[] = { MD_ON | MD_ACK, 3, 0x200, 0x800, 0xc0 };
	static const unsigned int expected[] = { 0, 3, 0x200, 0x800, 0xc0 };
	unsigned int i;

	/* Inherited ON/partial transitions are not ours to reset or shut down. */
	for (i = 0; i < 5; i++) {
		if ((md_boot_read(address[i]) & mask[i]) != expected[i])
			return md_boot_fail(-EBUSY);
	}
	return 0;
}

static int md_boot_plan(void)
{
	const struct tetris_modem_emi_resources *r = &boot.input.resources;
	struct tetris_modem_remap remap;
	unsigned long long profile[8];
	unsigned int i;
	int ret;

	ret = tetris_modem_plan_emi_policy(boot.input.preloader_sha256, 32, profile);
	if (!ret)
		ret = tetris_modem_plan_remap(r->firmware.base, r->firmware.capacity,
			r->dram_base, r->dram_size, &remap);
	if (ret)
		return md_boot_fail(ret);
	/* Bind every active row to its cached allocation, never a generic window.
	 * Source/role/policy completeness comes from the internal authenticated
	 * loader producer; this public draft is not a cryptographic certificate.
	 */
	for (i = 0; i < 12; i++) {
		const struct tetris_modem_emi_row *row = &boot.input.rows.row[i];
		const struct tetris_modem_emi_window *w = i < 7 || i == 8 ?
			&r->firmware : i == 7 ? &r->sib : i == 10 ? &r->nc : &r->cache;

		if (row->kind && (row->reservation.base != w->base ||
		    row->reservation.capacity != w->capacity))
			return md_boot_fail(-ERANGE);
	}
	/* Final bank4 view is four NC plus four cache 32MiB pages. LK first
	 * programs eight NC entries then overwrites4..7 with cache while MD OFF.
	 * Entire final mappings, not only used service bytes, must remain owned.
	 */
	if (r->nc.capacity < 0x8000000ULL || r->cache.capacity < 0x8000000ULL ||
	    boot.input.rows.smem.nc_capacity > 0x8000000ULL ||
	    boot.input.rows.smem.cache_capacity > 0x8000000ULL ||
	    r->nc.base > 0x800000000ULL - 0x10000000ULL ||
	    r->cache.base > 0x800000000ULL - 0x8000000ULL)
		return md_boot_fail(-ERANGE);
	return 0;
}

static int md_boot_smem_remap(void)
{
	/* ATF 0x1be10..0x1bf40: command3 x2/x3=physical low/high,
	 * x4=index, x0=status, x1=actual 32-bit register after RMW.
	 */
	static const unsigned int shift[8] = { 20, 0, 10, 20, 0, 10, 20, 0 };
	static const unsigned int reg[8] = { 0, 1, 1, 1, 2, 2, 2, 3 };
	unsigned int known_mask[4] = { 0 }, known_value[4] = { 0 };
	unsigned int call;

	for (call = 0; call < 12; call++) {
		struct arm_smccc_res reply = { 0 };
		unsigned int index = call < 8 ? call : call - 4;
		unsigned long long base = call < 8 ? boot.input.resources.nc.base :
			boot.input.resources.cache.base;
		unsigned long long address = base + (unsigned long long)
			(call < 8 ? call : call - 8) * 0x2000000ULL;
		unsigned int mask = 0x3ffU << shift[index];
		unsigned int expected = (unsigned int)((address >> 25) & 0x3ff) << shift[index];
		unsigned int word = reg[index];

		boot.report.bank_call = call;
		boot.report.bank_index = index;
		arm_smccc_smc(0xc200040bU, 3, (unsigned int)address,
			(unsigned int)(address >> 32), index, 0, 0, 0, &reply);
		boot.report.bank_reply[call][0] = reply.a0;
		boot.report.bank_reply[call][1] = reply.a1;
		boot.report.bank_reply[call][2] = reply.a2;
		boot.report.bank_reply[call][3] = reply.a3;
		memcpy(boot.report.reply, boot.report.bank_reply[call], sizeof(boot.report.reply));
		if (reply.a0) {
			unsigned long long status = reply.a0;

			if (status >= 0xfffff001ULL && status <= 0xffffffffULL)
				return md_boot_fail(-(int)(0x100000000ULL - status));
			if (status >= ~0ULL - 4094)
				return md_boot_fail(-(int)(~status + 1));
			return md_boot_fail(-EPROTO);
		}
		known_mask[word] |= mask;
		known_value[word] = (known_value[word] & ~mask) | expected;
		if (reply.a1 > 0xffffffffULL ||
		    ((unsigned int)reply.a1 & known_mask[word]) != known_value[word])
			return md_boot_fail(-EIO);
	}
	return 0;
}

static int md_boot_ccci(unsigned int request, unsigned int command)
{
	struct arm_smccc_res reply = { 0 };
	unsigned long long status;

	arm_smccc_smc(0xc200040bU, request, command, 0, 0, 0, 0, 0, &reply);
	boot.report.reply[0] = reply.a0;
	boot.report.reply[1] = reply.a1;
	boot.report.reply[2] = reply.a2;
	boot.report.reply[3] = reply.a3;
	status = reply.a0;
	if (!status)
		return 0;
	if (status >= 0xfffff001ULL && status <= 0xffffffffULL)
		return md_boot_fail(-(int)(0x100000000ULL - status));
	if (status >= ~0ULL - 4094)
		return md_boot_fail(-(int)(~status + 1));
	return md_boot_fail(-EPROTO);
}

int tetris_modem_bootstrap_once(struct blk_desc *dev,
		const struct tetris_modem_bootstrap_plan *plan)
{
	struct tetris_modem_boot_secure *secure;
	struct tetris_modem_boot_secure_report report;
	unsigned int i, value;
	int ret;

	if (boot.attempted)
		return boot.report.error ? boot.report.error : -EALREADY;
	boot.attempted = 1;
	boot.report.stage = TETRIS_MD_BOOT_INPUT;
	if (!dev || !plan)
		return md_boot_fail(-EINVAL);
	if (current_el() != 1 && current_el() != 2)
		return md_boot_fail(-EPERM);
	/* Copy bounded inputs before the first SMC/MMIO operation. */
	boot.input = *plan;
	ret = md_boot_plan();
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_PROFILE;
	ret = tetris_modem_boot_secure_open(dev, &secure);
	if (ret)
		return md_boot_fail(ret);
	boot.report.stage = TETRIS_MD_BOOT_COLD_OFF;
	ret = md_boot_cold_off();
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_EMI;
	ret = tetris_modem_emi_rows_program(&boot.input.rows, &tetris_modem_emi_rows_hw_ops,
		&boot.report.emi);
	boot.report.slot = boot.report.emi.slot;
	if (ret)
		return md_boot_fail(ret);
	boot.report.stage = TETRIS_MD_BOOT_REMAP;
	boot.report.slot = 0;
	ret = tetris_modem_boot_secure_remap(secure, boot.input.resources.firmware.base,
		boot.input.resources.firmware.capacity, boot.input.resources.dram_base,
		boot.input.resources.dram_size);
	if (ret)
		goto secure_failure;
	boot.report.stage = TETRIS_MD_BOOT_SMEM_REMAP;
	ret = md_boot_smem_remap();
	if (ret)
		return ret;
	/* LK 0x2542c: request8 closes further bootloader remap programming.
	 * This is top-level CCCI/8, NOT the no-op POWER_CONFIG subcommand8.
	 */
	boot.report.stage = TETRIS_MD_BOOT_REMAP_LOCK;
	ret = md_boot_ccci(8, 0);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_CLOCK;
	value = md_boot_read(MD_TOP);
	boot.mutated = 1;
	writel(value & ~MD_CLOCK, (volatile void *)MD_TOP);
	ret = md_boot_wait(MD_TOP, MD_CLOCK, 0);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_ISOLATION;
	value = md_boot_read(MD_EXTISO);
	writel(value & ~3U, (volatile void *)MD_EXTISO);
	ret = md_boot_wait(MD_EXTISO, 3, 0);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_POWER;
	value = md_boot_read(MD_POWER);
	writel(value | MD_ON, (volatile void *)MD_POWER);
	ret = md_boot_wait(MD_POWER, MD_ON | MD_ACK, MD_ON | MD_ACK);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_BUS_NEMI;
	writel(0xc0, (volatile void *)MD_NEMI_CLR);
	ret = md_boot_wait(MD_NEMI_STA, 0xc0, 0);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_BUS_IFR11;
	writel(0x800, (volatile void *)MD_IFR11_CLR);
	ret = md_boot_wait(MD_IFR11_STA, 0x800, 0);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_BUS_IFR9;
	writel(0x200, (volatile void *)MD_IFR9_CLR);
	ret = md_boot_wait(MD_IFR9_STA, 0x200, 0);
	if (ret)
		return ret;
	boot.report.stage = TETRIS_MD_BOOT_RELEASE;
	ret = md_boot_ccci(6, 1);
	if (ret)
		return ret;
	if (boot.report.reply[3] != 1)
		return md_boot_fail(-EIO);
	boot.report.stage = TETRIS_MD_BOOT_BROM;
	for (i = 0; i < MD_BROM_COUNT; i++) {
		boot.report.polls = i + 1;
		ret = md_boot_ccci(6, 7);
		if (ret)
			return ret;
		/* LK POWER/7 packs flags0/1 into x2 and flags2/3 into x3.
		 * This is exactly kernel POWER/3's four-equal-one predicate.
		 */
		if (boot.report.reply[2] == 0x100000001ULL &&
		    boot.report.reply[3] == 0x100000001ULL) {
			boot.report.stage = TETRIS_MD_BOOT_COMPLETE;
			return 0;
		}
		if (i + 1 < MD_BROM_COUNT)
			mdelay(20);
	}
	return md_boot_fail(-ETIMEDOUT);

secure_failure:
	if (!tetris_modem_boot_secure_report(secure, &report))
		memcpy(boot.report.reply, report.reply, sizeof(report.reply));
	return md_boot_fail(ret);
}

int tetris_modem_bootstrap_report(struct tetris_modem_bootstrap_report *out)
{
	if (!out || !boot.attempted)
		return -EINVAL;
	*out = boot.report;
	return 0;
}
