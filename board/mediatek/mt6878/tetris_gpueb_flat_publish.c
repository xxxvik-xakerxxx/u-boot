// SPDX-License-Identifier: GPL-2.0+
#include <malloc.h>
#include <mapmem.h>
#include <stdio.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/libfdt.h>
#include "tetris_gpueb_flat_publish.h"

/* Kept through boot: no second publication or post-transfer discard API. */
static struct tetris_gpueb_flat *published_owner;

static int intersects(u64 a, u64 n, u64 b, u64 m)
{
	if (!n || !m || n > ~0ULL - a || m > ~0ULL - b)
		return 1;
	return a < b + m && b < a + n;
}

static int reservations(const void *fdt, int parent, u64 base, u64 size)
{
	const fdt32_t *cells;
	int child, length, i;
	u64 a, n;

	fdt_for_each_subnode(child, fdt, parent) {
		cells = fdt_getprop(fdt, child, "reg", &length);
		if (!cells) {
			if (length != -FDT_ERR_NOTFOUND)
				return -EINVAL;
			continue;
		}
		if (length <= 0 || length % 16)
			return -EINVAL;
		for (i = 0; i < length / 4; i += 4) {
			a = ((u64)fdt32_to_cpu(cells[i]) << 32) | fdt32_to_cpu(cells[i+1]);
			n = ((u64)fdt32_to_cpu(cells[i+2]) << 32) | fdt32_to_cpu(cells[i+3]);
			if (intersects(base, size, a, n))
				return -EEXIST;
		}
	}
	return 0;
}

static int header_reservations(const void *fdt, u64 base, u64 size)
{
	u64 address, bytes;
	int count = fdt_num_mem_rsv(fdt), i, ret;

	if (count < 0)
		return count;
	for (i = 0; i < count; i++) {
		ret = fdt_get_mem_rsv(fdt, i, &address, &bytes);
		if (ret)
			return ret;
		if (intersects(base, size, address, bytes))
			return -EEXIST;
	}
	return 0;
}

static int build(void *fdt, const struct tetris_gpueb_flat_info *info)
{
	fdt32_t reg[4];
	u32 phandle;
	char name[64];
	int parent, node, consumer, ret;

	if (fdt_address_cells(fdt, 0) != 2 || fdt_size_cells(fdt, 0) != 2)
		return -EINVAL;
	ret = header_reservations(fdt, info->reserved_base, info->reserved_bytes);
	if (ret)
		return ret;
	parent = fdt_path_offset(fdt, "/reserved-memory");
	if (parent == -FDT_ERR_NOTFOUND) {
		parent = fdt_add_subnode(fdt, 0, "reserved-memory");
		if (parent < 0)
			return parent;
		ret = fdt_setprop_u32(fdt, parent, "#address-cells", 2);
		if (ret)
			return ret;
		ret = fdt_setprop_u32(fdt, parent, "#size-cells", 2);
		if (ret)
			return ret;
		ret = fdt_setprop(fdt, parent, "ranges", NULL, 0);
		if (ret)
			return ret;
	} else if (parent < 0) {
		return parent;
	}
	if (fdt_address_cells(fdt, parent) != 2 || fdt_size_cells(fdt, parent) != 2)
		return -EINVAL;
	{
		int len;
		if (!fdt_getprop(fdt, parent, "ranges", &len) || len != 0)
			return -EINVAL;
	}
	ret = reservations(fdt, parent, info->reserved_base, info->reserved_bytes);
	if (ret)
		return ret;
	if (fdt_path_offset(fdt, "/gpueb-flat-analysis") != -FDT_ERR_NOTFOUND)
		return -EEXIST;
	ret = fdt_generate_phandle(fdt, &phandle);
	if (ret)
		return ret;
	ret = snprintf(name, sizeof(name), "gpueb-authenticated@%llx",
		       (unsigned long long)info->reserved_base);
	if (ret < 0 || (size_t)ret >= sizeof(name))
		return -EINVAL;
	node = fdt_add_subnode(fdt, parent, name);
	if (node < 0)
		return node;
	reg[0] = cpu_to_fdt32(info->reserved_base >> 32);
	reg[1] = cpu_to_fdt32(info->reserved_base);
	reg[2] = 0;
	reg[3] = cpu_to_fdt32(info->reserved_bytes);
#define SET(call) do { ret = (call); if (ret) return ret; } while (0)
	SET(fdt_setprop(fdt, node, "reg", reg, sizeof(reg)));
	SET(fdt_setprop(fdt, node, "no-map", NULL, 0));
	SET(fdt_setprop_u32(fdt, node, "phandle", phandle));
	consumer = fdt_add_subnode(fdt, 0, "gpueb-flat-analysis");
	if (consumer < 0)
		return consumer;
	SET(fdt_setprop_string(fdt, consumer, "compatible", "nothing,tetris-gpueb-flat-analysis-v1"));
	SET(fdt_setprop_u32(fdt, consumer, "memory-region", phandle));
	SET(fdt_setprop_u32(fdt, consumer, "nothing,authenticated-bytes", info->authenticated_bytes));
	SET(fdt_setprop(fdt, consumer, "nothing,plaintext-sha256", info->plaintext_sha256, 32));
#undef SET
	return fdt_check_full(fdt, fdt_totalsize(fdt));
}

int tetris_gpueb_flat_publish(struct tetris_gpueb_flat **image,
	void *fdt, size_t capacity)
{
	struct tetris_gpueb_flat_info info;
	void *copy = NULL;
	int ret, released;

	if (!image || !*image)
		return -EINVAL;
	if (published_owner)
		return -EALREADY;
	ret = tetris_gpueb_flat_describe(*image, &info);
	if (ret)
		return ret;
	if (!fdt || capacity < sizeof(struct fdt_header) || capacity > 0x200000 ||
	    intersects(info.reserved_base, info.reserved_bytes, map_to_sysmem(fdt), capacity)) {
		ret = -EINVAL;
		goto abort;
	}
	ret = fdt_check_header(fdt);
	if (ret)
		goto abort;
	if ((size_t)fdt_totalsize(fdt) > capacity) {
		ret = -EINVAL;
		goto abort;
	}
	ret = fdt_check_full(fdt, capacity);
	if (ret)
		goto abort;
	copy = malloc(capacity);
	if (!copy) {
		ret = -ENOMEM;
		goto abort;
	}
	memset(copy, 0, capacity);
	ret = fdt_open_into(fdt, copy, capacity);
	if (!ret)
		ret = build(copy, &info);
	if (ret)
		goto abort;
	/* No fallible operation after this commit; original DT was untouched. */
	memcpy(fdt, copy, capacity);
	published_owner = *image;
	*image = NULL;
	free(copy);
	return 0;
abort:
	free(copy);
	released = tetris_gpueb_flat_discard(*image);
	if (!released)
		*image = NULL;
	return ret; /* First publication failure preserved even if release fails. */
}
