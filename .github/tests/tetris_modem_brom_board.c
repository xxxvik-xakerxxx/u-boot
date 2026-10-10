// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

typedef uint64_t phys_addr_t;
typedef uint64_t phys_size_t;
struct lmb_region { phys_addr_t base; phys_size_t size; unsigned int flags; };
struct alist { struct lmb_region *entries; unsigned int count; };
struct lmb { struct alist used_mem, available_mem; };
static struct lmb_region used, available;
static struct lmb memory;
static struct lmb *lmb_get(void) { return &memory; }
static phys_addr_t map_to_sysmem(const void *p) { return (uintptr_t)p; }
#define alist_for_each(p, list) \
	for ((p) = (list)->entries; (p) < (list)->entries + (list)->count; (p)++)
#define LMB_NOMAP 8
struct bootm_headers { char *ft_addr; unsigned long ft_len; };
struct blk_desc { int unused; };
struct disk_partition { int unused; };
struct tetris_modem_loaded_report {
	unsigned int stage;
	int error;
	struct {
		unsigned int stage, value;
		int error;
		unsigned long long address, reply[4];
		struct { unsigned int stage; int error; } cleanup;
	} hardware;
};
#define CONFIG_TETRIS_MODEM_BROM_ONLY 1
#define CONFIG_TETRIS_MODEM_LOAD_DIAGNOSTIC config_conflict
#define CONFIG_TETRIS_MODEM_RESERVE_DIAGNOSTIC 0
#define CONFIG_TETRIS_GPUEB_FLAT_RETENTION_DIAGNOSTIC 0
#define CONFIG_TETRIS_GPUEB_TRANSFORM_DIAGNOSTIC 0
#define IS_ENABLED(x) (x)
#define UCLASS_SCSI 1
#define LMB_MEM_ALLOC_MAX 1
#define LMB_NOOVERWRITE 2
#define LMB_NONOTIFY 4
static int fault, config_conflict, loader_calls, loader_error, report_error;
static int allocations, frees, unmaps, flushes, loader_publication_fault;
static void *allocation;
static struct blk_desc device;

static int blk_get_desc(int class, int index, struct blk_desc **out)
{
	assert(class == UCLASS_SCSI && index == 2);
	*out = &device;
	return fault == 1 ? -ENODEV : 0;
}
static int part_get_info_by_name(struct blk_desc *dev, const char *name,
				struct disk_partition *part)
{
	assert(dev == &device && part);
	return (fault == 2 && !strcmp(name, "modem_a")) ||
		(fault == 3 && !strcmp(name, "tee_a")) ? -ENOENT : 1;
}
static int lmb_alloc_mem(int mode, int align, phys_addr_t *address,
			size_t size, int flags)
{
	assert(mode == 1 && align == 4096 && flags == 2);
	if (fault == 4)
		return -ENOMEM;
	allocation = calloc(1, size);
	assert(allocation);
	*address = (uintptr_t)allocation;
	allocations++;
	return 0;
}
static void *map_sysmem(phys_addr_t address, size_t size)
{
	assert((void *)(uintptr_t)address == allocation && size);
	return fault == 5 ? NULL : allocation;
}
static void unmap_sysmem(void *memory) { assert(memory == allocation); unmaps++; }
static int lmb_free(phys_addr_t address, size_t size, int flags)
{
	assert((void *)(uintptr_t)address == allocation && size && flags == 6);
	free(allocation);
	allocation = NULL;
	frees++;
	return 0;
}
static void flush_dcache_range(unsigned long start, unsigned long end)
{
	assert(start && end > start);
	flushes++;
}
static int mock_open(const void *source, void *target, int size)
{
	return fault == 6 ? -FDT_ERR_BADSTRUCTURE : fdt_open_into(source, target, size);
}
static int mock_reserve(void *fdt, uint64_t address, uint64_t size)
{
	return fault == 7 ? -FDT_ERR_NOSPACE : fdt_add_mem_rsv(fdt, address, size);
}
static int mock_setprop(void *fdt, int node, const char *name, const void *data, int size)
{
	if ((fault == 8 || loader_publication_fault) &&
	    !strcmp(name, "nothing,modem-brom-report"))
		return -FDT_ERR_NOSPACE;
	return fdt_setprop(fdt, node, name, data, size);
}
static unsigned int get32(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}
static int tetris_modem_linux_b41_once(void *fdt, struct blk_desc *dev, char slot)
{
	int node = fdt_path_offset(fdt, "/chosen"), length;
	const unsigned char *record = fdt_getprop(fdt, node, "nothing,modem-brom-report", &length);
	assert(dev == &device && slot == 'a' && record && length == 80);
	assert(get32(record) == 1 && (int)get32(record + 4) == -EINPROGRESS);
	assert(fdt_num_mem_rsv(fdt) == 1);
	assert(flushes == 1);
	loader_calls++;
	if (fault == 9)
		loader_publication_fault = 1;
	return loader_error;
}
static int tetris_modem_loaded_boot_report(struct tetris_modem_loaded_report *report)
{
	memset(report, 0, sizeof(*report));
	report->stage = 11;
	report->error = loader_error;
	report->hardware.address = 0x123456789abcdef0ULL;
	report->hardware.reply[3] = 0xfedcba9876543210ULL;
	return report_error;
}
#define fdt_open_into mock_open
#define fdt_add_mem_rsv mock_reserve
#define fdt_setprop mock_setprop
/* libfdt's inline u32 writer was compiled before interception. */
static int mock_u32(void *fdt, int node, const char *name, uint32_t value)
{
	fdt32_t cell = cpu_to_fdt32(value);
	return mock_setprop(fdt, node, name, &cell, sizeof(cell));
}
#define fdt_setprop_u32 mock_u32
#define TETRIS_BROM_BOARD_HOST_TEST 1
#include "../../board/mediatek/mt6878/tetris_modem_brom_board.c"

