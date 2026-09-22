// SPDX-License-Identifier: GPL-2.0+
/* Boot-only ABI from the pinned LK/ATF pair, not Linux runtime SCP SMCs. */
#include "tetris_scp_secure.h"
#ifdef TETRIS_SCP_HANDOFF_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#endif

#define SCP_BOOT 0xc200040fU
#define EMI_REGION 0x82000415U

int tetris_scp_secure_plan(struct tetris_scp_secure_plan *p,
			   u64 firmware, u64 capacity, u64 shared, u64 shared_size,
			   u32 dram_size, const u32 *table, size_t cells,
			   const u32 dumps[5])
{
	u32 alignment[25] = { 0 }, seen = 0;
	u64 offset = 0, size = 0;
	size_t i;

	if (!p)
		return -EINVAL;
	memset(p, 0, sizeof(*p));
	if (!table || !dumps || !cells || cells > 75 || cells % 3 ||
	    firmware < 0x40000000ULL || firmware >= (1ULL << 32) ||
	    capacity != 0x2300000 || capacity > (1ULL << 32) - firmware ||
	    shared < 0x40000000ULL || shared >= 0x90000000ULL ||
	    shared_size > 0x90000000ULL - shared ||
	    (firmware & 0x7ffffff) || (shared & 0xffffff) ||
	    (firmware < shared + shared_size && shared < firmware + capacity) ||
	    !dram_size || dram_size > 0xe00000 || dumps[0] != 0x100000 ||
	    dumps[4] < dram_size)
		return -EINVAL;
	for (i = 0; i < 5; i++) {
		if (!dumps[i] || (dumps[i] & 3))
			return -EINVAL;
		size += dumps[i];
		if (size > 0x10000000)
			return -ERANGE;
		p->dumps[i + 1] = size;
	}
	if (size + 0x3a0000 > shared_size)
		return -ERANGE;
	p->dump_size = size;
	p->shared_size = size + 0x3a0000;
	for (i = 0; i < cells; i += 3) {
		u32 id = table[i], align = table[i + 2];

		if (id >= 25 || (seen & (1U << id)) ||
		    (align && (align & (align - 1))))
			return -EINVAL;
		seen |= 1U << id;
		p->sizes[id] = id ? table[i + 1] : p->dump_size;
		alignment[id] = align;
	}
	if (!(seen & 1))
		return -EINVAL;
	/* LK packs by feature ID, independently of DT tuple order. */
	for (i = 0; i < 25; i++) {
		if (!p->sizes[i])
			continue;
		if (alignment[i])
			offset = (offset + alignment[i] - 1) & ~((u64)alignment[i] - 1);
		if (offset > p->shared_size || p->sizes[i] > p->shared_size - offset)
			return -ERANGE;
		p->offsets[i] = offset;
		offset += p->sizes[i];
	}
	p->firmware = firmware;
	p->capacity = capacity;
	p->shared = shared;
	p->dram_size = dram_size;
	p->state = 1;
	return 0;
}

static int call(struct tetris_scp_secure_plan *p,
		const struct tetris_scp_secure_ops *ops,
		u32 function, u32 operation, u64 a, u64 b, u64 c)
{
	p->last_function = function;
	p->last_operation = operation;
	p->secure_error = ops->smc(function, operation, a, b, c);
	if (p->secure_error) {
		p->state = 4;
		return -EIO;
	}
	return 0;
}

int tetris_scp_secure_begin(struct tetris_scp_secure_plan *p,
			    const struct tetris_scp_secure_ops *ops)
{
	u32 i;
	int ret;

	if (!p || p->state != 1 || !ops || !ops->smc || !ops->write || !ops->sync)
		return -EINVAL;
	/* Mark attempted before the first call; no reuse after partial success. */
	p->state = 4;
	for (i = 0; i < 25; i++) {
		if (!p->sizes[i])
			continue;
		ret = call(p, ops, SCP_BOOT, 9, i, p->offsets[i], p->sizes[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < 6; i++) {
		ret = call(p, ops, SCP_BOOT, 7, i, p->dumps[i], 0);
		if (ret)
			return ret;
	}
	ret = call(p, ops, SCP_BOOT, 2, p->firmware, p->capacity, 0);
	if (!ret)
		p->state = 2;
	return ret;
}

int tetris_scp_secure_finish(struct tetris_scp_secure_plan *p,
			     const struct tetris_scp_secure_ops *ops)
{
	int ret;

	if (!p || p->state != 2 || !ops || !ops->smc || !ops->write || !ops->sync)
		return -EINVAL;
	p->state = 4;
	ret = call(p, ops, SCP_BOOT, 1, p->firmware + 0x700000, p->dram_size, 0);
	if (!ret)
		ret = call(p, ops, SCP_BOOT, 3, 0, 0, 0);
	if (ret)
		return ret;
	ops->sync();
	ops->write(0x1cb2a080, 0x33343043);
	ops->write(0x1cb2a084, 3);
	ops->write(0x1cb2a088, 0);
	ret = call(p, ops, SCP_BOOT, 4, 0, 0, 0);
	if (!ret)
		ret = call(p, ops, SCP_BOOT, 5, 0, 0, 0);
	if (!ret)
		ret = call(p, ops, EMI_REGION, 0, p->firmware >> 12,
			   (p->firmware + p->capacity) >> 12, 26);
	if (!ret)
		ret = call(p, ops, SCP_BOOT, 0, p->shared, p->shared_size, 0);
	if (!ret)
		ret = call(p, ops, EMI_REGION, 0, p->shared >> 12,
			   (p->shared + p->shared_size) >> 12, 27);
	if (!ret)
		p->state = 3;
	return ret;
}
