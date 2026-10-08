// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#else
#include <linux/errno.h>
#endif
#include "tetris_modem_layout.h"
#include "tetris_modem_remap.h"

static int matches(unsigned long long actual, unsigned int expected,
		   unsigned int mask)
{
	return actual <= 0xffffffffULL && (actual & mask) == (expected & mask);
}

int tetris_modem_program_remap(unsigned long long base,
			       unsigned long long capacity,
			       unsigned long long dram_base,
			       unsigned long long dram_size,
			       const struct tetris_modem_remap_ops *ops,
			       struct tetris_modem_remap_transaction *transaction)
{
	struct tetris_modem_remap plan;
	unsigned long long *reply;
	unsigned int operation, i;
	int ret;

	if (!transaction || !ops || !ops->smc)
		return -EINVAL;
	if (transaction->state != TETRIS_MODEM_REMAP_FRESH)
		return -EALREADY;
	ret = tetris_modem_plan_remap(base, capacity, dram_base, dram_size, &plan);
	if (ret)
		return ret;
	transaction->state = TETRIS_MODEM_REMAP_ATTEMPTED;
	for (operation = 1; operation <= 2; operation++) {
		transaction->operation = operation;
		reply = transaction->reply[operation - 1];
		/* Missing callback outputs must fail, not resemble a zero register. */
		for (i = 0; i < 4; i++)
			reply[i] = ~0ULL;
		/* LK CCCI remap ABI; the kernel CCCI interface is different. */
		ret = ops->smc(ops->context, 0xc200040bU, operation,
			       (unsigned int)base, (unsigned int)(base >> 32), reply);
		if (ret)
			goto failed;
		ret = -EIO;
		if (operation == 1) {
			/* Command 1 owns only the low twenty bits of shared register 2. */
			if (reply[0] || !matches(reply[1], plan.value[0], plan.mask[0]) ||
			    !matches(reply[2], plan.value[1], plan.mask[1]) ||
			    !matches(reply[3], plan.value[2], 0xfffffU))
				goto failed;
		} else {
			/* Command 2 returns shared register 2 in x0, not a status. */
			for (i = 0; i < 4; i++)
				if (!matches(reply[i], plan.value[i + 2], plan.mask[i + 2]))
					goto failed;
		}
	}
	transaction->state = TETRIS_MODEM_REMAP_VERIFIED;
	return 0;

failed:
	transaction->state = TETRIS_MODEM_REMAP_FAILED;
	return ret < 0 ? ret : -EIO;
}
