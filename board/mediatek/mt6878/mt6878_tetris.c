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
#define MTK_SIP_CONNSYS_EMI_SUCCESS	0x100

#define TETRIS_CONNSYS_EMI_BASE		0x85e00000ULL
#define TETRIS_CONNSYS_EMI_SIZE		0x00c00000ULL

static int tetris_prepare_connsys_emi(const void *fdt)
{
	struct arm_smccc_res res = { 0 };
	fdt_size_t reserved_size;
	fdt_addr_t base;
	const fdt32_t *memory_region;
	u64 emi_size;
	int consys, len, reserved;

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
	if (base != TETRIS_CONNSYS_EMI_BASE ||
	    emi_size != TETRIS_CONNSYS_EMI_SIZE ||
	    reserved_size < emi_size) {
		printf("Tetris: unsafe conninfra EMI base=%llx size=%llx reserved=%llx\n",
		       (unsigned long long)base,
		       (unsigned long long)emi_size,
		       (unsigned long long)reserved_size);
		return -FDT_ERR_BADVALUE;
	}

	arm_smccc_smc(MTK_SIP_CONNSYS_EMI_SET,
		      MTK_SIP_CONNSYS_EMI_MAIN, base, emi_size,
		      0, 0, 0, 0, &res);
	if (res.a0 != MTK_SIP_CONNSYS_EMI_SUCCESS) {
		printf("Tetris: conninfra EMI SMC failed: %lx\n", res.a0);
		return -EIO;
	}

	printf("Tetris: conninfra EMI mapped at %llx, size %llx\n",
	       (unsigned long long)base, (unsigned long long)emi_size);

	return 0;
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
		run_command("scsi scan", 0);
		run_command("ext4load scsi 2:51 0x49000000 /boot_image.itb", 0);
		run_command("bootm 0x49000000", 0);
	} else {
		fastboot_fail("unknown oem_board command", response);
	}
}
