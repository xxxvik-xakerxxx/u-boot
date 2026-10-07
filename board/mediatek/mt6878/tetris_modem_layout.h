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

#endif
