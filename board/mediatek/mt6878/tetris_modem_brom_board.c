// SPDX-License-Identifier: GPL-2.0+
#include <blk.h>
#include <bootm.h>
#include <cpu_func.h>
#include <lmb.h>
#include <mapmem.h>
#include <part.h>
#include <stdio.h>
#include <asm/cache.h>
#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include "tetris_modem_final.h"
#include "tetris_modem_linux_policy.h"
#include "tetris_modem_loaded_boot.h"

static unsigned int attempted;
static int first_error;

static void put32(unsigned char *p, unsigned int value)
{
	unsigned int i;
	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)(value >> (i * 8));
}

static void put64(unsigned char *p, unsigned long long value)
{
	put32(p, (unsigned int)value);
	put32(p + 4, (unsigned int)(value >> 32));
}

static int disabled(const void *fdt, const char *compatible)
{
	int node = -1, length;
	const char *status;
	for (;;) {
		node = fdt_node_offset_by_compatible(fdt, node, compatible);
		if (node == -FDT_ERR_NOTFOUND)
			return 0;
		if (node < 0)
			return node;
		status = fdt_getprop(fdt, node, "status", &length);
		if (!status || length != 9 || memcmp(status, "disabled", 9))
			return -EBUSY;
		if (fdt_getprop(fdt, node, "ccci,modem_info_v2", &length) ||
		    fdt_getprop(fdt, node, "ccci,modem_info", &length))
			return -EEXIST;
	}
}