static unsigned int property(void *fdt, const char *name)
{
	int size;
	const fdt32_t *value = fdt_getprop(fdt, fdt_path_offset(fdt, "/chosen"), name, &size);
	assert(value && size == 4);
	return fdt32_to_cpu(*value);
}
static void tree(char *fdt, int missing, int active)
{
	const char *compatibles[] = { "mediatek,mddriver",
		"mediatek,mt6878-modem-power-controller", "mediatek,mt6878-modem-preflight" };
	unsigned int i;
	assert(!fdt_create_empty_tree(fdt, 16384));
	used = (struct lmb_region){ (uintptr_t)fdt, 16384, 0 };
	available = used;
	memory = (struct lmb){ { &used, 1 }, { &available, 1 } };
	for (i = 0; i < 3 && !missing; i++) {
		int node = fdt_add_subnode(fdt, 0, compatibles[i]);
		assert(node >= 0);
		assert(!fdt_setprop_string(fdt, node, "compatible", compatibles[i]));
		assert(!fdt_setprop_string(fdt, node, "status", active ? "okay" : "disabled"));
	}
}
static void reset(void)
{
	free(allocation); allocation = NULL;
	attempted = 0; first_error = 0; fault = 0; config_conflict = 0;
	loader_calls = loader_error = report_error = loader_publication_fault = 0;
	allocations = frees = unmaps = flushes = 0;
}
int main(void)
{
	char original[65536];
	struct bootm_headers images;
	int i, result, length;
	const unsigned char *record;
	for (i = 0; i <= 9; i++) {
		reset(); tree(original, 0, 0);
		images = (struct bootm_headers){ original, sizeof(original) };
		fault = i;
		result = tetris_modem_brom_only_board(&images);
		assert((i == 0) == (result == 0));
		assert(loader_calls == (i == 0 || i == 9));
		assert(tetris_modem_brom_only_board(&images) == (result ? result : -EALREADY));
		assert(loader_calls == (i == 0 || i == 9));
		assert(images.ft_len == (unsigned long)fdt_totalsize(images.ft_addr));
		assert(property(images.ft_addr, "nothing,modem-brom-preflight-status") == (result ? 2U : 1U));
		assert((int)property(images.ft_addr, "nothing,modem-brom-preflight-error") == result);
		assert((int)property(images.ft_addr, "nothing,modem-brom-publication-error") ==
			(i == 8 || i == 9 ? -FDT_ERR_NOSPACE : 0));
		if (i >= 4 && i <= 8) {
			assert(images.ft_addr == original && frees == (i != 4));
		} else {
			assert(images.ft_addr == allocation && !frees && !unmaps);
		}
		if (i == 0) {
			record = fdt_getprop(images.ft_addr, fdt_path_offset(images.ft_addr, "/chosen"),
				"nothing,modem-brom-report", &length);
			assert(record && length == 80 && get32(record) == 1 && get32(record + 12) == 11);
			assert(get32(record + 40) == 0x9abcdef0 && get32(record + 76) == 0xfedcba98);
		}
	}
	for (i = 0; i < 3; i++) {
		reset(); tree(original, i == 0, i == 1);
		images = (struct bootm_headers){ original, sizeof(original) };
		config_conflict = i == 2;
		result = tetris_modem_brom_only_board(&images);
		assert(result == (i == 0 ? 0 : -EBUSY));
		assert(loader_calls == (i == 0));
		assert(property(images.ft_addr, "nothing,modem-brom-preflight-stage") ==
			(i == 0 ? BROM_FINISHED : i == 2 ? BROM_CONFIG : BROM_CONSUMERS));
	}
	/* Absence keeps the original disabled policy; loader failures still report. */
	reset(); tree(original, 1, 0);
	images = (struct bootm_headers){ original, sizeof(original) };
	loader_error = -ENOENT; report_error = -EINVAL;
	assert(tetris_modem_brom_only_board(&images) == -ENOENT && loader_calls == 1);
	assert((int)property(images.ft_addr, "nothing,modem-brom-preflight-error") == -ENOENT);
	reset(); tree(original, 0, 0);
	images = (struct bootm_headers){ original, sizeof(original) };
	loader_error = -EBUSY; report_error = -EINVAL; fault = 9;
	assert(tetris_modem_brom_only_board(&images) == -EBUSY);
	assert((int)property(images.ft_addr, "nothing,modem-brom-publication-error") == -FDT_ERR_NOSPACE);
	reset(); tree(original, 0, 0);
	images = (struct bootm_headers){ original, sizeof(original) };
	report_error = -EINVAL; fault = 9;
	assert(tetris_modem_brom_only_board(&images) == -EINVAL);
	reset(); images = (struct bootm_headers){ original, sizeof(original) };
	used = (struct lmb_region){ (uintptr_t)original, sizeof(original), 0 };
	available = used;
	memset(original, 0, sizeof(original));
	assert(tetris_modem_brom_only_board(&images) < 0 && !allocations && !flushes && !loader_calls);
	reset(); assert(tetris_modem_brom_only_board(NULL) == -EINVAL);
	/* Exact observed stale FIT bound vs relocated/shrunk Linux DT. */
	reset(); tree(original, 0, 0);
	assert(!fdt_open_into(original, original, 53248));
	used.size = available.size = 53248;
	images = (struct bootm_headers){ original, 50056 };
	assert(!tetris_linux_fdt_sync(&images) && images.ft_len == 53248);
	assert(!tetris_modem_brom_only_board(&images) && loader_calls == 1);
	reset(); tree(original, 0, 0);
	images = (struct bootm_headers){ original, 50056 };
	used.size = sizeof(struct fdt_header) - 1;
	assert(tetris_linux_fdt_sync(&images) == -ERANGE && images.ft_len == 50056);
	used.size = 16383;
	assert(tetris_linux_fdt_sync(&images) == -ERANGE);
	used.size = 16384; available.size = 16383;
	assert(tetris_linux_fdt_sync(&images) == -ERANGE);
	available.size = 16384; used.flags = LMB_NOMAP;
	assert(tetris_linux_fdt_sync(&images) == -ERANGE);
	used.flags = 0; memory.used_mem.count = 0;
	assert(tetris_linux_fdt_sync(&images) == -ERANGE);
	memory.used_mem.count = 1; used.base = UINT64_MAX - 2; used.size = 8;
	assert(tetris_linux_fdt_sync(&images) == -ERANGE);
	reset();
	puts("BROM board preflight/report fault fixtures PASS (mocked hardware only)");
	return 0;
}
