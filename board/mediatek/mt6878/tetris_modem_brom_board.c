// SPDX-License-Identifier: GPL-2.0+
#ifndef TETRIS_BROM_BOARD_HOST_TEST
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
#endif
#include "tetris_linux_fdt_bounds.h"

static unsigned int attempted;
static int first_error;

enum brom_board_stage {
	BROM_INPUT = 1, BROM_FDT, BROM_CAPACITY, BROM_ALLOC, BROM_MAP,
	BROM_CLONE, BROM_RESERVATION, BROM_PLACEHOLDER, BROM_CONFIG,
	BROM_CONSUMERS, BROM_STORAGE, BROM_MODEM_GPT, BROM_TEE_GPT,
	BROM_POLICY, BROM_LOADER_REPORT, BROM_FINISHED,
};

static void latch(int error)
{
	if (error && !first_error)
		first_error = error < 0 ? error : -EPROTO;
}

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

/* Independent observation fields; never permission to execute hardware.
 * Try every field even if another fails, preserving the first publication error.
 */
static int publish(void *fdt, const unsigned char recorded[80], unsigned int stage,
		   unsigned int status, unsigned int entered, int publication_error)
{
	const char *names[] = {
		"nothing,modem-brom-preflight-stage",
		"nothing,modem-brom-preflight-status",
		"nothing,modem-brom-preflight-error",
		"nothing,modem-brom-loader-entered",
		"nothing,modem-brom-publication-error",
	};
	unsigned int values[] = { stage, status, (unsigned int)first_error, entered,
				  (unsigned int)publication_error };
	unsigned int i;
	int chosen, ret, error = 0;

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen == -FDT_ERR_NOTFOUND)
		chosen = fdt_add_subnode(fdt, 0, "chosen");
	if (chosen < 0)
		return chosen;
	ret = fdt_setprop(fdt, chosen, "nothing,modem-brom-report", recorded, 80);
	if (ret)
		error = ret;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		chosen = fdt_path_offset(fdt, "/chosen");
		ret = chosen < 0 ? chosen : fdt_setprop_u32(fdt, chosen, names[i], values[i]);
		if (ret && !error)
			error = ret;
	}
	return error;
}

