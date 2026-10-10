// SPDX-License-Identifier: GPL-2.0+
#include <asm/cache.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/u-boot.h>
#include <cpu_func.h>
#include <mapmem.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_modem_loaded_boot.h"
#include "tetris_modem_ccci_tags.h"
#include "tetris_modem_final.h"
#include "tetris_modem_storage.h"
#include "tetris_modem_reserve.h"
#include "tetris_scp_security.h"
DECLARE_GLOBAL_DATA_PTR;

/* Same independently audited manufacturer root as existing load diagnostic. */
static const unsigned char root_pin[32] = {
	0xe1, 0xb5, 0x23, 0x5d, 0x94, 0x11, 0x47, 0x3a,
	0x35, 0x8c, 0x75, 0x4f, 0x84, 0x84, 0x38, 0x01,
	0xb9, 0x1f, 0x05, 0xb8, 0xfb, 0x9d, 0xc4, 0x86,
	0x33, 0x93, 0xe3, 0x78, 0xe4, 0x1a, 0x11, 0x5e,
};

static struct {
	unsigned int attempted;
	struct tetris_modem_loaded_report report;
	struct tetris_modem_boot_plan loaded;
	unsigned char check_header[512];
} owner;

static int fail(int ret)
{
	if (!owner.report.error)
		owner.report.error = ret < 0 ? ret : -EPROTO;
	return owner.report.error;
}

static int phy_capacity(const char *text, unsigned long long *capacity)
{
	unsigned int n = 0, i;

	*capacity = 0;
	if (!text || !*text)
		return 0;
	for (i = 0; i < 10; i++) {
		if (!text[i]) {
			if (n > 4095)
				return -ERANGE;
			*capacity = n > 1536 ? 0x60000000ULL : (unsigned long long)n << 20;
			return 0;
		}
		if (text[i] < '0' || text[i] > '9' ||
		    n > (0xffffffffU - (unsigned int)(text[i] - '0')) / 10)
			return -EINVAL;
		n = n * 10 + (unsigned int)(text[i] - '0');
	}
	return -EINVAL;
}

static int cold_off(void)
{
	static const unsigned long addresses[] = {
		0x1c001e00UL, 0x1c001f24UL, 0x10001c5cUL, 0x10001c4cUL, 0x1027008cUL,
	};
	static const unsigned int masks[] = { 4U | TETRIS_MD_POWER_ACK, 3, 0x200, 0x800, 0xc0 };
	static const unsigned int expected[] = { 0, 3, 0x200, 0x800, 0xc0 };
	unsigned int i;

	/* Before ANY placed-RAM/service writes; repeated by bootstrap before EMI. */
	for (i = 0; i < 5; i++) {
		owner.report.address = addresses[i];
		owner.report.value = readl((const volatile void *)addresses[i]);
		if ((owner.report.value & masks[i]) != expected[i])
			return -EBUSY;
	}
	return 0;
}

static int find_dram(struct tetris_modem_emi_resources *resources)
{
	const struct tetris_modem_emi_window *w[] = {
		&resources->firmware, &resources->nc, &resources->cache, &resources->sib,
	};
	int bank;
	unsigned int i;

	if (!gd || !gd->bd)
		return -EINVAL;
	for (bank = 0; bank < CONFIG_NR_DRAM_BANKS; bank++) {
		unsigned long long start = gd->bd->bi_dram[bank].start;
		unsigned long long size = gd->bd->bi_dram[bank].size;

		if (!size || start > ~0ULL - size)
			continue;
		for (i = 0; i < 4; i++) {
			if (!w[i]->capacity)
				continue;
			if (w[i]->base < start || w[i]->base - start >= size ||
			    w[i]->capacity > size - (w[i]->base - start))
				break;
		}
		if (i == 4) {
			resources->dram_base = start;
			resources->dram_size = size;
			return 0;
		}
	}
	return -ERANGE;
}

static int synchronize(void *ctx, unsigned long start, unsigned long end)
{
	(void)ctx;
	flush_dcache_range(start, end);
	return 0;
}

