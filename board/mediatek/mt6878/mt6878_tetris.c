// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#include <config.h>
#include <bootm.h>
#include <command.h>
#include <env.h>
#include <fastboot.h>
#include <fdtdec.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/system.h>
#include <linux/arm-smccc.h>
#include <linux/libfdt.h>
#include <string.h>

DECLARE_GLOBAL_DATA_PTR;

#define MTK_SIP_CONNSYS_EMI_SET		0xc200041a
#define MTK_SIP_CONNSYS_EMI_MAIN	0
#define MTK_SIP_CONNSYS_EMI_MD		1
#define MTK_SIP_CONNSYS_EMI_GPS		2
#define MTK_SIP_CONNSYS_EMI_SUCCESS	0x100
#define MTK_SIP_EMIMPU_SET		0x82000415
#define MTK_SIP_EMIMPU_PAGE_SHIFT	12

#define TETRIS_CONNSYS_EMI_BASE		0x85e00000ULL
#define TETRIS_CONNSYS_EMI_SIZE		0x00c00000ULL
#define TETRIS_GPS_EMI_BASE		0x86a00000ULL
#define TETRIS_GPS_EMI_SIZE		0x00100000ULL
#define TETRIS_MD_CACHE_EMI_BASE	0x88000000ULL
#define TETRIS_MD_CACHE_EMI_SIZE	0x02560000ULL
#define TETRIS_MD_CONNSYS_EMI_SIZE	0x00d80000ULL

struct tetris_emimpu_region {
	u64 start;
	u64 end;
	unsigned long id;
	const char *name;
};

static const struct tetris_emimpu_region tetris_connsys_emimpu_regions[] = {
	{ 0x85e00000ULL, 0x86480000ULL, 0x2c, "conninfra RO" },
	{ 0x86480000ULL, 0x86900000ULL, 0x2e, "conninfra RW" },
	{ 0x86900000ULL, 0x87f00000ULL, 0x2f, "GPS and WiFi DMA" },
	{ 0x87f00000ULL, 0x87f20000ULL, 0x2d, "connscp shared" },
};

static int tetris_set_emimpu_region(const struct tetris_emimpu_region *region)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_EMIMPU_SET, 0,
		      region->start >> MTK_SIP_EMIMPU_PAGE_SHIFT,
		      region->end >> MTK_SIP_EMIMPU_PAGE_SHIFT,
		      region->id, 0, 0, 0, &res);
	if (res.a0) {
		printf("Tetris: %s EMI MPU region %lx failed: %lx\n",
		       region->name, region->id, res.a0);
		return -EIO;
	}

	printf("Tetris: %s EMI MPU region %lx set at %llx..%llx\n",
	       region->name, region->id,
	       (unsigned long long)region->start,
	       (unsigned long long)region->end);

	return 0;
}

