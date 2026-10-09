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

static int check_tree(const void *fdt, const char *name,
		      unsigned long long base, unsigned long long capacity)
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
	if (fdt_subnode_offset(fdt, parent, name) !=
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
			if (base && start < base + capacity &&
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
		if (base && address < base + capacity &&
		    base < address + length)
			return -EEXIST;
	}
	return 0;
}

static int reserve_region(void *fdt, const struct tetris_modem_allocator *ops,
			  const char *name, unsigned long long capacity,
			  unsigned long long alignment)
{
	unsigned long long base = 0;
	fdt32_t reg[4];
	void *copy;
	int ret, bytes, node;

	if (!fdt || !ops || !ops->alloc || !ops->release || !name ||
	    !capacity || capacity > TETRIS_MODEM_LIMIT || !alignment ||
	    (alignment & (alignment - 1)) || capacity % alignment)
		return -EINVAL;
	ret = fdt_check_header(fdt);
	if (ret)
		return ret;
	ret = check_tree(fdt, name, 0, capacity);
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
	if (!base || base % alignment ||
	    base > TETRIS_MODEM_LIMIT - capacity) {
		ret = -ERANGE;
		goto release;
	}
	ret = check_tree(copy, name, base, capacity);
	if (ret)
		goto release;
	node = fdt_path_offset(copy, "/reserved-memory");
	node = fdt_add_subnode(copy, node, name);
	if (node < 0) {
		ret = node;
		goto release;
	}
	reg[0] = cpu_to_fdt32(base >> 32);
	reg[1] = cpu_to_fdt32(base);
	reg[2] = cpu_to_fdt32(capacity >> 32);
	reg[3] = cpu_to_fdt32(capacity);
	ret = fdt_setprop(copy, node, "reg", reg, sizeof(reg));
	if (!ret)
		ret = fdt_setprop(copy, node, "no-map", NULL, 0);
	if (!ret)
		ret = fdt_add_mem_rsv(copy, base, capacity);
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

int tetris_modem_reserve(void *fdt, const struct tetris_modem_allocator *ops)
{
	return reserve_region(fdt, ops, "tetris-modem-diagnostic",
			      TETRIS_MODEM_WINDOW, TETRIS_MODEM_ALIGN);
}

#ifndef TETRIS_MODEM_RESERVE_HOST_TEST
struct window_allocation {
	unsigned long long capacity;
	unsigned long long alignment;
	unsigned long long base;
};

static void release_window(void *ctx, unsigned long long base)
{
	const struct window_allocation *allocation = ctx;
	long ret;

	ret = lmb_free(base, allocation->capacity, LMB_NOMAP | LMB_NOOVERWRITE);
	if (ret)
		printf("Tetris modem RAM release failed: %ld\n", ret);
}

static int allocate_window(void *ctx, unsigned long long *result)
{
	struct window_allocation *allocation = ctx;
	phys_addr_t base = TETRIS_MODEM_LIMIT;
	unsigned long long start, size;
	int ret, i;

	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, allocation->alignment, &base,
			    allocation->capacity, LMB_NOMAP | LMB_NOOVERWRITE);
	if (ret)
		return ret;
	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++) {
		start = gd->bd->bi_dram[i].start;
		size = gd->bd->bi_dram[i].size;
		if (size >= allocation->capacity && base >= start &&
		    base - start <= size - allocation->capacity) {
			*result = base;
			allocation->base = base;
			return 0;
		}
	}
	release_window(ctx, base);
	return -ERANGE;
}

int tetris_modem_reserve_diagnostic(void *fdt)
{
	unsigned long long base;

	return tetris_modem_reserve_diagnostic_window(fdt, &base);
}

int tetris_modem_reserve_diagnostic_window(void *fdt, unsigned long long *base)
{
	struct window_allocation allocation = {
		.capacity = TETRIS_MODEM_WINDOW,
		.alignment = TETRIS_MODEM_ALIGN,
	};
	const struct tetris_modem_allocator ops = {
		.alloc = allocate_window,
		.release = release_window,
		.ctx = &allocation,
	};
	int ret;

	if (!base)
		return -EINVAL;
	ret = tetris_modem_reserve(fdt, &ops);
	if (!ret)
		*base = allocation.base;
	return ret;
}

int tetris_modem_reserve_services(void *fdt, unsigned long long capacity,
				 unsigned long long *base)
{
	struct window_allocation allocation = {
		.capacity = capacity,
		.alignment = 0x10000,
	};
	const struct tetris_modem_allocator ops = {
		.alloc = allocate_window,
		.release = release_window,
		.ctx = &allocation,
	};
	int ret;

	if (!base)
		return -EINVAL;
	ret = reserve_region(fdt, &ops, "tetris-modem-service-diagnostic",
			     capacity, allocation.alignment);
	if (!ret)
		*base = allocation.base;
	return ret;
}
#endif
