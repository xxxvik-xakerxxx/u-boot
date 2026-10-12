/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_SMEM_EVIDENCE_H
#define __TETRIS_MODEM_SMEM_EVIDENCE_H
#include "tetris_modem_loaded_boot.h"

#define TETRIS_MODEM_SMEM_EVIDENCE_HEADER 80U
#define TETRIS_MODEM_SMEM_EVIDENCE_RECORD 128U
#define TETRIS_MODEM_SMEM_EVIDENCE_SIZE 336U
#define TETRIS_MODEM_SMEM_EVIDENCE_SCOPE_BL33 1U

/* Little-endian v1, two records (NC then cache). Header u32 offsets:
 * 0 version, 4 bytes, 8 scope, 12 count, 16 AP, 20 MD generation,
 * 24 loaded stage, 28 boot stage, 32/36/40 errors, 44 reserved;
 * 48..79 independently measured preloader SHA256.
 * Each record: u32 bank/slot; u64 reservation base/capacity, range start/size,
 * actual start/end/enable replies; eight actual u64 policy replies at offset64.
 * These are completed NS BL33 boot readbacks, NOT a Linux NS access grant,
 * authenticated runtime policy, DMA readiness or a modem READY event.
 */
int tetris_modem_build_smem_evidence(const struct tetris_modem_loaded_report *report,
		void *buffer, size_t capacity);
/* The real publisher has no caller-provided report/permission inputs. */
int tetris_modem_loaded_encode_smem_evidence(void *buffer, size_t capacity);
#endif
