// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_GPUEB_HOST_TEST
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t u64;
#else
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#endif
#include "tetris_gpueb_segments.h"

static u64 little(const unsigned char *p, unsigned int n)
{
	u64 value = 0;
	unsigned int i;
	for (i = 0; i < n; i++)
		value |= (u64)p[i] << (8 * i);
	return value;
}

static int within(u64 offset, u64 length, u64 size)
{
	return offset <= size && length <= size - offset;
}

static int intersects(u64 a, u64 na, u64 b, u64 nb)
{
	return na && nb && (a <= b ? b - a < na : a - b < nb);
}

/* B4.1 LK 0x178c4..0x178f4: next = header + hdrsize +
 * roundup(datasize, alignment), dispatch ID at +16. Actual cached GPUEB
 * xfile has 24-byte PT header, ID99 and END +40 hex trailer. This describes
 * framing only; it does NOT authenticate xfile or assign GPUEB load roles.
 */
static int inspect_pt(const unsigned char *p, size_t bytes,
		      struct tetris_gpueb_segment_info *out)
{
	struct tetris_gpueb_segment_info result = { 0 };
	size_t offset = 0, length, aligned, i;
	unsigned int alignment, id;

	while (bytes - offset >= 24 && little(p + offset, 4) == 0x58901690) {
		if (result.count == TETRIS_GPUEB_MAX_SEGMENTS ||
		    little(p + offset + 4, 4) != 24 || little(p + offset + 20, 4))
			return -EINVAL;
		length = little(p + offset + 8, 4);
		alignment = little(p + offset + 12, 4);
		id = little(p + offset + 16, 4);
		if (!length || !alignment || alignment > 0x10000 ||
		    (alignment & (alignment - 1)) || length > bytes - offset - 24)
			return -EINVAL;
		aligned = (length + alignment - 1) & ~(size_t)(alignment - 1);
		if (aligned > bytes - offset - 24)
			return -EINVAL;
		result.segments[result.count].file_offset = offset + 24;
		result.segments[result.count].file_bytes = length;
		result.segments[result.count].pt_id = id;
		result.segments[result.count++].pt_alignment = alignment;
		offset += 24 + aligned;
	}
	if (!result.count)
		return -EINVAL;
	if (offset != bytes) {
		/* Recognize the observed suffix shape, not its digest semantics. */
		if (bytes - offset != 43 || memcmp(p + offset, "END", 3))
			return -EINVAL;
		for (i = offset + 3; i < bytes; i++)
			if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
				return -EINVAL;
		result.trailer_bytes = 43;
	}
	result.format = TETRIS_GPUEB_SEGMENTS_MTK_PT;
	*out = result;
	return 0;
}

int tetris_gpueb_inspect_segments(const void *authenticated, size_t bytes,
				struct tetris_gpueb_segment_info *out)
{
	struct tetris_gpueb_segment_info result = { 0 };
	struct { u64 file, filesz, addr, memsz, flags; } load[16];
	const unsigned char *p = authenticated, *h;
	u64 entry, phoff, base = ~(u64)0, limit = 0, addr, memsz, off, filesz, align, flags;
	unsigned long input = (unsigned long)authenticated, output = (unsigned long)out;
	unsigned int cls, ehsize, phsize, phnum, type, i, j, count = 0;
	int entry_found = 0;

