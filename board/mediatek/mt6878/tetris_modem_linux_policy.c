// SPDX-License-Identifier: GPL-2.0+
#include <blk.h>
#include <dm.h>
#include <malloc.h>
#include <part.h>
#include <ufs.h>
#include <asm/cache.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <u-boot/sha256.h>
#include "tetris_modem_loaded_boot.h"
#include "tetris_modem_linux_policy.h"
#include "tetris_scp_handoff.h"

static unsigned int attempted;
static int first_error;

static unsigned int word(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static int require_slot_a(struct blk_desc *dev)
{
	struct disk_partition part;
	enum tetris_scp_slot slot;
	unsigned char *buffer;
	unsigned int sector;
	int ret;

	if (!dev || (dev->blksz != 512 && dev->blksz != 4096))
		return -EPROTONOSUPPORT;
	ret = part_get_info_by_name(dev, "misc", &part);
	if (ret < 0)
		return ret;
	sector = 2048 / dev->blksz;
	if (part.blksz != dev->blksz || part.start >= dev->lba ||
	    part.size > dev->lba - part.start || part.size <= sector)
		return -ERANGE;
	buffer = memalign(ARCH_DMA_MINALIGN, 4096);
	if (!buffer)
		return -ENOMEM;
	ret = -EIO;
	if (blk_dread(dev, part.start + sector, 1, buffer) == 1) {
		ret = tetris_scp_decode_boot_control(buffer + 2048 % dev->blksz, 32, &slot);
		if (!ret && slot != TETRIS_SCP_SLOT_A)
			ret = -EPROTONOSUPPORT;
	}
	free(buffer);
	return ret;
}

static int selected_boot(struct blk_desc *user, struct blk_desc **out)
{
	struct udevice *device;
	struct blk_desc *selected = NULL;
	unsigned int seen = 0;
	int ret;

	if (!user || !user->bdev || !user->bdev->parent || user->uclass_id != UCLASS_SCSI)
		return -EINVAL;
	ret = blk_first_device(UCLASS_SCSI, &device);
	while (!ret) {
		struct blk_desc *candidate = blk_get_by_device(device);
		unsigned int enabled, unit;

		if (!candidate)
			return -EPROTO;
		/* Only siblings of the actual modem partition's SCSI/UFS controller. */
		if (device->parent == user->bdev->parent && candidate->target == user->target) {
			ret = ufs_read_lun_boot_identity(device->parent, candidate->lun, &enabled, &unit);
			if (ret)
				return ret;
			if (unit == enabled) {
				selected = candidate;
				seen++;
			}
		}
		ret = blk_next_device(&device);
	}
	if (ret != -ENODEV)
		return ret;
	if (seen != 1)
		return seen ? -EEXIST : -ENODEV;
	*out = selected;
	return 0;
}

static int preloader_digest(struct blk_desc *dev, unsigned char digest[32])
{
	static const unsigned char expected[32] = {
		0x5d, 0x2b, 0xed, 0xd0, 0x00, 0x49, 0xfc, 0xed,
		0x98, 0x3d, 0x3a, 0xe5, 0x39, 0x89, 0x61, 0x6c,
		0x5c, 0x9f, 0x46, 0xc4, 0xed, 0xdd, 0x32, 0xa0,
		0x1a, 0x1a, 0xd4, 0x6f, 0xf8, 0xd2, 0xc5, 0x0f,
	};
	sha256_context sha;
	unsigned char *buffer;
	unsigned int size, remaining, have;
	unsigned long long block;
	int ret = -EBADMSG;

	if (!dev || (dev->blksz != 512 && dev->blksz != 4096) ||
	    dev->lba <= 0x1000 / dev->blksz)
		return -EINVAL;
	buffer = memalign(ARCH_DMA_MINALIGN, 4096);
	if (!buffer)
		return -ENOMEM;
	if (blk_dread(dev, 0, 1, buffer) != 1) {
		ret = -EIO;
		goto out;
	}
	if (memcmp(buffer, "UFS_BOOT\0", 9))
		goto out;
	block = 0x1000 / dev->blksz;
	if (blk_dread(dev, block, 1, buffer) != 1) {
		ret = -EIO;
		goto out;
	}
	if (memcmp(buffer, "MMM\x01", 4) || word(buffer + 0x1c) != 0x02000f00U)
		goto out;
	size = word(buffer + 0x20);
	if (size < 0x38 || size > 4U * 1024 * 1024 ||
	    (size + dev->blksz - 1) / dev->blksz > dev->lba - block) {
		ret = -ERANGE;
		goto out;
	}
	/* Include GFH itself and precisely declared bytes, not trailing LUN data.
	 * Keep the first already-read block in the stream; no reread of GFH.
	 */
	sha256_starts(&sha);
	remaining = size;
	have = dev->blksz;
	for (;;) {
		unsigned int take = remaining < have ? remaining : have;
		unsigned int count;

		sha256_update(&sha, buffer, take);
		remaining -= take;
		if (!remaining)
			break;
		block += have / dev->blksz;
		count = (remaining + dev->blksz - 1) / dev->blksz;
		if (count > 4096 / dev->blksz)
			count = 4096 / dev->blksz;
		have = count * dev->blksz;
		if (blk_dread(dev, block, count, buffer) != count) {
			ret = -EIO;
			goto out;
		}
	}
	sha256_finish(&sha, digest);
	ret = memcmp(digest, expected, 32) ? -EKEYREJECTED : 0;
out:
	free(buffer);
	return ret;
}

int tetris_modem_linux_b41_once(void *fdt, struct blk_desc *user, char slot)
{
	struct blk_desc *boot = NULL;
	unsigned char measured[32];
	int ret;

	if (attempted)
		return first_error ? first_error : -EALREADY;
	attempted = 1;
	/* Existing ATF profile hashes tee_a only: no unsupported slot-B claim. */
	if (!fdt || slot != 'a') {
		first_error = -EINVAL;
		return first_error;
	}
	ret = require_slot_a(user);
	if (!ret)
		ret = selected_boot(user, &boot);
	if (!ret)
		ret = preloader_digest(boot, measured);
	if (!ret)
		/* Our explicit normal Linux policy, not a claim stock options were absent. */
		ret = tetris_modem_loaded_boot_once(fdt, user, slot, 1, "0", measured);
	if (ret)
		first_error = ret < 0 ? ret : -EPROTO;
	return first_error;
}
