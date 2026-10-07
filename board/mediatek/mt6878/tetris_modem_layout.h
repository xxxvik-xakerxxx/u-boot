/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_LAYOUT_H
#define __TETRIS_MODEM_LAYOUT_H

#include <stddef.h>

struct tetris_modem_layout {
	unsigned int memory_size;
	unsigned int logical_image_size;
	unsigned int rom_size;
	unsigned int dsp_offset;
	unsigned int dsp_capacity;
	unsigned int dsp_size;
	unsigned int region_count;
};

#define TETRIS_MODEM_MAX_BLOCKS 32

struct tetris_modem_block {
	unsigned int offset;
	unsigned int size;
	unsigned int info;
	unsigned int attributes;
	unsigned long long physical;
};

struct tetris_modem_memory_map {
	unsigned int count;
	struct tetris_modem_block blocks[TETRIS_MODEM_MAX_BLOCKS];
};

/*
 * Relative load plan only: caller must authenticate both immutable images,
 * establish device/rollback policy and exclusively reserve the supplied
 * capacity before any copy. No addresses, allocation, writes to firmware,
 * SMC, relocation, memory protection or CCCI publication are performed.
 * Supports only v6 / DRDI mode 3 (stock skips the separate DRDI image).
 * Output is unchanged on failure. No runtime boot caller exists yet.
 */
int tetris_modem_plan_layout(const void *rom, size_t rom_size, size_t dsp_size,
			     size_t reserved_capacity,
			     struct tetris_modem_layout *layout);

/*
 * Build the initial, pre-protection LK block map using a caller-owned physical
 * reservation. Does not reserve/free RAM, program remaps/MPU or publish tags.
 * Padding attributes describe the stock input, NOT permission to free memory.
 * The reservation and authenticated-image requirements above still apply.
 * Unknown profiles, crossing subregions and overflow leave output unchanged.
 */
int tetris_modem_plan_memory(const void *rom, size_t rom_size, size_t dsp_size,
			     unsigned long long base, size_t capacity,
			     struct tetris_modem_memory_map *map);

#endif
