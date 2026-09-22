// SPDX-License-Identifier: GPL-2.0+
#define TETRIS_SCP_HANDOFF_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define barrier() __asm__ __volatile__("" : : : "memory")
#include "../../board/mediatek/mt6878/tetris_scp_tcm.c"

static u32 memory[TCM_SIZE / 4];
static u64 addresses[484];
static u32 values[484];
static unsigned int writes, reads, barriers;
static bool corrupt;

static void write_word(u64 address, u32 value)
{
	if (writes < 484) {
		addresses[writes] = address;
		values[writes] = value;
	} else {
		assert(address >= TCM && address < TCM + TCM_SIZE);
		assert(!(address & 3));
		memory[(address - TCM) / 4] = value;
	}
	writes++;
}

static u32 read_word(u64 address)
{
	reads++;
	if (address == 0x1cb50234)
		return 0xa5a50005;
	assert(address >= TCM && address < TCM + LOADER_SIZE);
	return memory[(address - TCM) / 4] ^ (corrupt ? 1 : 0);
}

static void sync_writes(void) { barriers++; }

int main(void)
{
	static const u32 order[] = { 0xc0, 0xc4, 0xc8, 0xcc, 0xd0, 0xd4,
		0xd8, 0x80, 0x84, 0x2c, 0xd8, 0x88, 0x8c, 0x90, 0x94 };
	const struct tetris_scp_tcm_ops ops = { read_word, write_word, sync_writes };
	u8 core[LOADER_SIZE];
	u32 i, j;

	memset(core, 0x5a, sizeof(core));
	memset(core + 32, 0, 4);
	core[32] = 60;
	assert(tetris_scp_tcm_prepare(core, 0x11c730, 0xb8000000, 0x2300000,
				      0x924740, 7, &ops) == 0);
	assert(writes == 484 + TCM_SIZE / 4 + LOADER_SIZE / 4);
	assert(reads == 1 + LOADER_SIZE / 4 && barriers == 2);
	assert(addresses[0] == 0x1cb30004 && values[0] == 1);
	assert(addresses[1] == 0x1cb40004 && values[1] == 1);
	assert(addresses[2] == 0x1cb21000 && values[2] == 3);
	assert(addresses[3] == 0x1cb50234 && values[3] == 0xa5a54005);
	for (i = 0; i < 15; i++)
		for (j = 0; j < 32; j++) {
			assert(addresses[4 + i * 32 + j] == 0x1cb21000 + order[i]);
			assert(values[4 + i * 32 + j] == (1U << (31 - j)) - 1);
		}
	assert(memory[0] == 0x5a5a5a5a);
	assert(memory[1] == 0xb8000000 && memory[2] == 8192);
	assert(memory[3] == 0xb8002000 && memory[4] == TCM_SIZE);
	assert(memory[5] == 0xb8700000 && memory[6] == 0x924740);
	assert(memory[7] == 0xb9024800 && memory[8] == 60);
	assert(memory[11] == 7 && memory[15] == 0x5a5a5a5a);
	for (i = LOADER_SIZE / 4; i < TCM_SIZE / 4; i++)
		assert(memory[i] == 0);

	writes = reads = barriers = 0;
	assert(tetris_scp_tcm_prepare(core, 8191, 0xb8000000, 0x2300000,
				      0x924740, 0, &ops) < 0);
	assert(tetris_scp_tcm_prepare(core, 0x11c730, 0xb8000001, 0x2300000,
				      0x924740, 0, &ops) < 0);
	assert(tetris_scp_tcm_prepare(core, 0x11c730, 0xff000000, 0x2300000,
				      0x924740, 0, &ops) < 0);
	assert(tetris_scp_tcm_prepare(core, 0x11c730, 0xb8000000, 0x1000000,
				      0x924740, 0, &ops) < 0);
	core[32] = 56;
	assert(tetris_scp_tcm_prepare(core, 0x11c730, 0xb8000000, 0x2300000,
				      0x924740, 0, &ops) < 0);
	assert(!writes && !reads && !barriers);
	core[32] = 60;
	corrupt = true;
	assert(tetris_scp_tcm_prepare(core, 0x11c730, 0xb8000000, 0x2300000,
				      0x924740, 0, &ops) == -EIO);
	puts("SCP TCM sequence/readback/bounds: PASS (mock MMIO, no hardware)");
	return 0;
}
