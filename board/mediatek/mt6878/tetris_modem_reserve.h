/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_RESERVE_H
#define __TETRIS_MODEM_RESERVE_H

#define TETRIS_MODEM_WINDOW (512ULL << 20)
#define TETRIS_MODEM_ALIGN (32ULL << 20)
#define TETRIS_MODEM_LIMIT (1ULL << 35)

/* Allocator must return exclusive, single-DRAM-bank ownership below limit. */
struct tetris_modem_allocator {
	int (*alloc)(void *ctx, unsigned long long *base);
	void (*release)(void *ctx, unsigned long long base);
	void *ctx;
};

/* Transactional DT publication; never loads firmware or changes hardware. */
int tetris_modem_reserve(void *fdt, const struct tetris_modem_allocator *ops);
int tetris_modem_reserve_diagnostic(void *fdt);
#endif
