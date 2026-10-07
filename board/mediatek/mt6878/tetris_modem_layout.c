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
