// SPDX-License-Identifier: GPL-2.0+
/* Opt-in slot-A RAM placement, never permission to start the modem. */
#include <blk.h>
#include <cpu_func.h>
#include <mapmem.h>
#include <memalign.h>
#include <part.h>
#include <stdio.h>
#include <asm/cache.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#include "tetris_modem_reserve.h"
#include "tetris_modem_storage.h"
#include "tetris_scp_handoff.h"
#include "tetris_scp_security.h"

/* Independently audited stock LK SPKI, not obtained from the input image. */
static const unsigned char root_pin[32] = {
	0xe1, 0xb5, 0x23, 0x5d, 0x94, 0x11, 0x47, 0x3a,
	0x35, 0x8c, 0x75, 0x4f, 0x84, 0x84, 0x38, 0x01,
	0xb9, 0x1f, 0x05, 0xb8, 0xfb, 0x9d, 0xc4, 0x86,
	0x33, 0x93, 0xe3, 0x78, 0xe4, 0x1a, 0x11, 0x5e,
};

static int require_slot_a(struct blk_desc *dev)
{
	ALLOC_CACHE_ALIGN_BUFFER(unsigned char, block, 4096);
	struct disk_partition part;
	enum tetris_scp_slot slot;
	unsigned int sector;
	int ret;

	if (dev->blksz != 512 && dev->blksz != 4096)
		return -EPROTONOSUPPORT;
	ret = part_get_info_by_name(dev, "misc", &part);
	if (ret < 0)
		return ret;
	sector = 2048 / dev->blksz;
	if (part.blksz != dev->blksz || part.start >= dev->lba ||
	    part.size > dev->lba - part.start || part.size <= sector)
		return -ERANGE;
	if (blk_dread(dev, part.start + sector, 1, block) != 1)
		return -EIO;
	ret = tetris_scp_decode_boot_control(block + 2048 % dev->blksz, 32, &slot);
	return ret ? ret : (slot == TETRIS_SCP_SLOT_A ? 0 : -EPROTO);
}

static int synchronize(void *ctx, unsigned long start, unsigned long end)
{
	flush_dcache_range(start, end);
	return 0;
}

int tetris_modem_load_diagnostic(void *fdt)
{
	static bool attempted;
	const struct tetris_modem_cache_ops cache = { .flush = synchronize };
	struct tetris_modem_service_banks banks;
	struct tetris_modem_boot_plan plan;
	struct blk_desc *dev;
	unsigned long long base;
	unsigned char *window = NULL;
	const char *stage = "device";
	int ret, chosen;

	if (attempted)
		return -EALREADY;
	attempted = true;
	ret = blk_get_desc(UCLASS_SCSI, 2, &dev);
	if (ret)
		goto out;
	stage = "slot";
	ret = require_slot_a(dev);
	if (ret)
		goto out;
	stage = "reservation";
	ret = tetris_modem_reserve_diagnostic_window(fdt, &base);
	if (ret)
		goto out;
	window = map_sysmem(base, TETRIS_MODEM_WINDOW);
	if (!window) {
		ret = -ENOMEM;
		goto out;
	}
	stage = "authenticated-load";
	ret = tetris_modem_load_slot_b41(dev, 'a', root_pin,
			&tetris_scp_security_hw_ops, window, TETRIS_MODEM_WINDOW, 1, &plan);
	if (ret)
		goto out;
	stage = "service-layout";
	ret = tetris_modem_plan_service_banks_b41(&plan, TETRIS_MODEM_WINDOW, &banks);
	if (ret)
		goto out;
	stage = "service-initialization";
	ret = tetris_modem_initialize_smem_b41(&plan, window, banks.firmware_capacity,
		window + banks.nc_offset, banks.nc_capacity,
		window + banks.cache_offset, banks.cache_capacity, ARCH_DMA_MINALIGN, &cache);
out:
	if (window)
		unmap_sysmem(window);
	/* Keep any acquired window reserved, including after a post-copy failure. */
	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen >= 0) {
		int report;

		report = fdt_setprop_string(fdt, chosen, "nothing,modem-load-stage", stage);
		if (!report)
			report = fdt_setprop_u32(fdt, chosen, "nothing,modem-load-error", ret);
		if (!report)
			report = fdt_setprop_string(fdt, chosen, "nothing,modem-load-status",
				ret ? "failed-not-started" : "ram-loaded-not-started");
		if (!ret)
			ret = report;
	} else if (!ret) {
		ret = chosen;
	}
	printf("Tetris modem %s: %d (no reset/SMC/ready tags)\n", stage, ret);
	return ret;
}