	if (!p || !out || !bytes || bytes > 0x100000 ||
	    bytes > ~0UL - input || sizeof(*out) > ~0UL - output ||
	    (input < output + sizeof(*out) && output < input + bytes))
		return -EINVAL;
	if (bytes >= 4 && little(p, 4) == 0x58901690)
		return inspect_pt(p, bytes, out);
	if (bytes < 4 || memcmp(p, "\177ELF", 4)) {
		*out = result;
		return 0;
	}
	if (bytes < 16 || (p[4] != 1 && p[4] != 2) || p[5] != 1 || p[6] != 1)
		return -EINVAL;
	cls = p[4];
	ehsize = cls == 1 ? 52 : 64;
	phsize = cls == 1 ? 32 : 56;
	if (bytes < ehsize || little(p + 16, 2) != 2 || little(p + 18, 2) != 243 ||
	    little(p + 20, 4) != 1 || little(p + (cls == 1 ? 40 : 52), 2) != ehsize ||
	    little(p + (cls == 1 ? 42 : 54), 2) != phsize)
		return -EINVAL;
	entry = little(p + 24, cls == 1 ? 4 : 8);
	phoff = little(p + (cls == 1 ? 28 : 32), cls == 1 ? 4 : 8);
	phnum = little(p + (cls == 1 ? 44 : 56), 2);
	/* No PN_XNUM/section-header indirection and bounded stack/work. */
	if (!phnum || phnum > TETRIS_GPUEB_MAX_SEGMENTS || phoff < ehsize ||
	    !within(phoff, (u64)phnum * phsize, bytes))
		return -EINVAL;
	for (i = 0; i < phnum; i++) {
		h = p + (size_t)phoff + i * phsize;
		type = little(h, 4);
		if (!type)
			continue;
		/* This is a static remote firmware profile, not a dynamic ELF loader. */
		if (type != 1)
			return -EINVAL;
		off = little(h + (cls == 1 ? 4 : 8), cls == 1 ? 4 : 8);
		addr = little(h + (cls == 1 ? 12 : 24), cls == 1 ? 4 : 8);
		/* No virtual/physical translation is established for RV33. */
		if (addr != little(h + (cls == 1 ? 8 : 16), cls == 1 ? 4 : 8))
			return -EINVAL;
		filesz = little(h + (cls == 1 ? 16 : 32), cls == 1 ? 4 : 8);
		memsz = little(h + (cls == 1 ? 20 : 40), cls == 1 ? 4 : 8);
		flags = little(h + (cls == 1 ? 24 : 4), 4);
		align = little(h + (cls == 1 ? 28 : 48), cls == 1 ? 4 : 8);
		if (!memsz || filesz > memsz || memsz > 0x40000 || addr > ~(u64)0 - memsz ||
		    (cls == 1 && addr + memsz > 0x100000000ULL) ||
		    !within(off, filesz, bytes) || !(flags & 4) || (flags & ~7ULL) ||
		    (align > 1 && ((align & (align - 1)) || align > 0x10000 ||
		     (addr & (align - 1)) != (off & (align - 1)))))
			return -EINVAL;
		for (j = 0; j < count; j++)
			if (intersects(addr, memsz, load[j].addr, load[j].memsz) ||
			    intersects(off, filesz, load[j].file, load[j].filesz))
				return -EINVAL;
		load[count].file = off;
		load[count].filesz = filesz;
		load[count].addr = addr;
		load[count].memsz = memsz;
		load[count++].flags = flags;
		if (addr < base)
			base = addr;
		if (addr + memsz > limit)
			limit = addr + memsz;
		if ((flags & 1) && entry >= addr && entry - addr < filesz)
			entry_found = 1;
	}
	if (!count || !entry_found || limit - base > 0x40000)
		return -EINVAL;
	result.format = cls == 1 ? TETRIS_GPUEB_SEGMENTS_RISCV_ELF32 :
		TETRIS_GPUEB_SEGMENTS_RISCV_ELF64;
	result.count = count;
	result.memory_span = limit - base;
	result.entry_offset = entry - base;
	for (i = 0; i < count; i++) {
		result.segments[i].file_offset = load[i].file;
		result.segments[i].file_bytes = load[i].filesz;
		result.segments[i].memory_offset = load[i].addr - base;
		result.segments[i].memory_bytes = load[i].memsz;
		result.segments[i].flags = load[i].flags;
	}
	*out = result;
	return 0;
}
