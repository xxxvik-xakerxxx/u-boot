// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#else
#include <linux/errno.h>
#endif
#include "tetris_modem_layout.h"
#include "tetris_modem_emi.h"

int tetris_modem_program_emi_range(unsigned long long start,
		unsigned long long size, unsigned int slot,
		unsigned long long reserved_base, unsigned long long reserved_size,
		const unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS],
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_transaction *transaction)
{
	struct tetris_modem_emi_range plan;
	unsigned long long expected_policy[TETRIS_MODEM_EMI_POLICY_WORDS];
	unsigned long long a, b, c, expected;
	unsigned int step, operation;
	int ret;

	if (!transaction || !ops || !ops->smc || !policy)
		return -EINVAL;
	if (transaction->state != TETRIS_MODEM_EMI_FRESH)
		return -EALREADY;
	if (!reserved_base || !reserved_size ||
	    reserved_base > ~0ULL - reserved_size || start < reserved_base ||
	    start - reserved_base >= reserved_size ||
	    size > reserved_size - (start - reserved_base))
		return -ERANGE;
	ret = tetris_modem_plan_emi(start, size, slot, &plan);
	if (ret)
		return ret;
	for (step = 0; step < TETRIS_MODEM_EMI_POLICY_WORDS; step++)
		expected_policy[step] = policy[step];

	transaction->state = TETRIS_MODEM_EMI_ATTEMPTED;
	transaction->slot = slot;
	for (step = 0; step < TETRIS_MODEM_EMI_STEPS; step++) {
		/* Read enable/policy, program once, then verify range/enable/policy. */
		operation = 2;
		a = 3;
		b = slot;
		c = 0;
		expected = 0;
		switch (step) {
		case 9:
			operation = 0;
			a = plan.start_page;
			b = plan.end_page;
			c = slot;
			break;
		case 10:
			a = 0;
			expected = plan.start_readback;
			break;
		case 11:
			a = 1;
			expected = plan.end_readback;
			break;
		case 12:
			expected = 1;
			break;
		default:
			if (step) {
				a = 4;
				c = step < 9 ? step - 1 : step - 13;
				expected = expected_policy[c];
			}
			break;
		}
		transaction->step = step;
		/* All-ones is a valid packed policy, so poison relative to expectation. */
		transaction->reply[step] = ~expected;
		ret = ops->smc(ops->context, 0xc2000415U, operation, a, b, c,
			       &transaction->reply[step]);
		if (ret)
			goto failed;
		if (transaction->reply[step] != expected) {
			ret = step == 0 && transaction->reply[step] == 1 ? -EBUSY : -EIO;
			goto failed;
		}
	}
	transaction->state = TETRIS_MODEM_EMI_RANGE_VERIFIED;
	return 0;

failed:
	transaction->state = TETRIS_MODEM_EMI_FAILED;
	return ret < 0 ? ret : -EIO;
}
