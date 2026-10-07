// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#endif
#include "tetris_modem_layout.h"

#define CHECK_SIZE 512U
#define MAX_PAYLOAD (64U * 1024 * 1024)

static unsigned int word(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static int contained(unsigned int offset, unsigned int size, unsigned int total)
{
	return size && offset < total && size <= total - offset;
}

int tetris_modem_plan_layout(const void *rom, size_t rom_size, size_t dsp_size,
			     size_t reserved_capacity,
			     struct tetris_modem_layout *layout)
{
	struct tetris_modem_layout out = { 0 };
	const unsigned char *header;
	unsigned int i, j;

	if (!rom || !layout || rom_size < CHECK_SIZE || rom_size > MAX_PAYLOAD ||
	    rom_size % 16 || !dsp_size || dsp_size > MAX_PAYLOAD || dsp_size % 16)
		return -EINVAL;
	header = (const unsigned char *)rom + rom_size - CHECK_SIZE;
	if (memcmp(header, "CHECK_HEADER", 12) || word(header + 508) != CHECK_SIZE)
		return -EBADMSG;
	if (word(header + 12) != 6 || header[168] != 1 ||
	    word(header + 16) != 2 || word(header + 20) != 14 ||
	    word(header + 0x190) != 3)
		return -EPROTONOSUPPORT;
	out.memory_size = word(header + 172);
	out.logical_image_size = word(header + 176);
	out.rom_size = rom_size;
	out.dsp_offset = word(header + 184);
	out.dsp_capacity = word(header + 188);
	out.dsp_size = dsp_size;
	out.region_count = word(header + 192);
	if (!out.memory_size || out.memory_size > reserved_capacity ||
	    !out.logical_image_size || out.logical_image_size > out.memory_size ||
	    rom_size > out.memory_size ||
	    !contained(out.dsp_offset, out.dsp_capacity, out.memory_size) ||
	    dsp_size > out.dsp_capacity || out.dsp_offset < rom_size ||
	    !out.region_count || out.region_count > 8)
		return -ERANGE;
	/* Region descriptors are relative to MD memory, not AP physical addresses. */
	for (i = 0; i < out.region_count; i++) {
		unsigned int offset = word(header + 196 + 8 * i);
		unsigned int size = word(header + 200 + 8 * i);

		if (!contained(offset, size, out.memory_size))
			return -ERANGE;
		for (j = 0; j < i; j++) {
			unsigned int previous = word(header + 196 + 8 * j);
			unsigned int length = word(header + 200 + 8 * j);

			if (offset < previous + length && previous < offset + size)
				return -ERANGE;
		}
	}
	/* md_img_size is a logical size, not the extent of the stored ROM. */
	*layout = out;
	return 0;
}

/* LK annotates one containing block and splits before/after fragments. */
static int annotate(struct tetris_modem_memory_map *map, unsigned int offset,
		    unsigned int size, unsigned int info, unsigned int attributes)
{
	unsigned int i;

	if (!size)
		return -EINVAL;
	for (i = 0; i < map->count; i++) {
		struct tetris_modem_block old = map->blocks[i];
		struct tetris_modem_block parts[3];
		unsigned int count = 0, end;

		if (offset < old.offset || offset - old.offset >= old.size)
			continue;
		if (size > old.size - (offset - old.offset))
			return -ERANGE;
		if (offset == old.offset && size == old.size &&
		    (old.info & info) == info &&
		    (old.attributes & attributes) == attributes)
			return -EINVAL;
		end = offset + size;
		if (offset != old.offset) {
			parts[count] = old;
			parts[count++].size = offset - old.offset;
		}
		parts[count] = old;
		parts[count].offset = offset;
		parts[count].size = size;
		parts[count].info |= info;
		parts[count++].attributes |= attributes;
		if (end != old.offset + old.size) {
			parts[count] = old;
			parts[count].offset = end;
			parts[count++].size = old.offset + old.size - end;
		}
		if (count - 1 > TETRIS_MODEM_MAX_BLOCKS - map->count)
			return -E2BIG;
		memmove(&map->blocks[i + count], &map->blocks[i + 1],
			(map->count - i - 1) * sizeof(old));
		memcpy(&map->blocks[i], parts, count * sizeof(old));
		map->count += count - 1;
		return 0;
	}
	return -ERANGE;
}

int tetris_modem_plan_memory(const void *rom, size_t rom_size, size_t dsp_size,
			     unsigned long long base, size_t capacity,
			     struct tetris_modem_memory_map *map)
{
	struct tetris_modem_memory_map out = { 0 };
	struct tetris_modem_layout layout;
	const unsigned char *header;
	unsigned int i;
	int ret;

	if (!map || !base || !capacity || capacity > 0xffffffffU ||
	    base > ~0ULL - capacity)
		return -EINVAL;
	ret = tetris_modem_plan_layout(rom, rom_size, dsp_size, capacity, &layout);
	if (ret)
		return ret;
	header = (const unsigned char *)rom + rom_size - CHECK_SIZE;
	out.count = 1;
	out.blocks[0].size = capacity;
	ret = annotate(&out, 0, layout.memory_size, 0, 1);
	if (ret)
		return ret;
	for (i = 0; i < layout.region_count; i++) {
		ret = annotate(&out, word(header + 196 + 8 * i),
			       word(header + 200 + 8 * i), 1U << i, 0);
		if (ret)
			return ret;
	}
	ret = annotate(&out, layout.dsp_offset, layout.dsp_capacity, 0, 2);
	if (ret)
		return ret;
	for (i = 0; i < 8; i++) {
		unsigned int offset = word(header + 0x11c + 8 * i);
		unsigned int size = word(header + 0x120 + 8 * i);

		if (!size)
			continue;
		if (!contained(offset, size, layout.memory_size) ||
		    offset < rom_size ||
		    (offset < layout.dsp_offset + layout.dsp_capacity &&
		     layout.dsp_offset < offset + size))
			return -ERANGE;
		ret = annotate(&out, offset, size, 0, 4);
		if (ret)
			return ret;
	}
	/* Stock processes DRDI windows even when mode 3 skips the image load. */
	for (i = 0; i < 3; i++) {
		unsigned int field = i == 2 ? 0x164 : 0x16c + 8 * i;
		unsigned int offset = word(header + field);
		unsigned int size = word(header + field + 4);

		if (!size)
			continue;
		if (!contained(offset, size, layout.memory_size))
			return -ERANGE;
		ret = annotate(&out, offset, size, 0, 0x20U << i);
		if (ret)
			return ret;
	}
	for (i = 0; i < out.count; i++)
		out.blocks[i].physical = base + out.blocks[i].offset;
	*map = out;
	return 0;
}
