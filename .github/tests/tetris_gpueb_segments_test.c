/* SPDX-License-Identifier: GPL-2.0+ */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "tetris_gpueb_segments.h"

static unsigned char image[512];
static void put(unsigned int offset, uint64_t value, unsigned int width)
{
	unsigned int i;
	for (i = 0; i < width; i++)
		image[offset + i] = value >> (8 * i);
}

static void fixture(unsigned int cls)
{
	unsigned int ph = cls == 1 ? 52 : 64;
	memset(image, 0, sizeof(image));
	memcpy(image, "\177ELF", 4);
	image[4] = cls;
	image[5] = image[6] = 1;
	put(16, 2, 2);
	put(18, 243, 2);
	put(20, 1, 4);
	put(24, 0x1000, cls == 1 ? 4 : 8);
	put(cls == 1 ? 28 : 32, ph, cls == 1 ? 4 : 8);
	put(cls == 1 ? 40 : 52, ph, 2);
	put(cls == 1 ? 42 : 54, cls == 1 ? 32 : 56, 2);
	put(cls == 1 ? 44 : 56, 1, 2);
	put(ph, 1, 4);
	put(ph + (cls == 1 ? 4 : 8), 256, cls == 1 ? 4 : 8);
	put(ph + (cls == 1 ? 8 : 16), 0x1000, cls == 1 ? 4 : 8);
	put(ph + (cls == 1 ? 12 : 24), 0x1000, cls == 1 ? 4 : 8);
	put(ph + (cls == 1 ? 16 : 32), 16, cls == 1 ? 4 : 8);
	put(ph + (cls == 1 ? 20 : 40), 32, cls == 1 ? 4 : 8);
	put(ph + (cls == 1 ? 24 : 4), 5, 4);
	put(ph + (cls == 1 ? 28 : 48), 256, cls == 1 ? 4 : 8);
}

static void reject(size_t size)
{
	struct tetris_gpueb_segment_info out, before;
	memset(&out, 0xa5, sizeof(out));
	before = out;
	assert(tetris_gpueb_inspect_segments(image, size, &out) == -EINVAL);
	assert(!memcmp(&out, &before, sizeof(out)));
}

int main(void)
{
	struct tetris_gpueb_segment_info out;
	unsigned int cls, size;
	for (cls = 1; cls <= 2; cls++) {
		unsigned int ph = cls == 1 ? 52 : 64;
		unsigned int width = cls == 1 ? 4 : 8;
		fixture(cls);
		assert(!tetris_gpueb_inspect_segments(image, sizeof(image), &out));
		assert(out.format == cls && out.count == 1 && out.memory_span == 32);
		assert(out.entry_offset == 0 && out.segments[0].file_offset == 256);
		assert(out.segments[0].memory_bytes == 32 && out.segments[0].file_bytes == 16);
		for (size = 4; size < 272; size++)
			reject(size);
		fixture(cls); put(18, 40, 2); reject(sizeof(image));
		fixture(cls); put(24, 0x1010, width); reject(sizeof(image)); /* BSS entry */
		fixture(cls); put(ph + (cls == 1 ? 16 : 32), 33, width); reject(sizeof(image));
		fixture(cls); put(ph + (cls == 1 ? 4 : 8), UINT64_MAX, width); reject(sizeof(image));
		fixture(cls); put(ph + (cls == 1 ? 28 : 48), 3, width); reject(sizeof(image));
		fixture(cls); put(ph + (cls == 1 ? 12 : 24), 0x2000, width); reject(sizeof(image));
		fixture(cls); put(ph, 2, 4); reject(sizeof(image)); /* dynamic */
		fixture(cls); put(cls == 1 ? 44 : 56, 0xffff, 2); reject(sizeof(image));
		fixture(cls); put(ph + (cls == 1 ? 20 : 40), 0x40001, width); reject(sizeof(image));
		/* Duplicate LOAD segments cannot alias file or target memory. */
		fixture(cls);
		memcpy(image + ph + (cls == 1 ? 32 : 56), image + ph, cls == 1 ? 32 : 56);
		put(cls == 1 ? 44 : 56, 2, 2);
		reject(sizeof(image));
	}
	fixture(2);
	put(24, UINT64_MAX - 8, 8);
	put(64 + 16, UINT64_MAX - 8, 8);
	put(64 + 24, UINT64_MAX - 8, 8);
	reject(sizeof(image));
	fixture(1);
	put(24, 0xfffffff0, 4);
	put(52 + 8, 0xfffffff0, 4);
	put(52 + 12, 0xfffffff0, 4);
	put(52 + 28, 1, 4);
	reject(sizeof(image));
	memset(image, 0x55, sizeof(image));
	assert(!tetris_gpueb_inspect_segments(image, sizeof(image), &out));
	assert(!out.format && !out.count && !out.memory_span);
	assert(tetris_gpueb_inspect_segments(NULL, 1, &out) == -EINVAL);
	assert(tetris_gpueb_inspect_segments(image, 0, &out) == -EINVAL);
	assert(tetris_gpueb_inspect_segments(image, 1, NULL) == -EINVAL);
	assert(tetris_gpueb_inspect_segments(image, sizeof(image), (void *)image) == -EINVAL);
	/* MTK PT framing has no inferred PMEM/DMEM addresses or BSS. */
	memset(image, 0, sizeof(image));
	put(0, 0x58901690, 4); put(4, 24, 4); put(8, 8, 4); put(12, 4, 4); put(16, 99, 4);
	assert(!tetris_gpueb_inspect_segments(image, 32, &out));
	assert(out.format == TETRIS_GPUEB_SEGMENTS_MTK_PT && out.count == 1);
	assert(out.segments[0].file_offset == 24 && out.segments[0].file_bytes == 8);
	assert(out.segments[0].pt_id == 99 && !out.memory_span && !out.entry_offset);
	memcpy(image + 32, "END", 3); memset(image + 35, 'a', 40);
	assert(!tetris_gpueb_inspect_segments(image, 75, &out) && out.trailer_bytes == 43);
	image[74] = 'z'; reject(75); image[74] = 'a';
	put(12, 3, 4); reject(75); put(12, 4, 4);
	put(8, 0xffffffff, 4); reject(75); put(8, 8, 4);
	put(4, 16, 4); reject(75); put(4, 24, 4);
	put(20, 1, 4); reject(75); put(20, 0, 4);
	for (size = 4; size < 32; size++)
		reject(size);
	memcpy(image + 32, image, 32);
	assert(!tetris_gpueb_inspect_segments(image, 64, &out) && out.count == 2);
	return 0;
}
