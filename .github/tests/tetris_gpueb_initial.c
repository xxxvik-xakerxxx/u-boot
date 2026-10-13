// SPDX-License-Identifier: GPL-2.0+
/* CI-only synthetic fault instruments, not MFG0/secure-access hardware proof.
 * Link the bundled libfdt objects from run_tetris_linux_fdt_prepare.sh and use
 * -I<scratch> after its guarded extraction of tetris_gpueb_initial.inc.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

typedef uint32_t u32;
typedef uint64_t phys_addr_t;
typedef uint64_t phys_size_t;
struct lmb_region { phys_addr_t base; phys_size_t size; unsigned int flags; };
struct alist { struct lmb_region *entries; unsigned int count; };
struct lmb { struct alist used_mem, available_mem; };
struct bootm_headers { void *ft_addr; unsigned long ft_len; };

enum fault_mode {
	NO_FAULT, ALLOCATION_FAILURE, SMALL_VALID_CLONE, SMALL_CLONE_BAD_FALLBACK
};
static struct lmb_region regions[2];
static struct lmb memory;
static struct bootm_headers *active_images;
static void *original_address, *allocation;
static size_t original_size, allocated_size;
static enum fault_mode fault;
static int maps, unmaps, frees, flushes, reads, deletes, writes, versions;
static u32 input_word, captured_word;
static const char *last_write;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define alist_for_each(p, list) \
	for ((p) = (list)->entries; (p) < (list)->entries + (list)->count; (p)++)
#define LMB_MEM_ALLOC_MAX 1
#define LMB_NOOVERWRITE 2
#define LMB_NONOTIFY 4
#define LMB_NOMAP 8

static struct lmb *lmb_get(void) { return &memory; }
static phys_addr_t map_to_sysmem(const void *p)
{
	return p && p == allocation ? 0x7f000000ULL : (uintptr_t)p;
}

static int lmb_alloc_mem(int mode, int align, phys_addr_t *address,
			 size_t size, int flags)
{
	assert(mode == LMB_MEM_ALLOC_MAX && align == 4096 && flags == LMB_NOOVERWRITE);
	assert(*address == 0x80000000ULL && !(size % 4096) && !allocation);
	if (fault == ALLOCATION_FAILURE)
		return -ENOMEM;
	allocation = calloc(1, size);
	assert(allocation);
	allocated_size = size;
	*address = 0x7f000000ULL;
	regions[1] = (struct lmb_region){ *address, size, LMB_NOOVERWRITE };
	memory.used_mem.count = 2;
	memory.available_mem.count = 2;
	return 0;
}

static void *map_sysmem(phys_addr_t address, size_t size)
{
	assert(address == 0x7f000000ULL && size == allocated_size && allocation);
	maps++;
	return allocation;
}

static void unmap_sysmem(void *p)
{
	assert(p == allocation && allocation);
	unmaps++;
}

static int lmb_free(phys_addr_t address, size_t size, int flags)
{
	assert(address == 0x7f000000ULL && size == allocated_size);
	assert(flags == (LMB_NOOVERWRITE | LMB_NONOTIFY) && allocation);
	assert(active_images->ft_addr == original_address);
	free(allocation);
	allocation = NULL;
	memory.used_mem.count = 1;
	frees++;
	if (fault == SMALL_CLONE_BAD_FALLBACK)
		fdt_set_magic(original_address, 0);
	return 0;
}

static void flush_dcache_range(unsigned long start, unsigned long end)
{
	assert(end >= start);
	if (allocation && start == (uintptr_t)allocation) {
		assert(end - start == allocated_size);
		assert(active_images->ft_addr == original_address);
	} else {
		assert(start == (uintptr_t)original_address && end - start <= original_size);
	}
	flushes++;
}

static int instrument_open_into(const void *source, void *target, int size)
{
	void *packed;
	int ret, small;

	if (fault != SMALL_VALID_CLONE && fault != SMALL_CLONE_BAD_FALLBACK)
		return fdt_open_into(source, target, size);
	/* Real libfdt creates a valid tree with room ONLY for the clone's new
	 * reservation. Report writes then fail with real -FDT_ERR_NOSPACE.
	 */
	packed = malloc(fdt_totalsize(source));
	assert(packed);
	ret = fdt_open_into(source, packed, fdt_totalsize(source));
	assert(!ret && !fdt_pack(packed));
	small = fdt_totalsize(packed) + 2 * sizeof(uint64_t);
	assert(small < size);
	ret = fdt_open_into(packed, target, small);
	free(packed);
	return ret;
}

