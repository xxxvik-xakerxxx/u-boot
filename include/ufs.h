/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _UFS_H
#define _UFS_H

struct udevice;

/**
 * ufs_probe() - initialize all devices in the UFS uclass
 *
 * Return: 0 if Ok, -ve on error
 */
int ufs_probe(void);

/**
 * ufs_probe_dev() - initialize a particular device in the UFS uclass
 *
 * @index: index in the uclass sequence
 *
 * Return: 0 if successfully probed, -ve on error
 */
int ufs_probe_dev(int index);
/* Read actual selected boot attribute and this unit's boot ID. No writes. */
int ufs_read_lun_boot_identity(struct udevice *scsi, unsigned int lun,
		unsigned int *enabled_boot, unsigned int *unit_boot);


#endif
