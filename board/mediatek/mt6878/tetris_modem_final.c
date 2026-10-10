// SPDX-License-Identifier: GPL-2.0+
#include <bootm.h>
#include <cpu_func.h>
#include <lmb.h>
#include <mapmem.h>
#include <stdio.h>
#include <asm/cache.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include "tetris_modem_ccci_tags.h"
#include "tetris_modem_final.h"

#define TAG_CAPACITY 65536UL
#define DT_MAX 0x200000UL
static unsigned int attempted;
static int first_error;

static void put32(unsigned char *p, unsigned int value)
{
	unsigned int i;
	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)(value >> (8 * i));
}

static int consumer(const void *fdt)
{
	const void *property;
	int node, second, length;

	node = fdt_node_offset_by_compatible(fdt, -1, "mediatek,mddriver");
	if (node < 0)
		return node;
	second = fdt_node_offset_by_compatible(fdt, node, "mediatek,mddriver");
	if (second != -FDT_ERR_NOTFOUND)
		return second < 0 ? second : -EEXIST;
	property = fdt_getprop(fdt, node, "ccci,modem_info_v2", &length);
	if (property || length != -FDT_ERR_NOTFOUND)
		return property ? -EEXIST : length;
	property = fdt_getprop(fdt, node, "ccci,modem_info", &length);
	if (property || length != -FDT_ERR_NOTFOUND)
		return property ? -EEXIST : length;
	return node;
}

static int no_map_reservation(void *fdt, unsigned long long address)
{
	fdt64_t reg[2] = { cpu_to_fdt64(address), cpu_to_fdt64(TAG_CAPACITY) };
	int parent, node, ret, length;

	parent = fdt_path_offset(fdt, "/reserved-memory");
	if (parent < 0)
		return parent;
	if (fdt_address_cells(fdt, parent) != 2 || fdt_size_cells(fdt, parent) != 2)
		return -EPROTONOSUPPORT;
	if (!fdt_getprop(fdt, parent, "ranges", &length) || length)
		return -EPROTONOSUPPORT;
	/* This allocation is newly LMB-owned; never adopt a named inherited node. */
	node = fdt_subnode_offset(fdt, parent, "tetris-modem-linux-tags");
	if (node != -FDT_ERR_NOTFOUND)
		return node < 0 ? node : -EEXIST;
	node = fdt_add_subnode(fdt, parent, "tetris-modem-linux-tags");
	if (node < 0)
		return node;
	ret = fdt_setprop(fdt, node, "reg", reg, sizeof(reg));
	if (!ret)
		ret = fdt_setprop(fdt, node, "no-map", NULL, 0);
	if (!ret)
		ret = fdt_add_mem_rsv(fdt, address, TAG_CAPACITY);
	return ret;
}

int tetris_modem_publish_final(struct bootm_headers *images)
{
	phys_addr_t tag_address = 0x80000000ULL, dt_address = 0x80000000ULL;
	void *tags = NULL, *final = NULL;
	unsigned char descriptor[32] = { 0 };
	size_t capacity = 0;
	int node, ret, bytes, cleanup_ret;
	unsigned int have_tags = 0, have_dt = 0;

	if (attempted)
		return first_error ? first_error : -EALREADY;
	attempted = 1;
	if (!images || !images->ft_addr || !images->ft_len) {
		ret = -EINVAL;
		goto fail;
	}
	ret = fdt_check_full(images->ft_addr, images->ft_len);
	if (ret)
		goto fail;
	ret = consumer(images->ft_addr);
	if (ret < 0)
		goto fail;
	if ((size_t)fdt_totalsize(images->ft_addr) > DT_MAX - 4096) {
		ret = -E2BIG;
		goto fail;
	}
	capacity = ((size_t)fdt_totalsize(images->ft_addr) + 4096 + 4095) & ~4095UL;
	/* Kernel maps MAX_LK_INFO_SIZE=64KiB, even when descriptor size is smaller.
	 * Reserve/zero/clean the ENTIRE buffer, not merely the encoded payload.
	 */
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 4096, &tag_address, TAG_CAPACITY, LMB_NOOVERWRITE);
	if (ret)
		goto fail;
	have_tags = 1;
	tags = map_sysmem(tag_address, TAG_CAPACITY);
	if (!tags) {
		ret = -ENOMEM;
		goto fail;
	}
	memset(tags, 0, TAG_CAPACITY);
	bytes = tetris_modem_loaded_encode_tags(tags, TAG_CAPACITY);
	if (bytes <= 0 || bytes > (int)TAG_CAPACITY) {
		ret = bytes < 0 ? bytes : -EPROTO;
		goto fail;
	}
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 4096, &dt_address, capacity, LMB_NOOVERWRITE);
	if (ret)
		goto fail;
	have_dt = 1;
	final = map_sysmem(dt_address, capacity);
	if (!final) {
		ret = -ENOMEM;
		goto fail;
	}
	memset(final, 0, capacity);
	ret = fdt_open_into(images->ft_addr, final, capacity);
	if (!ret)
		ret = fdt_add_mem_rsv(final, dt_address, capacity);
	if (!ret)
		ret = no_map_reservation(final, tag_address);
	if (ret)
		goto fail;
	/* Node offsets can change after reserved-memory edits; reacquire. */
	node = consumer(final);
	if (node < 0) {
		ret = node;
		goto fail;
	}
	/* Exact _ccci_lk_info_v2 little-endian ABI, not BE FDT cells. */
	put32(descriptor, (unsigned int)tag_address);
	put32(descriptor + 4, (unsigned int)((unsigned long long)tag_address >> 32));
	put32(descriptor + 8, (unsigned int)bytes);
	put32(descriptor + 16, 2);
	put32(descriptor + 20, TETRIS_MODEM_LINUX_TAG_COUNT);
	/* err_no/ld_flag/ld_md_errno stay zero; kernel derives MD enable from
	 * actual successful hdr_tbl_inf. No guessed modem-ready flag here.
	 */
	ret = fdt_setprop(final, node, "ccci,modem_info_v2", descriptor, sizeof(descriptor));
	if (!ret)
		ret = fdt_check_full(final, capacity);
	if (ret)
		goto fail;
	/* Cache helpers complete architecture-required barriers, same convention
	 * as existing GPU final-DT owner. Commit borrowed images ONLY afterwards.
	 */
	flush_dcache_range((unsigned long)tags, (unsigned long)tags + TAG_CAPACITY);
	flush_dcache_range((unsigned long)final, (unsigned long)final + capacity);
	images->ft_addr = final;
	images->ft_len = fdt_totalsize(final);
	/* Retain new allocations/mappings and original DT until Linux handoff. */
	return 0;
fail:
	if (!first_error)
		first_error = ret < 0 ? ret : -EPROTO;
	if (final)
		unmap_sysmem(final);
	if (tags)
		unmap_sysmem(tags);
	/* Only AP-only unpublished allocations are releasable. Firmware/service
	 * allocations belong to the running MD and are NEVER freed here.
	 */
	if (have_dt) {
		cleanup_ret = lmb_free(dt_address, capacity, LMB_NOOVERWRITE | LMB_NONOTIFY);
		if (cleanup_ret)
			printf("Tetris MD unpublished DT release error=%d; first=%d\n", cleanup_ret, first_error);
	}
	if (have_tags) {
		cleanup_ret = lmb_free(tag_address, TAG_CAPACITY, LMB_NOOVERWRITE | LMB_NONOTIFY);
		if (cleanup_ret)
			printf("Tetris MD unpublished tags release error=%d; first=%d\n", cleanup_ret, first_error);
	}
	return first_error;
}
