/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_LINUX_POLICY_H
#define __TETRIS_MODEM_LINUX_POLICY_H
struct blk_desc;
/* Internal normal native-Linux policy: CCBgear1, PHY capture explicitly OFF.
 * Not stock-option emulation, not an auth/permission assertion or a CLI.
 * Derives pinned preloader hash from the actual selected UFS boot LUN.
 */
int tetris_modem_linux_b41_once(void *fdt, struct blk_desc *user, char slot);
#endif
