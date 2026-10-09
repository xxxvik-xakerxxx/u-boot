// SPDX-License-Identifier: GPL-2.0+
/* Concrete boot-stage secure transport; no MMIO power or fake READY path. */
#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_scp_security.h"
#include "tetris_modem_boot_secure.h"

struct tetris_modem_boot_secure {
	struct tetris_modem_emi_observation observed[12];
	struct tetris_modem_emi_transaction ranges[12];
	struct tetris_modem_remap_transaction remap;
	struct tetris_modem_boot_secure_report report;
	unsigned int attempted, opened;
};

static struct tetris_modem_boot_secure boot_session;

static int md_secure_latch(struct tetris_modem_boot_secure *session, int error)
{
	if (error && !session->report.error)
		session->report.error = error < 0 ? error : -EPROTO;
	return session->report.error;
}

/* Only use for actual status fields, never packed policy/register readback. */
static int md_secure_status(unsigned long long raw)
{
	if (!raw)
		return 0;
	if (raw >= 0xfffff001ULL && raw <= 0xffffffffULL)
		return -(int)(0x100000000ULL - raw);
	if (raw >= ~0ULL - 4094)
		return -(int)(~raw + 1);
	return -EPROTO;
}

static void md_secure_capture(struct tetris_modem_boot_secure *session,
			      const struct arm_smccc_res *res)
{
	session->report.reply[0] = res->a0;
	session->report.reply[1] = res->a1;
	session->report.reply[2] = res->a2;
	session->report.reply[3] = res->a3;
}

static int md_secure_emi_call(void *context, unsigned int function,
		unsigned int operation, unsigned long long a, unsigned long long b,
		unsigned long long c, unsigned long long *reply)
{
	struct tetris_modem_boot_secure *session = context;
	struct arm_smccc_res res = { 0 };

	if (function != 0xc2000415U || !reply ||
	    !((operation == 0 && c >= 32 && c <= 43) ||
	      (operation == 2 && b >= 32 && b <= 43 &&
	       ((a <= 1 && !c) || (a == 3 && !c) || (a == 4 && c < 8)))))
		return -EINVAL;
	session->report.operation = operation;
	arm_smccc_smc(function, operation, a, b, c, 0, 0, 0, &res);
	md_secure_capture(session, &res);
	*reply = res.a0;
	if (!operation)
		return md_secure_status(res.a0);
	/* Enable accepts only 0/1; all-ones packed policy is legitimate data. */
	if (a == 3 && res.a0 > 1)
		return md_secure_status(res.a0);
	return 0;
}

static int md_secure_remap_call(void *context, unsigned int function,
		unsigned int operation, unsigned int low, unsigned int high,
		unsigned long long reply[4])
{
	struct tetris_modem_boot_secure *session = context;
	struct arm_smccc_res res = { 0 };

	if (function != 0xc200040bU || (operation != 1 && operation != 2) || !reply)
		return -EINVAL;
	session->report.operation = operation;
	arm_smccc_smc(function, operation, low, high, 0, 0, 0, 0, &res);
	md_secure_capture(session, &res);
	memcpy(reply, session->report.reply, sizeof(session->report.reply));
	/* Command2 x0 is register DATA, not an errno or zero-only success code. */
	return operation == 1 ? md_secure_status(res.a0) : 0;
}

static int md_secure_session(const struct tetris_modem_boot_secure *session)
{
	if (session != &boot_session || !session->opened)
		return -EINVAL;
	return session->report.error;
}

int tetris_modem_boot_secure_open(struct blk_desc *dev,
				struct tetris_modem_boot_secure **out)
{
	int ret;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (!dev)
		return -EINVAL;
	if (boot_session.attempted)
		return boot_session.report.error ? boot_session.report.error : -EALREADY;
	boot_session.attempted = 1;
	boot_session.report.stage = TETRIS_MD_SECURE_PROFILE;
	ret = tetris_scp_check_atf_profile(dev);
	if (ret)
		return md_secure_latch(&boot_session, ret);
	boot_session.opened = 1;
	*out = &boot_session;
	return 0;
}

int tetris_modem_boot_secure_observe(struct tetris_modem_boot_secure *session,
		unsigned int slot, struct tetris_modem_emi_observation *out)
{
	struct tetris_modem_emi_ops ops = { .smc = md_secure_emi_call, .context = session };
	int ret = md_secure_session(session);

	if (ret)
		return ret;
	if (!out || slot < 32 || slot > 43)
		return md_secure_latch(session, -EINVAL);
	session->report.stage = TETRIS_MD_SECURE_OBSERVE;
	session->report.slot = slot;
	ret = tetris_modem_read_emi_slot(slot, &ops, &session->observed[slot - 32]);
	if (ret)
		return md_secure_latch(session, ret);
	*out = session->observed[slot - 32];
	return 0;
}

int tetris_modem_boot_secure_range(struct tetris_modem_boot_secure *session,
		unsigned long long start, unsigned long long size, unsigned int slot,
		unsigned long long reserved_base, unsigned long long reserved_size,
		const unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS])
{
	struct tetris_modem_emi_ops ops = { .smc = md_secure_emi_call, .context = session };
	int ret = md_secure_session(session);

	if (ret)
		return ret;
	if (slot < 32 || slot > 43)
		return md_secure_latch(session, -EINVAL);
	session->report.stage = TETRIS_MD_SECURE_EMI;
	session->report.slot = slot;
	ret = tetris_modem_program_emi_range(start, size, slot, reserved_base,
		reserved_size, policy, &ops, &session->ranges[slot - 32]);
	return ret ? md_secure_latch(session, ret) : 0;
}

int tetris_modem_boot_secure_remap(struct tetris_modem_boot_secure *session,
		unsigned long long base, unsigned long long capacity,
		unsigned long long dram_base, unsigned long long dram_size)
{
	struct tetris_modem_remap_ops ops = { .smc = md_secure_remap_call, .context = session };
	int ret = md_secure_session(session);

	if (ret)
		return ret;
	session->report.stage = TETRIS_MD_SECURE_REMAP;
	session->report.slot = 0;
	ret = tetris_modem_program_remap(base, capacity, dram_base, dram_size, &ops, &session->remap);
	return ret ? md_secure_latch(session, ret) : 0;
}

int tetris_modem_boot_secure_report(const struct tetris_modem_boot_secure *session,
				  struct tetris_modem_boot_secure_report *out)
{
	if (session != &boot_session || !session->attempted || !out)
		return -EINVAL;
	*out = session->report;
	return 0;
}