static int tetris_set_connsys_emimpu_regions(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(tetris_connsys_emimpu_regions); i++) {
		if (tetris_connsys_emimpu_regions[i].id == 0x2c) {
			printf("Tetris: defer conninfra RO protection for Linux FWDL\n");
			continue;
		}
		ret = tetris_set_emimpu_region(&tetris_connsys_emimpu_regions[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int tetris_set_connsys_emi(unsigned long selector, u64 base, u64 size,
				  const char *name)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_smc(MTK_SIP_CONNSYS_EMI_SET, selector, base, size,
		      0, 0, 0, 0, &res);
	if (res.a0 != MTK_SIP_CONNSYS_EMI_SUCCESS) {
		printf("Tetris: %s EMI SMC failed: %lx\n", name, res.a0);
		return -EIO;
	}

	printf("Tetris: %s EMI registered at %llx, size %llx\n", name,
	       (unsigned long long)base, (unsigned long long)size);

	return 0;
}

static int tetris_prepare_connsys_emi(const void *fdt)
{
	fdt_size_t md_reserved_size, reserved_size;
	fdt_addr_t base, md_base;
	const fdt32_t *memory_region;
	u64 emi_size, gps_base, gps_size;
	int consys, gps, len, md_reserved, reserved, ret;

	consys = fdt_node_offset_by_compatible(fdt, -1,
					       "mediatek,mt6878-6631-consys");
	if (consys < 0) {
		printf("Tetris: conninfra node missing: %s\n",
		       fdt_strerror(consys));
		return consys;
	}

	memory_region = fdt_getprop(fdt, consys, "memory-region", &len);
	if (!memory_region || len != sizeof(*memory_region)) {
		printf("Tetris: invalid conninfra memory-region\n");
		return -FDT_ERR_BADPHANDLE;
	}

	reserved = fdt_node_offset_by_phandle(fdt,
					      fdt32_to_cpu(*memory_region));
	if (reserved < 0) {
		printf("Tetris: conninfra reserved memory missing: %s\n",
		       fdt_strerror(reserved));
		return reserved;
	}

	base = fdtdec_get_addr_size_auto_noparent(fdt, reserved, "reg", 0,
						  &reserved_size, true);
	if (base == FDT_ADDR_T_NONE) {
		printf("Tetris: invalid conninfra reserved memory reg\n");
		return -FDT_ERR_BADVALUE;
	}

	emi_size = fdtdec_get_uint(fdt, consys, "emi-size", 0);
	gps = fdt_node_offset_by_compatible(fdt, -1, "mediatek,mt6878-gps");
	if (gps < 0) {
		printf("Tetris: GPS node missing: %s\n", fdt_strerror(gps));
		return gps;
	}

	gps_size = fdtdec_get_uint(fdt, gps, "emi-size", 0);
	gps_base = base + emi_size;
	if (base != TETRIS_CONNSYS_EMI_BASE ||
	    emi_size != TETRIS_CONNSYS_EMI_SIZE ||
	    gps_base != TETRIS_GPS_EMI_BASE ||
	    gps_size != TETRIS_GPS_EMI_SIZE ||
	    reserved_size < emi_size + gps_size) {
		printf("Tetris: unsafe EMI layout conn=%llx/%llx gps=%llx/%llx reserved=%llx\n",
		       (unsigned long long)base,
		       (unsigned long long)emi_size,
		       (unsigned long long)gps_base,
		       (unsigned long long)gps_size,
		       (unsigned long long)reserved_size);
		return -FDT_ERR_BADVALUE;
	}

	md_reserved = fdt_node_offset_by_compatible(fdt, -1,
						    "mediatek,ap_md_c_smem");
	if (md_reserved < 0) {
		printf("Tetris: AP/MD cache shared memory missing: %s\n",
		       fdt_strerror(md_reserved));
		return md_reserved;
	}

	md_base = fdtdec_get_addr_size_auto_noparent(fdt, md_reserved, "reg", 0,
						     &md_reserved_size, true);
	if (md_base != TETRIS_MD_CACHE_EMI_BASE ||
	    md_reserved_size != TETRIS_MD_CACHE_EMI_SIZE) {
		printf("Tetris: unsafe AP/MD cache layout %llx/%llx\n",
		       (unsigned long long)md_base,
		       (unsigned long long)md_reserved_size);
		return -FDT_ERR_BADVALUE;
	}

	ret = tetris_set_connsys_emimpu_regions();
	if (ret)
		return ret;

	ret = tetris_set_connsys_emi(MTK_SIP_CONNSYS_EMI_MAIN, base,
				     emi_size, "conninfra");
	if (ret)
		return ret;

	ret = tetris_set_connsys_emi(MTK_SIP_CONNSYS_EMI_MD, md_base,
				     TETRIS_MD_CONNSYS_EMI_SIZE,
				     "modem conninfra");
	if (ret)
		return ret;

	return tetris_set_connsys_emi(MTK_SIP_CONNSYS_EMI_GPS, gps_base,
				      gps_size, "GPS");
}

void board_prep_linux(struct bootm_headers *images)
{
	int ret;

	ret = tetris_prepare_connsys_emi((const void *)images->ft_addr);
	if (ret)
		panic("Tetris: refusing Linux boot without conninfra EMI mapping\n");
}

void fastboot_oem_board(char *cmd_parameter, void *data, u32 size, char *response)
{
	if (!cmd_parameter) {
		fastboot_fail("missing oem_board command", response);
		return;
	}

	if (!strcmp(cmd_parameter, "poweroff")) {
		fastboot_okay("goodbye :(", response);
		psci_system_off();
	} else if (!strcmp(cmd_parameter, "boot_pmos")) {
		fastboot_okay("booting postmarketOS", response);
		run_command("run boot_pmos", 0);
	} else if (!strcmp(cmd_parameter, "boot_pmos_safe")) {
		fastboot_okay("booting postmarketOS with radio modules disabled",
			      response);
		run_command("run boot_pmos_safe", 0);
	} else {
		fastboot_fail("unknown oem_board command", response);
	}
}
