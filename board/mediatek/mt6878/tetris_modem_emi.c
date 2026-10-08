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
		const struct tetris_modem_emi_ops *ops,
		struct tetris_modem_emi_transaction *transaction)
{
	struct tetris_modem_emi_range plan;
	unsigned long long a, b, c, expected;
	unsigned int step, operation;
	int ret;

	if (!transaction || !ops || !ops->smc)
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

	transaction->state = TETRIS_MODEM_EMI_ATTEMPTED;
	transaction->slot = slot;
	for (step = 0; step < 5; step++) {
		/* Read enable, program once, then read start, raw end and enable. */
		operation = 2;
		a = 3;
		b = slot;
		c = 0;
		expected = 0;
		switch (step) {
		case 1:
			operation = 0;
			a = plan.start_page;
			b = plan.end_page;
			c = slot;
			break;
		case 2:
			a = 0;
			expected = plan.start_readback;
			break;
		case 3:
			a = 1;
			expected = plan.end_readback;
			break;
		case 4:
			expected = 1;
			break;
		}
		transaction->step = step;
		transaction->reply[step] = ~0ULL;
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
