#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Source-order regression guard; device validation is still required."""
from pathlib import Path

source = (Path(__file__).resolve().parents[2] / "boot/bootm.c").read_text()
start = source.index("int bootm_run_states(")
body = source[start:]
reserve = "boot_fdt_add_mem_rsv_regions(images->ft_addr);"
assert body.count(reserve) == 1, "reserve DT memory exactly once in combined bootm"
reserve_at = body.index(reserve)
ramdisk_at = body.index("ret = boot_ramdisk_high(")
fdt_at = body.index("ret = boot_relocate_fdt(")
assert reserve_at < ramdisk_at < fdt_at, "initrd must not occupy firmware carveouts"
assert "states & (BOOTM_STATE_RAMDISK | BOOTM_STATE_FDT)" in body[:reserve_at]
assert "!ret && images->ft_addr" in body[:reserve_at], "DT-less boots must skip reservation"
assert "CONFIG_IS_ENABLED(OF_LIBFDT) && CONFIG_IS_ENABLED(LMB)" in body[:reserve_at]
print("bootm reservation order: PASS (source guard, not hardware validation)")

root = Path(__file__).resolve().parents[2]
arm = (root / "arch/arm/lib/bootm.c").read_text()
prep = arm[arm.index("static void boot_prep_linux("):]
assert prep.index("image_setup_linux(images)") < prep.index("board_prep_linux(images)")
board = (root / "board/mediatek/mt6878/mt6878_tetris.c").read_text()
prep = board[board.index("void board_prep_linux("):board.index("static int tetris_boot_pmos(")]
reserve_at = prep.index("tetris_modem_reserve_diagnostic(fdt)")
assert prep.index("tetris_scp_prepare_diagnostic(images, fdt)") < reserve_at
assert prep.index("tetris_prepare_connsys_emi(fdt)") < reserve_at
assert "IS_ENABLED(CONFIG_TETRIS_MODEM_RESERVE_DIAGNOSTIC)" in prep[:reserve_at]
print("modem RAM diagnostic order: PASS (after Linux and firmware placement)")
