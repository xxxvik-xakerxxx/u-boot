// SPDX-License-Identifier: GPL-2.0+
#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_modem_emi_rows.h"

static int status(unsigned long long reply)
{
	if (!reply)
		return 0;
	if (reply >= 0xfffff001ULL && reply <= 0xffffffffULL)
		return -(int)(0x100000000ULL - reply);
	if (reply >= ~0ULL - 4094)
		return -(int)(~reply + 1);
	return -EPROTO;
}

static int hw_call(void *context, unsigned int function, unsigned int operation,
		unsigned long long a, unsigned long long b, unsigned long long c,
		unsigned long long *reply)
{
	struct arm_smccc_res res = { 0 };

	(void)context;
	if (!reply || function != 0xc2000415U ||
	    !((operation == 0 && c >= 32 && c <= 43) ||
	      (operation == 6 && a == 40 && b <= 3 && !c) ||
	      (operation == 2 && b >= 32 && b <= 43 &&
	       ((a <= 1 && !c) || (a == 3 && !c) || (a == 4 && c < 8)))))
		return -EINVAL;
	arm_smccc_smc(function, operation, a, b, c, 0, 0, 0, &res);
	*reply = res.a0;
	if (operation != 2 || (a == 3 && res.a0 > 1))
		return status(res.a0);
	return 0;
}

const struct tetris_modem_emi_ops tetris_modem_emi_rows_hw_ops = {
	.smc = hw_call,
};

static int fail(struct tetris_modem_emi_rows_transaction *tx, int error)
{
	if (!tx->error)
		tx->error = error < 0 ? error : -EPROTO;
	return tx->error;
}

static int aliases(const void *a, size_t na, const void *b, size_t nb)
{
	size_t x = (size_t)a, y = (size_t)b;

	return x > (size_t)-1 - na || y > (size_t)-1 - nb ||
		(x < y + nb && y < x + na);
}

int tetris_modem_emi_rows_program(const struct tetris_modem_emi_rows *rows,
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_rows_transaction *tx)
{
	struct tetris_modem_emi_row input[12];
	static const unsigned int role[12] = { 0, 4, 5, 6, 1, 2, 3, 7, 0, 8, 10, 9 };
	unsigned int i, j;
	int ret;

	if (!tx || !rows || !ops || !ops->smc)
		return -EINVAL;
	if (aliases(tx, sizeof(*tx), rows, sizeof(*rows)) ||
	    aliases(tx, sizeof(*tx), ops, sizeof(*ops)))
		return -EINVAL;
	if (tx->attempted)
		return tx->error ? tx->error : -EALREADY;
	tx->attempted = 1;
	/* Freeze bounded controls, not firmware buffers, before any secure call. */
	memcpy(input, rows->row, sizeof(input));
	for (i = 0; i < 12; i++) {
		const struct tetris_modem_emi_row *r = &input[i];
		struct tetris_modem_emi_range encoded;
		unsigned long long policy[8];

		tx->slot = i + 32;
		if (!r->kind) {
			if (r->start || r->size)
				return fail(tx, -EINVAL);
			continue;
		}
		if (r->slot != i + 32 ||
		    (i == 8 ? r->kind != TETRIS_MD_EMI_PRESET_RANGE || r->role > 3 :
		     r->kind != TETRIS_MD_EMI_RANGE || r->role != role[i]) ||
		    !r->reservation.base || !r->reservation.capacity ||
		    r->reservation.base > ~0ULL - r->reservation.capacity ||
		    r->start < r->reservation.base ||
		    r->start - r->reservation.base >= r->reservation.capacity ||
		    r->size > r->reservation.capacity - (r->start - r->reservation.base))
			return fail(tx, -EINVAL);
		ret = tetris_modem_plan_emi(r->start, r->size, r->slot, &encoded);
		if (!ret)
			ret = tetris_modem_emi_row_policy_b41(r->slot, r->role, policy);
		if (ret)
			return fail(tx, ret);
		if (memcmp(policy, r->policy, sizeof(policy)))
			return fail(tx, -EKEYREJECTED);
	}
	if (input[0].kind != TETRIS_MD_EMI_RANGE)
		return fail(tx, -EINVAL);
	for (i = 0; i < 12; i++) {
		const struct tetris_modem_emi_row *r = &input[i];

		if (!r->kind)
			continue;
		tx->slot = r->slot;
		if (r->kind == TETRIS_MD_EMI_PRESET_RANGE) {
			tx->operation = 2;
			ret = tetris_modem_read_emi_slot(40, ops, &tx->padding);
			if (ret)
				return fail(tx, ret);
			if (tx->padding.words[0])
				return fail(tx, -EBUSY);
			for (j = 0; j < 8; j++)
				if (tx->padding.words[j + 3] & ~r->policy[j])
					return fail(tx, -EKEYREJECTED);
			tx->operation = 6;
			tx->reply = ~0ULL;
			ret = ops->smc(ops->context, 0xc2000415U, 6, 40, r->role, 0, &tx->reply);
			if (!ret)
				ret = status(tx->reply);
			if (ret)
				return fail(tx, ret);
			/* Existing range helper reads and verifies every resulting policy
			 * word BEFORE command0; no assumed-success permission flags.
			 */
		}
		tx->operation = 0;
		ret = tetris_modem_program_emi_range(r->start, r->size, r->slot,
			r->reservation.base, r->reservation.capacity, r->policy,
			ops, &tx->ranges[i]);
		if (ret)
			return fail(tx, ret);
	}
	return 0;
}
