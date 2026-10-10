/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_LINUX_FDT_BOUNDS_H
#define __TETRIS_LINUX_FDT_BOUNDS_H
#ifndef TETRIS_BROM_BOARD_HOST_TEST
#include <bootm.h>
#include <lmb.h>
#include <mapmem.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#endif

/* image_setup_libfdt shrinks/re-reserves the relocated DT but does not update
 * images->ft_len. Prove readable DRAM and reservation bounds before inspecting
 * its header; never extend a bound using the header's own assertion of size.
 */
static int tetris_linux_fdt_sync(struct bootm_headers *images)
{
	struct lmb *lmb = lmb_get();
	struct lmb_region *region;
	phys_addr_t address;
	phys_size_t owned = 0, dram = 0, remaining;
	unsigned int total;
	int ret;

	if (!images || !images->ft_addr || !lmb)
		return -EINVAL;
	address = map_to_sysmem(images->ft_addr);
	alist_for_each(region, &lmb->used_mem) {
		if (region->size > (phys_size_t)-1 - region->base ||
		    region->flags & LMB_NOMAP || address < region->base ||
		    address - region->base >= region->size)
			continue;
		remaining = region->size - (address - region->base);
		if (remaining > owned)
			owned = remaining;
	}
	alist_for_each(region, &lmb->available_mem) {
		if (region->size > (phys_size_t)-1 - region->base ||
		    region->flags & LMB_NOMAP || address < region->base ||
		    address - region->base >= region->size)
			continue;
		remaining = region->size - (address - region->base);
		if (remaining > dram)
			dram = remaining;
	}
	if (owned < sizeof(struct fdt_header) || dram < sizeof(struct fdt_header))
		return -ERANGE;
	/* Header access is now bounded, but its declared whole-tree size is not yet. */
	total = fdt_totalsize(images->ft_addr);
	if (total < sizeof(struct fdt_header) || total > owned || total > dram)
		return -ERANGE;
	ret = fdt_check_full(images->ft_addr, total);
	if (ret)
		return ret;
	images->ft_len = total;
	return 0;
}
#endif
