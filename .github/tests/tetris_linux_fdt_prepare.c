// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

typedef uint64_t phys_addr_t;
typedef uint64_t phys_size_t;
struct lmb_region { phys_addr_t base; phys_size_t size; unsigned int flags; };
struct alist { struct lmb_region *entries; unsigned int count; };
struct lmb { struct alist used_mem, available_mem; };
struct bootm_headers { void *ft_addr; unsigned long ft_len; };
static struct lmb_region regions[2];
static struct lmb memory;
static void *allocation;
static size_t allocated_size;
static int fault, frees, unmaps, flushes, maps;
static struct lmb *lmb_get(void) { return &memory; }
static phys_addr_t map_to_sysmem(const void *p)
{
	return p == allocation && p ? 0x7f000000ULL : (uintptr_t)p;
}
#define alist_for_each(p, list) \
	for ((p) = (list)->entries; (p) < (list)->entries + (list)->count; (p)++)
#define LMB_MEM_ALLOC_MAX 1
#define LMB_NOOVERWRITE 2
#define LMB_NONOTIFY 4
#define LMB_NOMAP 8
static int lmb_alloc_mem(int mode, int align, phys_addr_t *address,
			 size_t size, int flags)
{
	assert(mode == 1 && align == 4096 && flags == 2);
	assert(*address == 0x80000000ULL && !(size % 4096));
	if (fault == 1)
		return -ENOMEM;
	if (fault == 6)
		return 0;
	allocation = calloc(1, size);
	assert(allocation);
	allocated_size = size;
	*address = 0x7f000000ULL;
	regions[1] = (struct lmb_region){ *address, size, LMB_NOOVERWRITE };
	if (fault != 7)
		memory.used_mem.count = 2;
	memory.available_mem.count = 2;
	return 0;
}
static void *map_sysmem(phys_addr_t address, size_t size)
{
	assert(address == 0x7f000000ULL && size == allocated_size);
	maps++;
	return fault == 2 ? NULL : allocation;
}
static void unmap_sysmem(void *p) { assert(p == allocation); unmaps++; }
static int lmb_free(phys_addr_t address, size_t size, int flags)
{
	assert(address == 0x7f000000ULL && size == allocated_size && flags == 6);
	free(allocation);
	allocation = NULL;
	frees++;
	return 0;
}
static void flush_dcache_range(unsigned long start, unsigned long end)
{
	assert(start == (uintptr_t)allocation && end - start == allocated_size);
	flushes++;
}
static int mock_open(const void *source, void *target, int size)
{
	return fault == 3 ? -FDT_ERR_BADSTRUCTURE : fdt_open_into(source, target, size);
}
static int mock_reserve(void *fdt, uint64_t address, uint64_t size)
{
	return fault == 4 ? -FDT_ERR_NOSPACE : fdt_add_mem_rsv(fdt, address, size);
}
static int mock_check(const void *fdt, size_t size)
{
	return fault == 5 && fdt == allocation ? -FDT_ERR_BADSTRUCTURE :
		fdt_check_full(fdt, size);
}
#define fdt_open_into mock_open
#define fdt_add_mem_rsv mock_reserve
#define fdt_check_full mock_check
#define TETRIS_BROM_BOARD_HOST_TEST 1
#include "../../board/mediatek/mt6878/tetris_linux_fdt_prepare.h"
#undef fdt_open_into
#undef fdt_add_mem_rsv
#undef fdt_check_full

int main(void)
{
	char original[4096], copy[4096];
	struct bootm_headers images;
	unsigned long original_size;
	int chosen, i;
	assert(!fdt_create_empty_tree(original, sizeof(original)));
	assert(!fdt_add_subnode(original, 0, "chosen"));
	assert(!fdt_add_mem_rsv(original, 0x9d000000, 0x9c0000));
	assert(!fdt_pack(original));
	original_size = fdt_totalsize(original);
	memcpy(copy, original, original_size);
	chosen = fdt_path_offset(copy, "/chosen");
	/* Reproduce a late report refused by bootm's packed DT. */
	assert(fdt_setprop_string(copy, chosen, "nothing,scp-prepare-stage",
				  "secure-handoff-prepared") == -FDT_ERR_NOSPACE);
	memcpy(copy, original, original_size);
	for (i = 0; i <= 7; i++) {
		fault = i; frees = unmaps = flushes = maps = 0;
		regions[0] = (struct lmb_region){ (uintptr_t)original, original_size, 0 };
		memory = (struct lmb){ { regions, 1 }, { regions, 1 } };
		images = (struct bootm_headers){ original, sizeof(original) };
		assert((tetris_linux_fdt_prepare(&images) == 0) == (i == 0));
		assert(!memcmp(original, copy, original_size));
		if (i) {
			assert(images.ft_addr == original && images.ft_len == original_size);
			assert(frees == (i >= 2 && i <= 5));
			assert(unmaps == (i >= 3 && i <= 5) && !flushes);
			assert(maps == (i >= 2 && i <= 5));
			free(allocation); allocation = NULL;
			continue;
		}
		assert(images.ft_addr == allocation && images.ft_len == allocated_size);
		assert(!frees && !unmaps && flushes == 1);
		assert(fdt_num_mem_rsv(allocation) == 2);
		{
			uint64_t address, size;
			assert(!fdt_get_mem_rsv(allocation, 1, &address, &size));
			assert(address == 0x7f000000ULL && size == allocated_size);
		}
		assert(!tetris_linux_fdt_sync(&images));
		chosen = fdt_path_offset(allocation, "/chosen");
		assert(!fdt_setprop_u32(allocation, chosen, "nothing,scp-secure-state", 3));
		assert(!fdt_setprop_u32(allocation, chosen, "nothing,scp-secure-operation", 1));
		assert(!fdt_setprop_u64(allocation, chosen, "nothing,scp-secure-error", 0));
		assert(!fdt_setprop_string(allocation, chosen, "nothing,scp-prepare-stage",
					  "secure-handoff-prepared"));
		assert(!fdt_setprop_u32(allocation, chosen, "nothing,scp-prepare-error", 0));
		assert(!fdt_check_full(allocation, allocated_size));
		free(allocation); allocation = NULL;
	}
	/* Bounds admission must fail before even attempting allocation. */
	fault = 0;
	regions[0].size = sizeof(struct fdt_header) - 1;
	images = (struct bootm_headers){ original, original_size };
	assert(tetris_linux_fdt_prepare(&images) == -ERANGE && !allocation);
	puts("late Linux DT owned-space and publication regression passed");
	return 0;
}
