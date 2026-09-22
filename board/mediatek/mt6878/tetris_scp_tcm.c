// SPDX-License-Identifier: GPL-2.0+
/* Pinned MT6878 LK sequence; no secure registration or reset release. */
#include "tetris_scp_tcm.h"
#ifdef TETRIS_SCP_HANDOFF_HOST_TEST
#include <errno.h>
#else
#include <linux/errno.h>
#endif

#define TCM 0x1c400000ULL
#define TCM_SIZE 0x100000U
#define LOADER_SIZE 0x2000U
#define CORE_CAPACITY 0x700000U

static u32 le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
		((u32)p[3] << 24);
}

static u32 loader_word(const u8 *core, u32 offset, u32 firmware,
		       u32 core_size, u32 dram_size, u32 scpctl)
{
	switch (offset) {
	case 4: return firmware;
	case 8: return LOADER_SIZE;
	case 12: return firmware + LOADER_SIZE;
	case 16: return core_size < TCM_SIZE ? core_size : TCM_SIZE;
	case 20: return firmware + CORE_CAPACITY;
	case 24: return dram_size;
	case 28: return firmware + CORE_CAPACITY + ((dram_size + 1023) & ~1023U);
	case 44: return scpctl;
	default: return le32(core + offset);
	}
}

int tetris_scp_tcm_prepare(const u8 *core, u32 core_size, u64 firmware,
			   u64 capacity, u32 dram_size, u32 scpctl,
			   const struct tetris_scp_tcm_ops *ops)
{
	static const u32 offsets[] = {
		0xc0, 0xc4, 0xc8, 0xcc, 0xd0, 0xd4, 0xd8, 0x80,
		0x84, 0x2c, 0xd8, 0x88, 0x8c, 0x90, 0x94,
	};
	u64 required;
	u32 i, j, word;

	if (!core || !ops || !ops->read || !ops->write || !ops->barrier ||
	    core_size < LOADER_SIZE || core_size > CORE_CAPACITY ||
	    !dram_size || dram_size > 0xe00000U || (dram_size & 15) ||
	    firmware < 0x40000000ULL || firmware >= (1ULL << 32) ||
	    (firmware & 4095) || capacity > (1ULL << 32) - firmware)
		return -EINVAL;
	required = CORE_CAPACITY + (u64)((dram_size + 1023) & ~1023U) + dram_size;
	/* Only the decrypted 60-byte ABI observed on the pinned profile. */
	if (required > capacity || le32(core + 32) != 60)
		return -ERANGE;

	/* LK 0x2c0a4..0x2cfc8, including the second write group for 0xd8. */
	ops->write(0x1cb30004, 1);
	ops->write(0x1cb40004, 1);
	ops->write(0x1cb21000, 3);
	word = ops->read(0x1cb50234);
	ops->write(0x1cb50234, word | (1U << 14));
	for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
		for (j = 32; j; j--)
			ops->write(0x1cb21000 + offsets[i], (1U << (j - 1)) - 1);
	ops->barrier();
	/* Device memory: use aligned word accesses, never ordinary memset/memcpy. */
	for (i = 0; i < TCM_SIZE; i += 4)
		ops->write(TCM + i, 0);
	for (i = 0; i < LOADER_SIZE; i += 4)
		ops->write(TCM + i, loader_word(core, i, firmware, core_size,
						  dram_size, scpctl));
	ops->barrier();
	for (i = 0; i < LOADER_SIZE; i += 4)
		if (ops->read(TCM + i) != loader_word(core, i, firmware, core_size,
							 dram_size, scpctl))
			return -EIO;
	return 0;
}