int tetris_modem_brom_only_board(struct bootm_headers *images)
{
	struct tetris_modem_loaded_report report;
	struct blk_desc *dev;
	struct disk_partition part;
	phys_addr_t address = 0x80000000ULL;
	void *final;
	size_t capacity;
	int ret, report_ret, chosen, publish_ret;
	unsigned char recorded[80] = { 0 };

	/* Explicit build opt-in only, not an environment/chosen permission flag. */
	if (!IS_ENABLED(CONFIG_TETRIS_MODEM_BROM_ONLY))
		return -EPERM;
	if (attempted)
		return first_error ? first_error : -EALREADY;
	attempted = 1;
	if (!images || !images->ft_addr || !images->ft_len) {
		ret = -EINVAL;
		goto fail;
	}
	if (IS_ENABLED(CONFIG_TETRIS_MODEM_LOAD_DIAGNOSTIC) ||
	    IS_ENABLED(CONFIG_TETRIS_MODEM_RESERVE_DIAGNOSTIC) ||
	    IS_ENABLED(CONFIG_TETRIS_GPUEB_FLAT_RETENTION_DIAGNOSTIC) ||
	    IS_ENABLED(CONFIG_TETRIS_GPUEB_TRANSFORM_DIAGNOSTIC)) {
		ret = -EBUSY;
		goto fail;
	}
	ret = fdt_check_full(images->ft_addr, images->ft_len);
	if (!ret)
		ret = disabled(images->ft_addr, "mediatek,mddriver");
	if (!ret)
		ret = disabled(images->ft_addr, "mediatek,mt6878-modem-power-controller");
	if (!ret)
		ret = disabled(images->ft_addr, "mediatek,mt6878-modem-preflight");
	if (ret)
		goto fail;
	/* Same user-storage descriptor as existing Tetris pmOS/rootfs boot path.
	 * Validate required GPT names; policy producer independently selects the
	 * actual UFS boot LUN and measured GFH, never assumes dev0/preloader slot.
	 */
	ret = blk_get_desc(UCLASS_SCSI, 2, &dev);
	if (ret)
		goto fail;
	ret = part_get_info_by_name(dev, "modem_a", &part);
	if (ret < 0)
		goto fail;
	ret = part_get_info_by_name(dev, "tee_a", &part);
	if (ret < 0)
		goto fail;
	if ((size_t)fdt_totalsize(images->ft_addr) > 0x200000UL - 8192) {
		ret = -E2BIG;
		goto fail;
	}
	capacity = ((size_t)fdt_totalsize(images->ft_addr) + 8192 + 4095) & ~4095UL;
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 4096, &address, capacity, LMB_NOOVERWRITE);
	if (ret)
		goto fail;
	final = map_sysmem(address, capacity);
	if (!final) {
		ret = -ENOMEM;
		goto release;
	}
	memset(final, 0, capacity);
	ret = fdt_open_into(images->ft_addr, final, capacity);
	if (!ret)
		ret = fdt_add_mem_rsv(final, address, capacity);
	chosen = ret ? ret : fdt_path_offset(final, "/chosen");
	if (!ret && chosen == -FDT_ERR_NOTFOUND)
		chosen = fdt_add_subnode(final, 0, "chosen");
	if (!ret && chosen < 0)
		ret = chosen;
	if (!ret) {
		ret = fdt_delprop(final, chosen, "nothing,modem-brom-report");
		if (ret == -FDT_ERR_NOTFOUND)
			ret = 0;
	}
	if (ret) {
		unmap_sysmem(final);
		goto release;
	}
	/* Commit the clean clone BEFORE the loader reserves anything. Therefore
	 * even failed bootstrap/cleanup retains all acquired MD memory in Linux's
	 * DT. This is NOT a CCCI handoff or readiness publication.
	 */
	images->ft_addr = final;
	images->ft_len = fdt_totalsize(final);
	ret = tetris_modem_linux_b41_once(final, dev, 'a');
	report_ret = tetris_modem_loaded_boot_report(&report);
	printf("Tetris MD BROM-only result=%d report=%d; no CCCI descriptor\n", ret, report_ret);
	if (!report_ret) {
		printf("Tetris MD stage=%u error=%d hw_stage=%u hw_error=%d cleanup=%u/%d\n",
			report.stage, report.error, report.hardware.stage, report.hardware.error,
			report.hardware.cleanup.stage, report.hardware.cleanup.error);
		printf("Tetris MD reply=%llx/%llx/%llx/%llx\n",
			report.hardware.reply[0], report.hardware.reply[1],
			report.hardware.reply[2], report.hardware.reply[3]);
	}
	/* Observation-only LE record, explicitly NOT a kernel CCCI descriptor.
	 * Recover node offset after the loader's transactional DT reservations.
	 */
	put32(recorded, 1); /* record format version */
	put32(recorded + 4, (unsigned int)ret);
	put32(recorded + 8, (unsigned int)report_ret);
	if (!report_ret) {
		put32(recorded + 12, report.stage);
		put32(recorded + 16, (unsigned int)report.error);
		put32(recorded + 20, report.hardware.stage);
		put32(recorded + 24, (unsigned int)report.hardware.error);
		put32(recorded + 28, report.hardware.cleanup.stage);
		put32(recorded + 32, (unsigned int)report.hardware.cleanup.error);
		put32(recorded + 36, report.hardware.value);
		put64(recorded + 40, report.hardware.address);
		put64(recorded + 48, report.hardware.reply[0]);
		put64(recorded + 56, report.hardware.reply[1]);
		put64(recorded + 64, report.hardware.reply[2]);
		put64(recorded + 72, report.hardware.reply[3]);
	}
	chosen = fdt_path_offset(final, "/chosen");
	publish_ret = chosen < 0 ? chosen : fdt_setprop(final, chosen,
		"nothing,modem-brom-report", recorded, sizeof(recorded));
	if (publish_ret) {
		printf("Tetris MD observation record error=%d\n", publish_ret);
		if (!ret)
			ret = publish_ret;
	}
	/* Flush the updated reservation DT even on first hardware failure. Retain
	 * all MD allocations; never retry, free active memory, or reset implicitly.
	 */
	flush_dcache_range((unsigned long)final, (unsigned long)final + capacity);
	images->ft_len = fdt_totalsize(final);
	if (!ret && report_ret)
		ret = report_ret;
	if (!ret)
		return 0;
	goto fail;
release:
	lmb_free(address, capacity, LMB_NOOVERWRITE | LMB_NONOTIFY);
fail:
	if (!first_error)
		first_error = ret < 0 ? ret : -EPROTO;
	return first_error;
}