int tetris_modem_brom_only_board(struct bootm_headers *images)
{
	struct tetris_modem_loaded_report report;
	struct blk_desc *dev;
	struct disk_partition part;
	phys_addr_t address = 0x80000000ULL;
	void *final = NULL, *original = NULL;
	size_t capacity = 0;
	unsigned int stage = BROM_INPUT, valid = 0, allocated = 0, committed = 0;
	unsigned int entered = 0;
	int ret, report_ret = -EINPROGRESS, publish_ret, cleanup_ret;
	int publication_error = 0;
	unsigned char recorded[80] = { 0 };

	/* Explicit build opt-in only, not an environment/chosen permission flag. */
	if (!IS_ENABLED(CONFIG_TETRIS_MODEM_BROM_ONLY))
		return -EPERM;
	if (attempted)
		return first_error ? first_error : -EALREADY;
	attempted = 1;
	put32(recorded, 1);
	put32(recorded + 4, (unsigned int)-EINPROGRESS);
	put32(recorded + 8, (unsigned int)-EINPROGRESS);
	if (!images || !images->ft_addr || !images->ft_len) {
		ret = -EINVAL;
		goto fail;
	}
	original = images->ft_addr; /* Borrowed virtual pointer, not a physical address. */
	stage = BROM_FDT;
	ret = tetris_linux_fdt_sync(images);
	if (ret)
		goto fail;
	valid = 1;
	stage = BROM_CAPACITY;
	if ((size_t)fdt_totalsize(images->ft_addr) > 0x200000UL - 8192) {
		ret = -E2BIG;
		goto fail;
	}
	capacity = ((size_t)fdt_totalsize(images->ft_addr) + 8192 + 4095) & ~4095UL;
	stage = BROM_ALLOC;
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 4096, &address, capacity, LMB_NOOVERWRITE);
	if (ret)
		goto fail;
	allocated = 1;
	stage = BROM_MAP;
	final = map_sysmem(address, capacity);
	if (!final) {
		ret = -ENOMEM;
		goto fail;
	}
	memset(final, 0, capacity);
	stage = BROM_CLONE;
	ret = fdt_open_into(images->ft_addr, final, capacity);
	if (ret)
		goto fail;
	stage = BROM_RESERVATION;
	ret = fdt_add_mem_rsv(final, address, capacity);
	if (ret)
		goto fail;
	stage = BROM_PLACEHOLDER;
	ret = publish(final, recorded, stage, 0, 0, 0);
	if (ret) {
		publication_error = ret;
		goto fail;
	}
	/* Commit the clean clone BEFORE the loader reserves anything. Therefore
	 * even failed bootstrap/cleanup retains all acquired MD memory in Linux's
	 * DT. This is NOT a CCCI handoff or readiness publication.
	 */
	images->ft_addr = final;
	images->ft_len = fdt_totalsize(final);
	committed = 1;
	stage = BROM_CONFIG;
	if (IS_ENABLED(CONFIG_TETRIS_MODEM_LOAD_DIAGNOSTIC) ||
	    IS_ENABLED(CONFIG_TETRIS_MODEM_RESERVE_DIAGNOSTIC) ||
	    IS_ENABLED(CONFIG_TETRIS_GPUEB_FLAT_RETENTION_DIAGNOSTIC) ||
	    IS_ENABLED(CONFIG_TETRIS_GPUEB_TRANSFORM_DIAGNOSTIC)) {
		ret = -EBUSY;
		goto fail;
	}
	stage = BROM_CONSUMERS;
	ret = disabled(final, "mediatek,mddriver");
	if (!ret)
		ret = disabled(final, "mediatek,mt6878-modem-power-controller");
	if (!ret)
		ret = disabled(final, "mediatek,mt6878-modem-preflight");
	if (ret)
		goto fail;
	stage = BROM_STORAGE;
	ret = blk_get_desc(UCLASS_SCSI, 2, &dev);
	if (ret)
		goto fail;
	stage = BROM_MODEM_GPT;
	ret = part_get_info_by_name(dev, "modem_a", &part);
	if (ret < 0)
		goto fail;
	stage = BROM_TEE_GPT;
	ret = part_get_info_by_name(dev, "tee_a", &part);
	if (ret < 0)
		goto fail;
	stage = BROM_POLICY;
	ret = publish(final, recorded, stage, 0, 1, 0);
	if (ret) {
		publication_error = ret;
		goto fail;
	}
	entered = 1;
	flush_dcache_range((unsigned long)final, (unsigned long)final + capacity);
	images->ft_len = fdt_totalsize(final);
	ret = tetris_modem_linux_b41_once(final, dev, 'a');
	latch(ret);
	stage = BROM_LOADER_REPORT;
	report_ret = tetris_modem_loaded_boot_report(&report);
	latch(report_ret);
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
	if (!first_error)
		stage = BROM_FINISHED;
	ret = first_error;
fail:
	latch(ret);
	/* Before the clone commits, only a validated borrowed DT may be touched.
	 * Failure publication is best-effort, never a hardware retry or fallback.
	 */
	if (valid) {
		void *target = committed ? final : original;

		if (!entered) {
			put32(recorded + 4, (unsigned int)first_error);
			put32(recorded + 8, (unsigned int)-EINPROGRESS);
		}
		publish_ret = publish(target, recorded, stage, first_error ? 2 : 1,
				      entered, publication_error);
		if (publish_ret) {
			if (!publication_error)
				publication_error = publish_ret;
			latch(publish_ret);
			/* A second observation-only update may expose the publication error;
			 * it must not overwrite the first operational error or run hardware.
			 */
			publish(target, recorded, stage, 2, entered, publication_error);
			printf("Tetris MD report publication error=%d first=%d\n",
			       publish_ret, first_error);
		}
		flush_dcache_range((unsigned long)target,
			(unsigned long)target + (committed ? capacity : images->ft_len));
		images->ft_len = fdt_totalsize(target);
	}
	if (!committed) {
		if (final)
			unmap_sysmem(final);
		if (allocated) {
			cleanup_ret = lmb_free(address, capacity, LMB_NOOVERWRITE | LMB_NONOTIFY);
			latch(cleanup_ret);
		}
	}
	printf("Tetris MD board stage=%u first=%d loader-entered=%u\n",
	       stage, first_error, entered);
	return first_error;
}