/* Compile the actual bounded clone first: its static declarations/API must
 * precede the extracted publication helper, exactly as in the native board.
 */
#define fdt_open_into instrument_open_into
#define TETRIS_BROM_BOARD_HOST_TEST 1
#include "../../board/mediatek/mt6878/tetris_linux_fdt_prepare.h"
#undef fdt_open_into

static u32 instrument_readl(const volatile void *address)
{
	assert((uintptr_t)address == 0x1c001eb4UL);
	reads++;
	captured_word = input_word;
	return input_word;
}

static int instrument_delprop(void *fdt, int node, const char *name)
{
	if (!(deletes % 7))
		assert(!strcmp(name, "nothing,gpueb-initial-report-version"));
	deletes++;
	return fdt_delprop(fdt, node, name);
}

static int instrument_setprop_u32(void *fdt, int node, const char *name, u32 value)
{
	int ret;

	ret = fdt_setprop_u32(fdt, node, name, value);
	if (!ret) {
		writes++;
		last_write = name;
		if (!strcmp(name, "nothing,gpueb-initial-report-version"))
			versions++;
	}
	return ret;
}

#define CONFIG_TETRIS_GPUEB_FLAT_RETENTION_DIAGNOSTIC 1
#define CONFIG_TETRIS_GPUEB_TRANSFORM_DIAGNOSTIC 0
#define CONFIG_IS_ENABLED(option) CONFIG_##option
#define readl instrument_readl
#define fdt_delprop instrument_delprop
#define fdt_setprop_u32 instrument_setprop_u32
#include "tetris_gpueb_initial.inc"
#undef readl
#undef fdt_delprop
#undef fdt_setprop_u32

static u32 property(const void *fdt, size_t index)
{
	const fdt32_t *value;
	int len, node = fdt_path_offset(fdt, "/chosen");

	assert(node >= 0 && index < ARRAY_SIZE(gpueb_initial_properties));
	value = fdt_getprop(fdt, node, gpueb_initial_properties[index], &len);
	assert(value && len == (int)sizeof(*value));
	return fdt32_to_cpu(*value);
}

static void assert_no_raw(const void *fdt)
{
	size_t i;
	int len, node = fdt_path_offset(fdt, "/chosen");

	assert(node >= 0);
	for (i = 3; i < ARRAY_SIZE(gpueb_initial_properties); i++) {
		assert(!fdt_getprop(fdt, node, gpueb_initial_properties[i], &len));
		assert(len == -FDT_ERR_NOTFOUND);
	}
}

static void assert_report(const void *fdt, u32 stage, int error)
{
	assert(!fdt_check_full(fdt, fdt_totalsize(fdt)));
	assert(property(fdt, 0) == 1 && property(fdt, 1) == stage);
	assert(property(fdt, 2) == (u32)error);
	assert(last_write && !strcmp(last_write, gpueb_initial_properties[0]));
	assert(versions == 1);
	if (stage == 1 && !error) {
		assert(property(fdt, 3) == captured_word);
		assert(property(fdt, 4) == ((captured_word >> 30) & 3U));
		assert(property(fdt, 5) == ((captured_word >> 12) & 1U));
		assert(property(fdt, 6) == ((captured_word >> 13) & 1U));
	} else {
		assert_no_raw(fdt);
	}
}

static void setup(void *original, size_t size, struct bootm_headers *images,
		  enum fault_mode mode, bool stale)
{
	size_t i;
	int chosen;

	assert(!allocation);
	memset(original, 0, size);
	assert(!fdt_create_empty_tree(original, size));
	chosen = fdt_add_subnode(original, 0, "chosen");
	assert(chosen >= 0);
	assert(!fdt_setprop_string(original, chosen, "fixture,unrelated", "keep"));
	if (stale)
		for (i = 0; i < ARRAY_SIZE(gpueb_initial_properties); i++)
			assert(!fdt_setprop_u32(original, chosen, gpueb_initial_properties[i], 0xa5a5a5a5));
	original_address = original;
	original_size = size;
	regions[0] = (struct lmb_region){ (uintptr_t)original, size, 0 };
	memory = (struct lmb){ { regions, 1 }, { regions, 1 } };
	*images = (struct bootm_headers){ original, size };
	active_images = images;
	fault = mode;
	maps = unmaps = frees = flushes = reads = deletes = writes = versions = 0;
	last_write = NULL;
	gpueb_initial_raw = 0;
	gpueb_initial_stage = 0;
	gpueb_initial_error = -ENODATA;
}

