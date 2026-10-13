/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_LINUX_FDT_PREPARE_H
#define __TETRIS_LINUX_FDT_PREPARE_H
#ifndef TETRIS_BROM_BOARD_HOST_TEST
#include <cpu_func.h>
#include <asm/cache.h>
#endif
#include "tetris_linux_fdt_bounds.h"

/* image_setup_libfdt removes relocation padding before board_prep_linux.
 * Own fresh space before SCP audio reservations and late /chosen reports.
 */
static int tetris_linux_fdt_prepare(struct bootm_headers *images)
{
	phys_addr_t address = 0x80000000ULL;
	struct lmb *lmb = lmb_get();
	struct lmb_region *region;
	bool owned = false, dram = false;
	void *final;
	size_t capacity;
	int ret;

	ret = tetris_linux_fdt_sync(images);
	if (ret)
		return ret;
	if (images->ft_len > 2 * 1024 * 1024 - 16384 - 4095)
		return -E2BIG;
	capacity = (images->ft_len + 16384 + 4095) & ~4095UL;
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 4096, &address, capacity,
			    LMB_NOOVERWRITE);
	if (ret)
		return ret;
	/* _lmb_alloc_base can return zero without setting addr on insertion failure.
	 * Never map or free an unchanged ceiling or an unproven reservation.
	 */
	if (address >= 0x80000000ULL || (address & 4095) ||
	    capacity > 0x80000000ULL - address)
		return -ERANGE;
	alist_for_each(region, &lmb->used_mem) {
		if (region->size > (phys_size_t)-1 - region->base ||
		    (region->flags & (LMB_NOMAP | LMB_NOOVERWRITE)) != LMB_NOOVERWRITE ||
		    address < region->base || capacity > region->size ||
		    address - region->base > region->size - capacity)
			continue;
		owned = true;
	}
	alist_for_each(region, &lmb->available_mem) {
		if (region->size > (phys_size_t)-1 - region->base ||
		    region->flags & LMB_NOMAP || address < region->base ||
		    capacity > region->size || address - region->base > region->size - capacity)
			continue;
		dram = true;
	}
	if (!owned || !dram)
		return -ERANGE;
	final = map_sysmem(address, capacity);
	if (!final) {
		ret = -ENOMEM;
		goto free;
	}
	ret = fdt_open_into(images->ft_addr, final, capacity);
	if (!ret)
		ret = fdt_add_mem_rsv(final, address, capacity);
	if (!ret)
		ret = fdt_check_full(final, capacity);
	if (ret) {
		unmap_sysmem(final);
		goto free;
	}
	flush_dcache_range((unsigned long)final,
			   (unsigned long)final + capacity);
	images->ft_addr = final;
	images->ft_len = capacity;
	/* Keep both the original reservation and committed clone until handoff. */
	return 0;
free:
	lmb_free(address, capacity, LMB_NOOVERWRITE | LMB_NONOTIFY);
	return ret;
}
#endif
