// SPDX-License-Identifier: GPL-2.0+
/* Exercise the actual late caller with the existing real-libfdt clone fixture. */
#define main late_fdt_main
#include "tetris_linux_fdt_prepare.c"
#undef main

typedef uint8_t u8;
struct blk_desc { unsigned long blksz, lba; };
struct disk_partition { unsigned long blksz, size, start; };
struct tetris_scp_crypto { int unused; };
struct tetris_scp_security_ops { int unused; };
struct tetris_gpueb_flat { int unused; };
static struct tetris_gpueb_flat snapshot;
static int publications, discards, publish_error, discard_error;
#define ARCH_DMA_MINALIGN 64
#define TETRIS_GPUEB_MAX_CONTAINER 0x200000U
#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
static int part_get_info_by_name(struct blk_desc *dev, const char *name,
				struct disk_partition *part)
{
	(void)dev; (void)name; (void)part;
	return -ENOSYS;
}
static void *memalign(size_t alignment, size_t size)
{
	(void)alignment;
	return malloc(size);
}
static unsigned long blk_dread(struct blk_desc *dev, unsigned long start,
			      unsigned long count, void *data)
{
	(void)dev; (void)start; (void)count; (void)data;
	return 0;
}
static int tetris_gpueb_flat_retain(struct tetris_scp_crypto *crypto,
	const struct tetris_scp_security_ops *security, const void *container,
	size_t bytes, const u8 root_pin[32], struct tetris_gpueb_flat **result)
{
	(void)crypto; (void)security; (void)container; (void)bytes;
	(void)root_pin; (void)result;
	return -ENOSYS;
}
static int tetris_gpueb_flat_discard(struct tetris_gpueb_flat *image)
{
	assert(image == &snapshot);
	discards++;
	return discard_error;
}
static int tetris_gpueb_flat_publish(struct tetris_gpueb_flat **image,
				    void *fdt, size_t capacity)
{
	int chosen;
	assert(*image == &snapshot && fdt == allocation && capacity == allocated_size);
	assert(!fdt_check_full(fdt, capacity));
	assert(fdt_num_mem_rsv(fdt) == 2);
	publications++;
	if (publish_error)
		return publish_error;
	chosen = fdt_path_offset(fdt, "/chosen");
	assert(chosen >= 0);
	assert(!fdt_setprop_string(fdt, chosen, "fixture-only", "not-hardware-proof"));
	*image = NULL;
	return 0;
}
#define TETRIS_GPUEB_FINAL_HOST_TEST 1
#include "../../board/mediatek/mt6878/tetris_gpueb_flat_hook.c"

int main(void)
{
	char original[4096], copy[4096];
	struct bootm_headers images;
	unsigned long size;
	int i;
	assert(!late_fdt_main());
	assert(!fdt_create_empty_tree(original, sizeof(original)));
	assert(fdt_add_subnode(original, 0, "chosen") >= 0);
	assert(!fdt_add_mem_rsv(original, 0x9d000000, 0x9c0000));
	assert(!fdt_pack(original));
	size = fdt_totalsize(original);
	memcpy(copy, original, size);
	for (i = 0; i <= 9; i++) {
		fault = i <= 7 ? i : 0;
		publish_error = i >= 8 ? -EIO : 0;
		discard_error = i == 9 ? -EBUSY : 0;
		frees = unmaps = flushes = maps = publications = discards = 0;
		allocation = NULL;
		regions[0] = (struct lmb_region){ (uintptr_t)original, size, 0 };
		memory = (struct lmb){ { regions, 1 }, { regions, 1 } };
		images = (struct bootm_headers){ original, sizeof(original) };
		pending = &snapshot; attempted = true; capture_error = 0;
		assert((tetris_gpueb_flat_publish_final(&images) == 0) == (i == 0));
		assert(!memcmp(original, copy, size));
		if (i == 0) {
			assert(images.ft_addr == allocation && images.ft_len == allocated_size);
			assert(publications == 1 && !discards && !pending);
			assert(!frees && !unmaps && flushes == 2);
			assert(!fdt_check_full(images.ft_addr, images.ft_len));
		} else {
			assert(images.ft_addr == original && images.ft_len == sizeof(original));
			assert(publications == (i >= 8) && discards == 1);
			assert((pending != NULL) == (i == 9));
			assert(frees == ((i >= 2 && i <= 5) || i >= 8));
			assert(maps == ((i >= 2 && i <= 5) || i >= 8));
			assert(unmaps == ((i >= 3 && i <= 5) || i >= 8));
			assert(flushes == (i >= 8));
		}
		free(allocation); allocation = NULL;
	}
	images = (struct bootm_headers){ original, sizeof(original) };
	attempted = false; capture_error = -ENODATA;
	assert(tetris_gpueb_flat_publish_final(&images) == -ENODATA);
	attempted = true; capture_error = -EKEYREJECTED;
	assert(tetris_gpueb_flat_publish_final(&images) == -EKEYREJECTED);
	capture_error = 0; pending = NULL;
	assert(tetris_gpueb_flat_publish_final(&images) == -EINVAL);
	pending = &snapshot;
	assert(tetris_gpueb_flat_publish_final(NULL) == -EINVAL);
	puts("GPUEB final DT clone, publication and first-error retention passed");
	return 0;
}
