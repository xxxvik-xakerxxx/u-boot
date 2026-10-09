// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_RESERVE_HOST_TEST
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>
#else
#include <malloc.h>
#include <stdio.h>
#include <asm/u-boot.h>
#include <string.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#include <lmb.h>
#include <asm/global_data.h>
DECLARE_GLOBAL_DATA_PTR;
#endif
#include "tetris_modem_reserve.h"

static unsigned long long read_pair(const fdt32_t *p)
{
	return (unsigned long long)fdt32_to_cpu(p[0]) << 32 |
	       fdt32_to_cpu(p[1]);
}

static int check_tree(const void *fdt, unsigned long long base)
{
	int parent, node, len, i, count;
	const fdt32_t *reg;
	const void *ranges;
	unsigned long long start, size;
	uint64_t address, length;

	parent = fdt_path_offset(fdt, "/reserved-memory");
	if (parent < 0)
		return parent;
	if (fdt_address_cells(fdt, 0) != 2 || fdt_size_cells(fdt, 0) != 2 ||
	    fdt_address_cells(fdt, parent) != 2 ||
	    fdt_size_cells(fdt, parent) != 2)
		return -FDT_ERR_BADNCELLS;
	ranges = fdt_getprop(fdt, parent, "ranges", &len);
	if (!ranges || len)
		return -FDT_ERR_BADVALUE;
	if (fdt_subnode_offset(fdt, parent, "tetris-modem-diagnostic") !=
	    -FDT_ERR_NOTFOUND)
		return -EEXIST;
	fdt_for_each_subnode(node, fdt, parent) {
		reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg && len == -FDT_ERR_NOTFOUND)
			continue; /* Linux allocates dynamic reservations around ours. */
		if (!reg || len <= 0 || len % 16)
			return -FDT_ERR_BADVALUE;
		for (i = 0; i < len / 4; i += 4) {
			start = read_pair(reg + i);
			size = read_pair(reg + i + 2);
			if (!size || start + size < start)
				return -FDT_ERR_BADVALUE;
			if (base && start < base + TETRIS_MODEM_WINDOW &&
			    base < start + size)
				return -EEXIST;
		}
	}
	if (node != -FDT_ERR_NOTFOUND)
		return node;
	count = fdt_num_mem_rsv(fdt);
	if (count < 0)
		return count;
	for (i = 0; i < count; i++) {
		if (fdt_get_mem_rsv(fdt, i, &address, &length))
			return -FDT_ERR_BADVALUE;
		if (!length || address + length < address)
			return -FDT_ERR_BADVALUE;
		if (base && address < base + TETRIS_MODEM_WINDOW &&
		    base < address + length)
			return -EEXIST;
	}
	return 0;
}

int tetris_modem_reserve(void *fdt, const struct tetris_modem_allocator *ops)
{
	unsigned long long base = 0;
	fdt32_t reg[4];
	void *copy;
	int ret, bytes, node;

	if (!fdt || !ops || !ops->alloc || !ops->release)
		return -EINVAL;
	ret = fdt_check_header(fdt);
	if (ret)
		return ret;
	ret = check_tree(fdt, 0);
	if (ret)
		return ret;
	bytes = fdt_totalsize(fdt);
	if (bytes <= 0)
		return -FDT_ERR_BADVALUE;
	copy = malloc(bytes);
	if (!copy)
		return -ENOMEM;
	memcpy(copy, fdt, bytes);
	ret = ops->alloc(ops->ctx, &base);
	if (ret)
		goto out;
	if (!base || base % TETRIS_MODEM_ALIGN ||
	    base > TETRIS_MODEM_LIMIT - TETRIS_MODEM_WINDOW) {
		ret = -ERANGE;
		goto release;
	}
	ret = check_tree(copy, base);
	if (ret)
		goto release;
	node = fdt_path_offset(copy, "/reserved-memory");
	node = fdt_add_subnode(copy, node, "tetris-modem-diagnostic");
	if (node < 0) {
		ret = node;
		goto release;
	}
	reg[0] = cpu_to_fdt32(base >> 32);
	reg[1] = cpu_to_fdt32(base);
	reg[2] = 0;
	reg[3] = cpu_to_fdt32(TETRIS_MODEM_WINDOW);
	ret = fdt_setprop(copy, node, "reg", reg, sizeof(reg));
	if (!ret)
		ret = fdt_setprop(copy, node, "no-map", NULL, 0);
	if (!ret)
		ret = fdt_add_mem_rsv(copy, base, TETRIS_MODEM_WINDOW);
	if (ret)
		goto release;
	/* Publish only after every mutation succeeds; no partial Linux carveout. */
	memcpy(fdt, copy, bytes);
	goto out;
release:
	ops->release(ops->ctx, base);
out:
	free(copy);
	return ret;
}

#ifndef TETRIS_MODEM_RESERVE_HOST_TEST
static void release_window(void *ctx, unsigned long long base)
{
	long ret;

	ret = lmb_free(base, TETRIS_MODEM_WINDOW, LMB_NOMAP | LMB_NOOVERWRITE);
	if (ret)
		printf("Tetris modem RAM release failed: %ld\n", ret);
}

static int allocate_window(void *ctx, unsigned long long *result)
{
	phys_addr_t base = TETRIS_MODEM_LIMIT;
	unsigned long long start, size;
	int ret, i;

	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, TETRIS_MODEM_ALIGN, &base,
			    TETRIS_MODEM_WINDOW, LMB_NOMAP | LMB_NOOVERWRITE);
	if (ret)
		return ret;
	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++) {
		start = gd->bd->bi_dram[i].start;
		size = gd->bd->bi_dram[i].size;
		if (size >= TETRIS_MODEM_WINDOW && base >= start &&
		    base - start <= size - TETRIS_MODEM_WINDOW) {
			*result = base;
			if (ctx)
				*(unsigned long long *)ctx = base;
			return 0;
		}
	}
	release_window(ctx, base);
	return -ERANGE;
}

int tetris_modem_reserve_diagnostic(void *fdt)
{
	const struct tetris_modem_allocator ops = {
		.alloc = allocate_window,
		.release = release_window,
	};

	return tetris_modem_reserve(fdt, &ops);
}

int tetris_modem_reserve_diagnostic_window(void *fdt, unsigned long long *base)
{
	unsigned long long allocated = 0;
	const struct tetris_modem_allocator ops = {
		.alloc = allocate_window,
		.release = release_window,
		.ctx = &allocated,
	};
	int ret;

	if (!base)
		return -EINVAL;
	ret = tetris_modem_reserve(fdt, &ops);
	if (!ret)
		*base = allocated;
	return ret;
}
#endif
