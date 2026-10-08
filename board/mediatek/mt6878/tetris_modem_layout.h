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
#define TETRIS_MODEM_CCCI_BLOCK_SIZE 24

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

struct tetris_modem_remap {
	unsigned int value[6];
	unsigned int mask[6];
};

struct tetris_modem_emi_range {
	unsigned long long start_page;
	unsigned long long end_page;
	unsigned long long start_readback;
	unsigned long long end_readback;
	unsigned int slot;
};

#define TETRIS_MODEM_TAG_NAME_SIZE 64
#define TETRIS_MODEM_TAG_HEADER_SIZE 76
#define TETRIS_MODEM_TAG_MAX_COUNT 128
#define TETRIS_MODEM_TAG_MAX_BYTES 65536

struct tetris_modem_tag {
	char name[TETRIS_MODEM_TAG_NAME_SIZE];
	const void *data;
	size_t size;
};

/*
 * Encode v2 CCCI tag headers and opaque, already encoded payloads. Returns
 * bytes used or negative errno; errors leave the destination untouched.
 * Inputs must remain immutable and cannot overlap the used destination.
 * This validates framing only, NOT payload semantics, firmware authenticity,
 * reservation/protection/reset state or readiness. Does not publish a DT
 * descriptor. Caller must complete those gates before exposing tags to Linux.
 */
int tetris_modem_encode_tags(const struct tetris_modem_tag *tags, size_t count,
			     void *buffer, size_t size);

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

/*
 * Encode the complete planned reservation as little-endian CCCI md_mem_layout
 * entries, not a native C struct dump. Return byte count or negative errno.
 * Validate contiguous coverage and physical addresses before any output write.
 * Does not publish tags, grant memory permissions or assert modem readiness.
 */
int tetris_modem_encode_memory(const struct tetris_modem_memory_map *map,
		unsigned long long base, size_t capacity, void *buffer, size_t size);

/*
 * Expected bank-0 remap fields for the audited ATF profile: sixteen 32 MiB
 * pages, ten physical page bits each. Validate the whole owned reservation
 * against one caller-supplied DRAM bank. Bounds do not establish ownership.
 * No SMC/MMIO or protection changes. Output is unchanged on failure.
 */
int tetris_modem_plan_remap(unsigned long long base,
			    unsigned long long capacity,
			    unsigned long long dram_base,
			    unsigned long long dram_size,
			    struct tetris_modem_remap *remap);

/*
 * Encode one modem EMI range for the audited ATF, using LK's start+size end
 * convention. Reject page truncation before any one-shot slot programming.
 * Slots are limited to the stock modem table (32..43). This is not a permission
 * preset, reservation/overlap check or authorization to issue an SMC; slot 40
 * additionally needs a validated permission preset. Output is atomic.
 */
int tetris_modem_plan_emi(unsigned long long start, unsigned long long size,
			  unsigned int slot, struct tetris_modem_emi_range *range);

#endif