static void assert_unrelated(const void *fdt)
{
	int len, node = fdt_path_offset(fdt, "/chosen");
	const char *value = fdt_getprop(fdt, node, "fixture,unrelated", &len);

	assert(value && len == 5 && !strcmp(value, "keep"));
}

static void test_capture_and_commit(u32 word)
{
	char original[4096], before[4096];
	struct bootm_headers images;

	setup(original, sizeof(original), &images, NO_FAULT, true);
	memcpy(before, original, sizeof(before));
	input_word = word;
	gpueb_initial_power_capture();
	input_word = ~word;
	gpueb_initial_power_capture();
	assert(reads == 1 && gpueb_initial_raw == word);
	assert(gpueb_initial_stage == 1 && !gpueb_initial_error);
	assert(!gpueb_initial_report_publish(&images));
	assert(images.ft_addr == allocation && images.ft_addr != original);
	assert(images.ft_len == allocated_size && maps == 1 && !unmaps && !frees);
	assert(flushes == 2 && deletes == 7 && writes == 7);
	assert(!memcmp(original, before, sizeof(original)));
	assert_report(images.ft_addr, 1, 0);
	assert_unrelated(images.ft_addr);
	free(allocation);
	allocation = NULL;
}

static void test_no_capture(void)
{
	char original[4096];
	struct bootm_headers images;

	setup(original, sizeof(original), &images, NO_FAULT, true);
	assert(!gpueb_initial_report_publish(&images));
	assert(!reads && writes == 3 && deletes == 7);
	assert_report(images.ft_addr, 0, -ENODATA);
	free(allocation);
	allocation = NULL;
}

static void test_allocation_fallback(void)
{
	char original[4096];
	struct bootm_headers images;

	setup(original, sizeof(original), &images, ALLOCATION_FAILURE, true);
	input_word = 0xc0002000U;
	gpueb_initial_power_capture();
	assert(gpueb_initial_report_publish(&images) == -ENOMEM);
	assert(images.ft_addr == original && !allocation);
	assert(!maps && !unmaps && !frees && flushes == 1);
	assert(deletes == 7 && writes == 3);
	assert_report(images.ft_addr, 0, -ENOMEM);
	assert_unrelated(images.ft_addr);
}

static void test_small_clone_fallback(void)
{
	char original[4096];
	struct bootm_headers images;

	setup(original, sizeof(original), &images, SMALL_VALID_CLONE, false);
	input_word = 0xc0002000U;
	gpueb_initial_power_capture();
	assert(gpueb_initial_report_publish(&images) == -FDT_ERR_NOSPACE);
	assert(images.ft_addr == original && !allocation);
	assert(maps == 1 && unmaps == 1 && frees == 1 && flushes == 2);
	assert(deletes == 14 && writes == 3);
	assert_report(images.ft_addr, 0, -FDT_ERR_NOSPACE);
	assert_unrelated(images.ft_addr);
}

static void test_secondary_error_preserves_first(void)
{
	char original[4096];
	struct bootm_headers images;

	setup(original, sizeof(original), &images, SMALL_CLONE_BAD_FALLBACK, false);
	input_word = 0xc0002000U;
	gpueb_initial_power_capture();
	assert(gpueb_initial_report_publish(&images) == -FDT_ERR_NOSPACE);
	assert(images.ft_addr == original && !allocation);
	assert(maps == 1 && unmaps == 1 && frees == 1 && flushes == 1);
	assert(fdt_check_full(original, sizeof(original)) == -FDT_ERR_BADMAGIC);
	assert(deletes == 7 && !writes && !versions);
}

int main(void)
{
	const char *ci = getenv("CI"), *github = getenv("GITHUB_ACTIONS");

	if (!ci || strcmp(ci, "true") || !github || strcmp(github, "true")) {
		fputs("CI-only fixture; no hardware proof\n", stderr);
		return 2;
	}
	test_capture_and_commit(0);
	test_capture_and_commit(0xc0002000U);
	test_capture_and_commit(0x00001000U);
	test_no_capture();
	test_allocation_fallback();
	test_small_clone_fallback();
	test_secondary_error_preserves_first();
	puts("GPUEB initial report real-libfdt fault fixture passed (not hardware validation)");
	return 0;
}
