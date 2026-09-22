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
static void sync_writes(void) { assert(calls == 17); }
static const struct tetris_scp_secure_ops ops = { smc, write_word, sync_writes };

static int plan(struct tetris_scp_secure_plan *p)
{
	const u32 sizes[] = { 0x180000, 0x200000, 0x5c0000, 0x80000 };
	int ret = tetris_scp_secure_plan(p, 0xb8000000, 0x2300000, 0x8e000000,
				       0x11c8000, 0x924740, table, 6, dumps);
	return ret ? ret : tetris_scp_secure_audio_plan(p, 0x98000000, 0x9c0000, sizes);
}

static void test_audio_bounds(void)
{
	struct tetris_scp_secure_plan p;
	u32 sizes[] = { 0x180000, 0x200000, 0x5c0000, 0x80000 };
	const u64 rejected[] = { 0, 0x3f000000, 0x98001000, 0xa0000000,
		0x8e000000, 0x8f000000, 0xb8000000, ~0ULL };
	unsigned int i;

	assert(!tetris_scp_secure_plan(&p, 0xb8000000, 0x2300000, 0x8e000000,
		0x11c8000, 0x924740, table, 6, dumps));
	assert(tetris_scp_secure_begin(&p, &ops) == -EINVAL && !calls);
	for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
		assert(tetris_scp_secure_audio_plan(&p, rejected[i], 0x9c0000, sizes) < 0 && !p.audio);
	assert(tetris_scp_secure_audio_plan(&p, 0x98000000, 0x9b0000, sizes) < 0);
	assert(tetris_scp_secure_audio_plan(&p, 0x98000000, 0x9d0000, sizes) < 0);
	assert(tetris_scp_secure_audio_plan(&p, 0x98000000, ~0ULL, sizes) < 0);
	sizes[2] = 0;
	assert(tetris_scp_secure_audio_plan(&p, 0x98000000, 0x9c0000, sizes) < 0);
	sizes[2] = ~0U;
	assert(tetris_scp_secure_audio_plan(&p, 0x98000000, 0x9c0000, sizes) < 0);
	sizes[2] = 0x5c0000;
	assert(!tetris_scp_secure_audio_plan(&p, 0x96000000, 0x9c0000, sizes));
	assert(p.audio == 0x96000000);
	assert(tetris_scp_secure_audio_plan(&p, 0x97000000, 0x9c0000, sizes) < 0);
	assert(p.audio == 0x96000000);
}

int main(void)
{
	struct tetris_scp_secure_plan p;
	u32 invalid[] = { 0, 0, 0, 0, 0x1000, 0 };
	unsigned int i;

	test_audio_bounds();
	assert(!plan(&p));
	assert(p.dump_size == 0xe28000 && p.shared_size == 0x11c8000);
	assert(p.offsets[0] == 0 && p.offsets[2] == 0xe28000);
	assert(p.dumps[5] == p.dump_size);
	assert(tetris_scp_secure_finish(&p, &ops) == -EINVAL && !calls);
	assert(!tetris_scp_secure_begin(&p, &ops));
	assert(calls == 15 && !writes && p.state == 2);
	for (i = 0; i < 4; i++)
		assert(args[i][0] == AUDIO_BOOT && args[i][1] == 1 &&
		       args[i][2] == i && args[i][3] == p.audio_offsets[i] &&
		       args[i][4] == p.audio_sizes[i]);
	assert(p.audio_offsets[2] == 0x380000 && p.audio_offsets[3] == 0x940000);
	assert(args[4][0] == SCP_BOOT && args[4][1] == 8 && args[4][2] == 5 &&
	       args[4][3] == 0x98000000 && args[4][4] == 0x9c0000);
	assert(args[5][0] == EMI_REGION && args[5][2] == 0x98000 &&
	       args[5][3] == 0x989c0 && args[5][4] == 29);
	assert(args[6][1] == 9 && args[6][2] == 0 && args[6][4] == 0xe28000);
	assert(args[7][1] == 9 && args[7][2] == 2 && args[7][3] == 0xe28000);
	for (i = 0; i < 6; i++)
		assert(args[8 + i][1] == 7 && args[8 + i][2] == i &&
		       args[8 + i][3] == p.dumps[i]);
	assert(args[14][1] == 2 && args[14][2] == 0xb8000000);
	assert(!tetris_scp_secure_finish(&p, &ops));
	assert(calls == 22 && writes == 3 && p.state == 3);
	assert(args[15][1] == 1 && args[15][2] == 0xb8700000 && args[15][3] == 0x924740);
	assert(args[16][1] == 3 && args[17][1] == 4 && args[18][1] == 5);
	assert(args[19][0] == EMI_REGION && args[19][4] == 26);
	assert(args[19][2] == 0xb8000 && args[19][3] == 0xba300);
	assert(args[20][0] == SCP_BOOT && args[20][1] == 0 &&
	       args[20][2] == 0x8e000000 && args[20][3] == 0x11c8000);
	assert(args[21][0] == EMI_REGION && args[21][4] == 27);
	assert(args[21][2] == 0x8e000 && args[21][3] == 0x8f1c8);
	assert(tetris_scp_secure_begin(&p, &ops) < 0 && calls == 22);
	assert(tetris_scp_secure_finish(&p, &ops) < 0 && calls == 22);
	for (i = 1; i <= 22; i++) {
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
