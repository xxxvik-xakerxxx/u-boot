// SPDX-License-Identifier: GPL-2.0+
#define TETRIS_SCP_HANDOFF_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include "../../board/mediatek/mt6878/tetris_scp_secure.c"

static unsigned int calls, writes, fail_at;
static u64 args[32][5];
static const u32 table[] = { 2, 0x100000, 0, 0, 0, 0 };
static const u32 dumps[] = { 0x100000, 0xc4000, 0x3c00, 0x400, 0xc60000 };

static u64 smc(u32 fn, u64 op, u64 a, u64 b, u64 c)
{
	u64 record[] = { fn, op, a, b, c };
	assert(calls < 32);
	memcpy(args[calls], record, sizeof(record));
	return ++calls == fail_at ? 0xfffffffcu : 0;
}

static void write_word(u64 address, u32 value)
{
	static const u32 values[] = { 0x33343043, 3, 0 };
	assert(writes < 3 && address == 0x1cb2a080 + 4 * writes);
	assert(value == values[writes++]);
}
static void sync_writes(void) { assert(calls == 11); }
static const struct tetris_scp_secure_ops ops = { smc, write_word, sync_writes };

static int plan(struct tetris_scp_secure_plan *p)
{
	return tetris_scp_secure_plan(p, 0xb8000000, 0x2300000, 0x8e000000,
				      0x11c8000, 0x924740, table, 6, dumps);
}

int main(void)
{
	struct tetris_scp_secure_plan p;
	u32 invalid[] = { 0, 0, 0, 0, 0x1000, 0 };
	unsigned int i;

	assert(!plan(&p));
	assert(p.dump_size == 0xe28000 && p.shared_size == 0x11c8000);
	assert(p.offsets[0] == 0 && p.offsets[2] == 0xe28000);
	assert(p.dumps[5] == p.dump_size);
	assert(tetris_scp_secure_finish(&p, &ops) == -EINVAL && !calls);
	assert(!tetris_scp_secure_begin(&p, &ops));
	assert(calls == 9 && !writes && p.state == 2);
	assert(args[0][1] == 9 && args[0][2] == 0 && args[0][4] == 0xe28000);
	assert(args[1][1] == 9 && args[1][2] == 2 && args[1][3] == 0xe28000);
	for (i = 0; i < 6; i++)
		assert(args[2 + i][1] == 7 && args[2 + i][2] == i &&
		       args[2 + i][3] == p.dumps[i]);
	assert(args[8][1] == 2 && args[8][2] == 0xb8000000);
	assert(!tetris_scp_secure_finish(&p, &ops));
	assert(calls == 16 && writes == 3 && p.state == 3);
	assert(args[9][1] == 1 && args[9][2] == 0xb8700000 && args[9][3] == 0x924740);
	assert(args[10][1] == 3 && args[11][1] == 4 && args[12][1] == 5);
	assert(args[13][0] == EMI_REGION && args[13][4] == 26);
	assert(args[13][2] == 0xb8000 && args[13][3] == 0xba300);
	assert(args[14][0] == SCP_BOOT && args[14][1] == 0 &&
	       args[14][2] == 0x8e000000 && args[14][3] == 0x11c8000);
	assert(args[15][0] == EMI_REGION && args[15][4] == 27);
	assert(args[15][2] == 0x8e000 && args[15][3] == 0x8f1c8);
	assert(tetris_scp_secure_begin(&p, &ops) < 0 && calls == 16);
	assert(tetris_scp_secure_finish(&p, &ops) < 0 && calls == 16);
	for (i = 1; i <= 16; i++) {
		int ret;
		calls = writes = 0;
		fail_at = i;
		assert(!plan(&p));
		ret = tetris_scp_secure_begin(&p, &ops);
		if (!ret)
			ret = tetris_scp_secure_finish(&p, &ops);
		assert(ret == -EIO && calls == i && p.state == 4);
		assert(p.secure_error == 0xfffffffcu);
		assert(tetris_scp_secure_begin(&p, &ops) < 0 && calls == i);
		assert(tetris_scp_secure_finish(&p, &ops) < 0 && calls == i);
	}
	assert(tetris_scp_secure_plan(&p, 0xb8000000, 0x2300000, 0x8e000000,
		0x1000000, 0x924740, table, 6, dumps) < 0 && !p.state);
	assert(tetris_scp_secure_plan(&p, 0xb8000000, 0x2300000, 0x8e000000,
		0x11c8000, 0x924740, invalid, 6, dumps) < 0 && !p.state);
	assert(tetris_scp_secure_plan(&p, 0xb8000000, 0x2300000, 0x90000000,
		0x11c8000, 0x924740, table, 6, dumps) < 0 && !p.state);
	puts("SCP secure handoff layout/sequence/first-error: PASS (mock SMC)");
	return 0;
}
