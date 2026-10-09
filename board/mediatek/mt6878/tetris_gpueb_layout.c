// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_GPUEB_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#endif
#include "tetris_gpueb_layout.h"

static unsigned int le32(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static int zeros(const unsigned char *p, size_t size)
{
	while (size--)
		if (*p++)
			return 0;
	return 1;
}

static int overlap(const void *a, size_t na, const void *b, size_t nb)
{
	unsigned long x = (unsigned long)a, y = (unsigned long)b;

	return na > ~0UL - x || nb > ~0UL - y ||
		(x < y + nb && y < x + na);
}

int tetris_gpueb_parse_layout(const void *data, size_t size,
			     struct tetris_gpueb_layout *out)
{
	static const unsigned char names[6][32] = {
		"tinysys-gpueb-RV33_A", "cert1", "cert2",
		"tinysys-gpueb-RV33_A_xfile", "cert1", "cert2",
	};
	static const unsigned int roles[3] = { 0, 0x02000000, 0x02000002 };
	struct tetris_gpueb_layout parsed;
	const unsigned char *p = data, *h;
	size_t offset = 0, length, end, aligned;
	unsigned int i;

	if (!p || !out || !size || size > TETRIS_GPUEB_MAX_CONTAINER ||
	    overlap(data, size, out, sizeof(*out)))
		return -EINVAL;
	for (i = 0; i < 6; i++) {
		if (size - offset < 512)
			return -EINVAL;
		h = p + offset;
		length = le32(h + 4);
		if (le32(h) != 0x58881688 || le32(h + 48) != 0x58891689 ||
		    le32(h + 52) != 512 || le32(h + 56) != 1 ||
		    le32(h + 60) != roles[i % 3] || le32(h + 68) != 16 ||
		    le32(h + 72) || le32(h + 76) != (i % 3 ? 0x22345678 : 0) ||
		    memcmp(h + 8, names[i], 32) || !length ||
		    length > size - offset - 512 ||
		    (i % 3 && length > 16384) ||
		    (!(i % 3) && length > TETRIS_GPUEB_MAX_IMAGE))
			return -EINVAL;
		parsed.sections[i].header_offset = offset;
		parsed.sections[i].payload_offset = offset + 512;
		parsed.sections[i].payload_size = length;
		end = offset + 512 + length;
		aligned = (end + 15) & ~(size_t)15;
		if (aligned > size || !zeros(p + end, aligned - end))
			return -EINVAL;
		offset = aligned;
	}
	if (parsed.sections[0].payload_size % 16 || !zeros(p + offset, size - offset))
		return -EINVAL;
	*out = parsed;
	return 0;
}

int tetris_gpueb_describe_plain(const void *data, size_t size,
			      struct tetris_gpueb_plain_info *out)
{
	struct tetris_gpueb_plain_info info = { 0 };
	const unsigned char *p = data;

	if (!p || !out || !size || size > TETRIS_GPUEB_MAX_IMAGE ||
	    overlap(data, size, out, sizeof(*out)))
		return -EINVAL;
	info.bytes = size;
	info.covers_stock_copy = size >= TETRIS_GPUEB_LK_COPY_BYTES;
	if (size >= 52 && !memcmp(p, "\177ELF", 4) && p[5] == 1 && p[6] == 1) {
		if (p[4] == 1 && p[40] == 52 && !p[41])
			info.format = TETRIS_GPUEB_PLAIN_ELF32;
		else if (size >= 64 && p[4] == 2 && p[52] == 64 && !p[53])
			info.format = TETRIS_GPUEB_PLAIN_ELF64;
	} else if (size >= 18 && p[0] == 0x1f && p[1] == 0x8b && p[2] == 8) {
		info.format = TETRIS_GPUEB_PLAIN_GZIP_SIGNATURE;
		info.gzip_isize_hint = le32(p + size - 4);
	} else if (size >= 4 && le32(p) == 0x184d2204) {
		info.format = TETRIS_GPUEB_PLAIN_LZ4_SIGNATURE;
	}
	*out = info;
	return 0;
}
