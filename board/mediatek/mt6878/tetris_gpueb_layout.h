/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_GPUEB_LAYOUT_H
#define __TETRIS_GPUEB_LAYOUT_H
#include <stddef.h>

#define TETRIS_GPUEB_MAX_CONTAINER 0x200000U
#define TETRIS_GPUEB_MAX_IMAGE 0x100000U
#define TETRIS_GPUEB_LK_COPY_BYTES 0x3f2b8U

struct tetris_gpueb_section {
	size_t header_offset;
	size_t payload_offset;
	size_t payload_size;
};

struct tetris_gpueb_layout {
	struct tetris_gpueb_section sections[6];
};

enum tetris_gpueb_plain_format {
	TETRIS_GPUEB_PLAIN_UNKNOWN,
	TETRIS_GPUEB_PLAIN_ELF32,
	TETRIS_GPUEB_PLAIN_ELF64,
	TETRIS_GPUEB_PLAIN_GZIP_SIGNATURE,
	TETRIS_GPUEB_PLAIN_LZ4_SIGNATURE,
};

struct tetris_gpueb_plain_info {
	size_t bytes;
	unsigned int format;
	unsigned int covers_stock_copy;
	/* Gzip trailer hint only: not validated expansion size or upload bounds. */
	unsigned int gzip_isize_hint;
};

/* Immutable six-member framing only; output unchanged on failure. */
int tetris_gpueb_parse_layout(const void *data, size_t size,
			     struct tetris_gpueb_layout *out);
/* Only bounded recognition; never decompress or expose plaintext addresses. */
int tetris_gpueb_describe_plain(const void *data, size_t size,
			      struct tetris_gpueb_plain_info *out);
#endif
