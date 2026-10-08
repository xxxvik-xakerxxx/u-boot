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

static void put_word(unsigned char *p, unsigned int value)
{
	p[0] = value;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

static int buffer_overlap(const void *a, size_t a_size, const void *b, size_t b_size)
{
	size_t first = (size_t)a, second = (size_t)b;

	if (first > (size_t)-1 - a_size || second > (size_t)-1 - b_size)
		return 1;
	return first < second + b_size && second < first + a_size;
}

static void put_smem(unsigned char *out, unsigned long long base,
		     unsigned int id, unsigned int offset, unsigned int size,
		     unsigned int flags, unsigned int md_offset)
{
	unsigned long long physical = base + offset;

	memset(out, 0, 40);
	put_word(out, physical);
	put_word(out + 4, physical >> 32);
	put_word(out + 16, id);
	put_word(out + 20, offset);
	put_word(out + 24, size);
	put_word(out + 32, flags);
	put_word(out + 36, md_offset + offset);
}

int tetris_modem_encode_smem(const struct tetris_modem_smem_entry *entries,
			    size_t count, unsigned long long base, size_t capacity,
			    unsigned int md_offset, void *buffer, size_t size)
{
	unsigned char *out = buffer;
	size_t i, j, rows = count;
	unsigned int cursor = 0;

	if (!entries || !buffer || !count || count > 128 ||
	    (size_t)entries > (size_t)-1 - count * sizeof(*entries))
		return -EINVAL;
	if (!base || !capacity || capacity > 0xffffffffULL ||
	    base > ~0ULL - capacity)
		return -ERANGE;
	for (i = 0; i < count; i++) {
		const struct tetris_modem_smem_entry *entry = &entries[i];

		if (entry->id > 0x7fffffffU || (entry->flags & ~0x1ffU) ||
		    (entry->flags & 4))
			return -EINVAL;
		if (entry->offset < cursor || entry->offset > capacity ||
		    entry->size > capacity - entry->offset ||
		    entry->offset > 0xffffffffU - md_offset ||
		    entry->size > 0xffffffffU - md_offset - entry->offset)
			return -ERANGE;
		for (j = 0; j < i; j++)
			if (entries[j].id == entry->id)
				return -EEXIST;
		if (entry->offset > cursor)
			rows++;
		cursor = entry->offset + entry->size;
	}
	if (rows * 40 > size)
		return -ENOSPC;
	if (buffer_overlap(buffer, rows * 40, entries, count * sizeof(*entries)))
		return -EINVAL;
	cursor = 0;
	for (i = 0; i < count; i++) {
		const struct tetris_modem_smem_entry *entry = &entries[i];

		if (entry->offset > cursor) {
			put_smem(out, base, entry->id, cursor, entry->offset - cursor,
				 4, md_offset);
			out += 40;
		}
		put_smem(out, base, entry->id, entry->offset, entry->size,
			 entry->flags, md_offset);
		out += 40;
		cursor = entry->offset + entry->size;
	}
	return rows * 40;
}

int tetris_modem_encode_tags(const struct tetris_modem_tag *tags, size_t count,
			     void *buffer, size_t size)
{
	unsigned char *out = buffer;
	size_t i, j, total, offset;

	if (!tags || !buffer || !count || count > TETRIS_MODEM_TAG_MAX_COUNT ||
	    (size_t)tags > (size_t)-1 - count * sizeof(*tags))
		return -EINVAL;
	total = count * TETRIS_MODEM_TAG_HEADER_SIZE;
	for (i = 0; i < count; i++) {
		if (!tags[i].name[0] ||
		    !memchr(tags[i].name, 0, TETRIS_MODEM_TAG_NAME_SIZE) ||
		    !tags[i].data || !tags[i].size)
			return -EINVAL;
		for (j = 0; j < i; j++)
			if (!strcmp(tags[i].name, tags[j].name))
				return -EEXIST;
		if (tags[i].size > TETRIS_MODEM_TAG_MAX_BYTES - total)
			return -E2BIG;
		total += tags[i].size;
	}
	if (total > size)
		return -ENOSPC;
	if (buffer_overlap(buffer, total, tags, count * sizeof(*tags)))
		return -EINVAL;
	for (i = 0; i < count; i++)
		if (buffer_overlap(buffer, total, tags[i].data, tags[i].size))
			return -EINVAL;

	/* All checks precede the first write; no pointers/native padding on wire. */
	memset(out, 0, total);
	offset = count * TETRIS_MODEM_TAG_HEADER_SIZE;
	for (i = 0; i < count; i++) {
		unsigned char *header = out + i * TETRIS_MODEM_TAG_HEADER_SIZE;

		memcpy(header, tags[i].name, strlen(tags[i].name));
		put_word(header + 64, offset);
		put_word(header + 68, tags[i].size);
		put_word(header + 72, i + 1 < count ?
			 (i + 1) * TETRIS_MODEM_TAG_HEADER_SIZE : 0);
		memcpy(out + offset, tags[i].data, tags[i].size);
		offset += tags[i].size;
	}
	return total;
}

int tetris_modem_encode_memory(const struct tetris_modem_memory_map *map,
		unsigned long long base, size_t capacity, void *buffer, size_t size)
{
	unsigned char encoded[TETRIS_MODEM_MAX_BLOCKS * TETRIS_MODEM_CCCI_BLOCK_SIZE];
	size_t bytes, offset = 0;
	unsigned int i;

	if (!map || !buffer || !base || !capacity || capacity > 0xffffffffU ||
	    base > ~0ULL - capacity || !map->count ||
	    map->count > TETRIS_MODEM_MAX_BLOCKS)
		return -EINVAL;
	bytes = map->count * TETRIS_MODEM_CCCI_BLOCK_SIZE;
	if (size < bytes)
		return -ENOSPC;
	for (i = 0; i < map->count; i++) {
		const struct tetris_modem_block *block = &map->blocks[i];
		unsigned char *entry = encoded + i * TETRIS_MODEM_CCCI_BLOCK_SIZE;

		if (block->offset != offset || !block->size ||
		    block->size > capacity - offset || block->physical != base + offset)
			return -ERANGE;
		put_word(entry, block->offset);
		put_word(entry + 4, block->size);
		put_word(entry + 8, block->info);
		put_word(entry + 12, block->attributes);
		put_word(entry + 16, block->physical);
		put_word(entry + 20, block->physical >> 32);
		offset += block->size;
	}
	if (offset != capacity)
		return -ERANGE;
	/* Staging also permits buffer to alias map without corrupting later entries. */
	memcpy(buffer, encoded, bytes);
	return bytes;
}

int tetris_modem_plan_remap(unsigned long long base,
			    unsigned long long capacity,
			    unsigned long long dram_base,
			    unsigned long long dram_size,
			    struct tetris_modem_remap *remap)
{
	struct tetris_modem_remap out = { 0 };
	const unsigned long long window = 1ULL << 29;
	unsigned int i;

	if (!remap || !base || (base & ((1ULL << 25) - 1)) ||
	    capacity < window || !dram_size ||
	    dram_base > ~0ULL - dram_size || base > ~0ULL - capacity)
		return -EINVAL;
	/* ATF truncates page addresses to ten bits and checks only the base. */
	if (base > (1ULL << 35) - window || base < dram_base ||
	    base - dram_base >= dram_size ||
	    capacity > dram_size - (base - dram_base))
		return -ERANGE;
	for (i = 0; i < 16; i++) {
		unsigned int shift = (i % 3) * 10;
		unsigned int page = (base >> 25) + i;

		out.value[i / 3] |= page << shift;
		out.mask[i / 3] |= 0x3ffU << shift;
	}
	*remap = out;
	return 0;
}

int tetris_modem_plan_emi(unsigned long long start, unsigned long long size,
			  unsigned int slot, struct tetris_modem_emi_range *range)
{
	struct tetris_modem_emi_range out = { 0 };
	const unsigned long long origin = 0x40000000ULL;
	const unsigned long long limit = origin + (1ULL << 35);

	if (!range || !size || ((start | size) & 4095) || slot < 32 || slot > 43)
		return -EINVAL;
	/* ATF masks input pages to 24 bits, then stores 23 relative page bits. */
	if (start < origin || start >= limit || size >= limit - start)
		return -ERANGE;
	out.start_page = start >> 12;
	out.end_page = (start + size) >> 12;
	out.start_readback = start;
	/* The raw end getter retains the enable marker from register bit 31. */
	out.end_readback = (start + size) | (1ULL << 43);
	out.slot = slot;
	*range = out;
	return 0;
}
