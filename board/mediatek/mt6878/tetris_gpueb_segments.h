/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TETRIS_GPUEB_SEGMENTS_H
#define TETRIS_GPUEB_SEGMENTS_H
#include <stddef.h>

#define TETRIS_GPUEB_MAX_SEGMENTS 16U
enum tetris_gpueb_segment_format { TETRIS_GPUEB_SEGMENTS_UNKNOWN,
	TETRIS_GPUEB_SEGMENTS_RISCV_ELF32, TETRIS_GPUEB_SEGMENTS_RISCV_ELF64,
	TETRIS_GPUEB_SEGMENTS_MTK_PT };
struct tetris_gpueb_segment {
	unsigned int file_offset, file_bytes, memory_offset, memory_bytes, flags;
	/* PT record ID/alignment are framing, NOT addresses or executable roles. */
	unsigned int pt_id, pt_alignment;
};
struct tetris_gpueb_segment_info {
	unsigned int format, count, memory_span, entry_offset;
	unsigned int trailer_bytes;
	struct tetris_gpueb_segment segments[TETRIS_GPUEB_MAX_SEGMENTS];
};
/* Call ONLY after signed plaintext digest verification. No authentication,
 * decompression, upload permission, absolute address or plaintext publication.
 * Unknown format succeeds with zero metadata; malformed known framing fails
 * atomically. PT reports no memory map: ID semantics are not established.
 */
int tetris_gpueb_inspect_segments(const void *authenticated, size_t bytes,
				struct tetris_gpueb_segment_info *out);
#endif
