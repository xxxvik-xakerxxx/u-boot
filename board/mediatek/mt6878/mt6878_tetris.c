// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#include <config.h>
#include <command.h>
#include <env.h>
#include <fastboot.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/system.h>
#include <string.h>

DECLARE_GLOBAL_DATA_PTR;

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
