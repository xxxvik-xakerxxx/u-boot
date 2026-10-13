// SPDX-License-Identifier: GPL-2.0+
#ifndef TETRIS_GPUEB_FINAL_HOST_TEST
#include <blk.h>
#include <bootm.h>
#include <cpu_func.h>
#include <lmb.h>
#include <malloc.h>
#include <mapmem.h>
#include <part.h>
#include <asm/cache.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include "tetris_gpueb_layout.h"
#include "tetris_gpueb_flat_hook.h"
#include "tetris_gpueb_flat_publish.h"
#include "tetris_linux_fdt_prepare.h"
#endif

static struct tetris_gpueb_flat *pending;
static bool attempted;
static int capture_error = -ENODATA;

int tetris_gpueb_flat_capture_slot_a(struct blk_desc *dev,
	struct tetris_scp_crypto *crypto,
	const struct tetris_scp_security_ops *security, const u8 root_pin[32])
{
	struct disk_partition part;
	u8 *container = NULL;
	size_t bytes = 0;
	int ret;

	if (attempted)
		return -EALREADY;
	attempted = true;
	if (!dev || !crypto || !security || !root_pin) {
		ret = -EINVAL;
		goto out;
	}
	/* Same direct GPT/block loader and bounds as production diagnostic. */
	ret = part_get_info_by_name(dev, "gpueb_a", &part);
	if (ret < 0)
		goto out;
	if (!part.blksz || part.blksz != dev->blksz || !part.size ||
	    part.size > TETRIS_GPUEB_MAX_CONTAINER / part.blksz ||
	    part.start > dev->lba || part.size > dev->lba - part.start) {
		ret = -ERANGE;
		goto out;
	}
	bytes = part.size * part.blksz;
	container = memalign(ARCH_DMA_MINALIGN, ALIGN(bytes, ARCH_DMA_MINALIGN));
	if (!container) {
		ret = -ENOMEM;
		goto out;
	}
	if (blk_dread(dev, part.start, part.size, container) != part.size) {
		ret = -EIO;
		goto out;
	}
	ret = tetris_gpueb_flat_retain(crypto, security, container, bytes, root_pin, &pending);
out:
	if (container) {
		volatile u8 *p = container;
		size_t n = bytes;
		while (n--)
			*p++ = 0;
		free(container);
	}
	capture_error = ret;
	return ret;
}

int tetris_gpueb_flat_publish_final(struct bootm_headers *images)
{
	struct bootm_headers candidate;
	phys_addr_t address;
	void *final;
	size_t capacity;
	int ret;

	if (!attempted || capture_error)
		return capture_error;
	if (!pending || !images || !images->ft_addr || !images->ft_len)
		return -EINVAL;
	/* Keep the caller's DT unchanged until payload publication succeeds.
	 * The common clone proves DRAM and LMB bounds before mapping, including
	 * allocator success with an unchanged address or missing reservation.
	 */
	candidate = *images;
	ret = tetris_linux_fdt_prepare(&candidate);
	if (ret)
		goto discard;
	final = candidate.ft_addr;
	capacity = candidate.ft_len;
	address = map_to_sysmem(final);
	ret = tetris_gpueb_flat_publish(&pending, final, capacity);
	if (ret)
		goto release;
	/* New final DT contains its own header reservation and no-map payload.
	 * Keep old DT allocation too; do not risk freeing a shared boot buffer.
	 */
	flush_dcache_range((unsigned long)final, (unsigned long)final + capacity);
	*images = candidate;
	/* bootm retains this mapping through Linux handoff; old is borrowed. */
	return 0;
release:
	unmap_sysmem(final);
	lmb_free(address, capacity, LMB_NOOVERWRITE | LMB_NONOTIFY);
discard:
	if (pending && !tetris_gpueb_flat_discard(pending))
		pending = NULL;
	return ret;
}