int tetris_modem_loaded_boot_once(void *fdt, struct blk_desc *dev, char slot,
		unsigned int ccb_gear, const char *phy_gear,
		const unsigned char preloader_sha256[32])
{
	struct tetris_modem_bootstrap_plan *boot = &owner.report.bootstrap;
	struct tetris_modem_emi_resources *r = &boot->resources;
	const struct tetris_modem_cache_ops cache = { .flush = synchronize };
	struct tetris_modem_boot_plan *loaded = &owner.loaded;
	unsigned long long policy[8];
	unsigned char *firmware = NULL, *nc = NULL, *cached = NULL;
	int ret;

	if (owner.attempted)
		return owner.report.error ? owner.report.error : -EALREADY;
	owner.attempted = 1;
	owner.report.stage = TETRIS_MD_LOAD_INPUT;
	if (!fdt || !dev || !preloader_sha256 || (slot != 'a' && slot != 'b') ||
	    (current_el() != 1 && current_el() != 2))
		return fail(-EINVAL);
	memcpy(boot->preloader_sha256, preloader_sha256, 32);
	ret = tetris_modem_plan_emi_policy(boot->preloader_sha256, 32, policy);
	if (!ret)
		ret = phy_capacity(phy_gear, &r->sib.capacity);
	if (ret)
		return fail(ret);
	owner.report.stage = TETRIS_MD_LOAD_PROFILE;
	ret = tetris_scp_check_atf_profile(dev);
	if (ret)
		return fail(ret);
	owner.report.stage = TETRIS_MD_LOAD_OFF;
	ret = cold_off();
	if (ret)
		return fail(ret);
	owner.report.stage = TETRIS_MD_LOAD_FIRMWARE_RESERVE;
	r->firmware.capacity = TETRIS_MODEM_WINDOW;
	ret = tetris_modem_reserve_diagnostic_window(fdt, &r->firmware.base);
	if (ret)
		return fail(ret);
	owner.report.stage = TETRIS_MD_LOAD_NC_RESERVE;
	r->nc.capacity = 0x8000000ULL;
	ret = tetris_modem_reserve_boot_bank(fdt, 0, r->nc.capacity, &r->nc.base);
	if (ret)
		return fail(ret);
	owner.report.stage = TETRIS_MD_LOAD_CACHE_RESERVE;
	r->cache.capacity = 0x8000000ULL;
	ret = tetris_modem_reserve_boot_bank(fdt, 1, r->cache.capacity, &r->cache.base);
	if (ret)
		return fail(ret);
	if (r->sib.capacity) {
		owner.report.stage = TETRIS_MD_LOAD_SIB_RESERVE;
		ret = tetris_modem_reserve_boot_bank(fdt, 2, r->sib.capacity, &r->sib.base);
		if (ret)
			return fail(ret);
	}
	owner.report.stage = TETRIS_MD_LOAD_MAP;
	ret = find_dram(r);
	if (ret)
		return fail(ret);
	firmware = map_sysmem(r->firmware.base, r->firmware.capacity);
	nc = map_sysmem(r->nc.base, r->nc.capacity);
	cached = map_sysmem(r->cache.base, r->cache.capacity);
	if (!firmware || !nc || !cached) {
		ret = -ENOMEM;
		goto out;
	}
	owner.report.stage = TETRIS_MD_LOAD_AUTH_COPY;
	ret = tetris_modem_load_slot_handoff_b41(dev, slot, root_pin,
		&tetris_scp_security_hw_ops, firmware, r->firmware.capacity, ccb_gear,
		phy_gear, boot->preloader_sha256, r, loaded, &boot->rows,
		owner.check_header);
	if (ret)
		goto out;
	owner.report.stage = TETRIS_MD_LOAD_SERVICES;
	ret = tetris_modem_initialize_smem_b41(loaded, firmware, r->firmware.capacity,
		nc, r->nc.capacity, cached, r->cache.capacity, ARCH_DMA_MINALIGN, &cache);
	if (ret)
		goto out;
	owner.report.stage = TETRIS_MD_LOAD_BOOTSTRAP;
	ret = tetris_modem_bootstrap_once(dev, boot);
	if (tetris_modem_bootstrap_report(&owner.report.hardware) && !ret)
		ret = -EPROTO;
	if (!ret)
		owner.report.stage = TETRIS_MD_LOAD_COMPLETE;
out:
	/* Drop CPU mappings only; NEVER free any acquired final reservation. */
	if (cached)
		unmap_sysmem(cached);
	if (nc)
		unmap_sysmem(nc);
	if (firmware)
		unmap_sysmem(firmware);
	return ret ? fail(ret) : 0;
}

int tetris_modem_loaded_boot_report(struct tetris_modem_loaded_report *out)
{
	if (!out || !owner.attempted)
		return -EINVAL;
	*out = owner.report;
	return 0;
}

/* No arbitrary caller-supplied metadata/report can acquire publication. */
int tetris_modem_loaded_encode_tags(void *buffer, size_t size)
{
	if (!owner.attempted || owner.report.stage != TETRIS_MD_LOAD_COMPLETE)
		return -EAGAIN;
	if (owner.report.error)
		return owner.report.error;
	return tetris_modem_build_linux_tags(&owner.loaded, owner.check_header,
		&owner.report, buffer, size);
}
