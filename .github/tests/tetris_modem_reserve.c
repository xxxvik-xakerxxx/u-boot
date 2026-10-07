// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <stdio.h>
#define TETRIS_MODEM_RESERVE_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_modem_reserve.c"

static unsigned long long allocation;
static int alloc_error, allocations, releases;

static int allocate(void *ctx, unsigned long long *base)
{
	assert(ctx == &allocation);
	allocations++;
	*base = allocation;
	return alloc_error;
}

static void release(void *ctx, unsigned long long base)
{
	assert(ctx == &allocation && base == allocation);
	releases++;
}

static const struct tetris_modem_allocator ops = {
	.alloc = allocate, .release = release, .ctx = &allocation,
};

static int fresh(void *fdt)
{
	int parent;

	memset(fdt, 0, 4096);
	assert(!fdt_create_empty_tree(fdt, 4096));
	assert(!fdt_setprop_u32(fdt, 0, "#address-cells", 2));
	assert(!fdt_setprop_u32(fdt, 0, "#size-cells", 2));
	parent = fdt_add_subnode(fdt, 0, "reserved-memory");
	assert(parent >= 0);
	assert(!fdt_setprop_u32(fdt, parent, "#address-cells", 2));
	assert(!fdt_setprop_u32(fdt, parent, "#size-cells", 2));
	assert(!fdt_setprop(fdt, parent, "ranges", NULL, 0));
	allocation = 0x180000000ULL;
	alloc_error = allocations = releases = 0;
	return parent;
}

static void unchanged_failure(void *fdt, int expected_allocations)
{
	unsigned char before[4096];

	memcpy(before, fdt, sizeof(before));
	assert(tetris_modem_reserve(fdt, &ops) < 0);
	assert(!memcmp(before, fdt, sizeof(before)));
	assert(allocations == expected_allocations);
	assert(releases == (expected_allocations && !alloc_error));
}

int main(void)
{
	unsigned long long fdt[512];
	uint64_t start, size;
	const fdt32_t *reg;
	fdt32_t overlap[4];
	int parent, node, len, i;

	for (i = 0; i < 2; i++) {
		fresh(fdt);
		if (i)
			allocation = TETRIS_MODEM_LIMIT - TETRIS_MODEM_WINDOW;
		assert(!tetris_modem_reserve(fdt, &ops));
		assert(allocations == 1 && !releases);
		node = fdt_path_offset(fdt, "/reserved-memory/tetris-modem-diagnostic");
		assert(node >= 0);
		reg = fdt_getprop(fdt, node, "reg", &len);
		assert(reg && len == 16 && read_pair(reg) == allocation);
		assert(read_pair(reg + 2) == TETRIS_MODEM_WINDOW);
		assert(fdt_getprop(fdt, node, "no-map", &len) && !len);
		assert(fdt_num_mem_rsv(fdt) == 1);
		assert(!fdt_get_mem_rsv(fdt, 0, &start, &size));
		assert(start == allocation && size == TETRIS_MODEM_WINDOW);
		allocations = 0;
		unchanged_failure(fdt, 0);
	}
	for (i = 0; i < 4; i++) {
		fresh(fdt);
		allocation = i == 0 ? 0 : i == 1 ? 1 :
			i == 2 ? TETRIS_MODEM_LIMIT : ~0ULL;
		unchanged_failure(fdt, 1);
	}
	fresh(fdt);
	alloc_error = -ENOMEM;
	unchanged_failure(fdt, 1);
	parent = fresh(fdt);
	assert(!fdt_setprop_u32(fdt, parent, "#address-cells", 1));
	unchanged_failure(fdt, 0);
	parent = fresh(fdt);
	assert(!fdt_delprop(fdt, parent, "ranges"));
	unchanged_failure(fdt, 0);
	parent = fresh(fdt);
	node = fdt_add_subnode(fdt, parent, "existing");
	assert(node >= 0);
	overlap[0] = cpu_to_fdt32(allocation >> 32);
	overlap[1] = cpu_to_fdt32(allocation);
	overlap[2] = 0;
	overlap[3] = cpu_to_fdt32(4096);
	assert(!fdt_setprop(fdt, node, "reg", overlap, sizeof(overlap)));
	unchanged_failure(fdt, 1);
	assert(!fdt_setprop(fdt, node, "reg", overlap, 4));
	allocations = releases = 0;
	unchanged_failure(fdt, 0);
	fresh(fdt);
	assert(!fdt_add_mem_rsv(fdt, allocation + 4096, 4096));
	unchanged_failure(fdt, 1);
	/* Exercise partial-edit failures at every available-space boundary. */
	for (i = 0; i < 256; i++) {
		fresh(fdt);
		assert(!fdt_pack(fdt));
		fdt_set_totalsize(fdt, fdt_totalsize(fdt) + i);
		if (tetris_modem_reserve(fdt, &ops)) {
			fresh(fdt);
			assert(!fdt_pack(fdt));
			fdt_set_totalsize(fdt, fdt_totalsize(fdt) + i);
			unchanged_failure(fdt, 1);
		}
	}
	puts("modem reservation: allocation, conflicts and atomic DT publication passed");
	return 0;
}
