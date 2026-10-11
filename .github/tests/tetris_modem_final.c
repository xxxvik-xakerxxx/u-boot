// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

typedef uint64_t phys_addr_t;
struct bootm_headers { void *ft_addr; unsigned long ft_len; };
#define LMB_MEM_ALLOC_MAX 1
#define LMB_NOOVERWRITE 2
#define LMB_NONOTIFY 4
#define TETRIS_MODEM_LINUX_TAG_COUNT 21
#define CONFIG_TETRIS_MODEM_METADATA_HANDOFF enabled
#define IS_ENABLED(x) (x)
static int enabled, fault, alloc_calls, encode_calls, check_calls, frees, flushes;
static int encode_result, free_error;
static void *blocks[2];
static size_t block_sizes[2];
static const phys_addr_t addresses[2] = { 0x70000000ULL, 0x70010000ULL };

static int lmb_alloc_mem(int mode, int align, phys_addr_t *address,
			 size_t size, int flags)
{
	int index = alloc_calls++;
	assert(index < 2 && mode == 1 && flags == 2);
	assert(align == (index ? 4096 : 65536));
	if (fault == (index ? 4 : 1))
		return -ENOMEM;
	blocks[index] = malloc(size);
	assert(blocks[index]);
	memset(blocks[index], 0xab, size);
	block_sizes[index] = size;
	*address = addresses[index];
	return 0;
}
static void *map_sysmem(phys_addr_t address, size_t size)
{
	int index = address == addresses[1];
	assert(address == addresses[index] && size == block_sizes[index]);
	return fault == (index ? 5 : 2) ? NULL : blocks[index];
}
static void unmap_sysmem(void *pointer)
{
	assert(pointer == blocks[0] || pointer == blocks[1]);
}
static int lmb_free(phys_addr_t address, size_t size, int flags)
{
	int index = address == addresses[1];
	assert(address == addresses[index] && size == block_sizes[index] && flags == 6);
	free(blocks[index]);
	blocks[index] = NULL;
	frees++;
	return free_error;
}
static void flush_dcache_range(unsigned long start, unsigned long end)
{
	int index = (void *)start == blocks[1];
	assert((void *)start == blocks[index] && end - start == block_sizes[index]);
	flushes++;
}
static int tetris_modem_loaded_encode_tags(void *buffer, size_t size)
{
	const unsigned char *bytes = buffer;
	size_t i;
	assert(buffer == blocks[0] && size == 65536);
	for (i = 0; i < size; i++)
		assert(bytes[i] == 0);
	encode_calls++;
	if (fault == 3)
		return -EAGAIN;
	if (encode_result > 0 && encode_result <= (int)size)
		memset(buffer, 0x5a, encode_result);
	return encode_result;
}
static int mock_check_full(const void *fdt, size_t size)
{
	check_calls++;
	return fault == 12 && check_calls == 2 ? -FDT_ERR_BADSTRUCTURE :
		fdt_check_full(fdt, size);
}
static int mock_open(const void *source, void *target, int size)
{
	return fault == 6 ? -FDT_ERR_BADSTRUCTURE : fdt_open_into(source, target, size);
}
static int mock_reserve(void *fdt, uint64_t address, uint64_t size)
{
	return fault == 7 ? -FDT_ERR_NOSPACE : fdt_add_mem_rsv(fdt, address, size);
}
static int mock_add(void *fdt, int parent, const char *name)
{
	return fault == 8 && !strcmp(name, "tetris-modem-linux-tags") ?
		-FDT_ERR_NOSPACE : fdt_add_subnode(fdt, parent, name);
}
static int mock_set(void *fdt, int node, const char *name, const void *data, int size)
{
	if ((fault == 9 && !strcmp(name, "reg")) ||
	    (fault == 10 && !strcmp(name, "no-map")) ||
	    (fault == 11 && !strcmp(name, "ccci,modem_info_v2")))
		return -FDT_ERR_NOSPACE;
	return fdt_setprop(fdt, node, name, data, size);
}
#define fdt_check_full mock_check_full
#define fdt_open_into mock_open
#define fdt_add_mem_rsv mock_reserve
#define fdt_add_subnode mock_add
#define fdt_setprop mock_set
#define TETRIS_MODEM_FINAL_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_modem_final.c"
#undef fdt_check_full
#undef fdt_open_into
#undef fdt_add_mem_rsv
#undef fdt_add_subnode
#undef fdt_setprop

static unsigned int get32(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}
static void reset(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		free(blocks[i]);
		blocks[i] = NULL;
		block_sizes[i] = 0;
	}
	attempted = first_error = fault = alloc_calls = encode_calls = 0;
	check_calls = frees = flushes = free_error = 0;
	enabled = 1;
	encode_result = 4096;
}
static int tree(char *fdt)
{
	int parent, node;
	assert(!fdt_create_empty_tree(fdt, 32768));
	parent = fdt_add_subnode(fdt, 0, "reserved-memory");
	assert(parent >= 0);
	assert(!fdt_setprop_u32(fdt, parent, "#address-cells", 2));
	assert(!fdt_setprop_u32(fdt, parent, "#size-cells", 2));
	assert(!fdt_setprop(fdt, parent, "ranges", NULL, 0));
	node = fdt_add_subnode(fdt, 0, "modem");
	assert(node >= 0);
	assert(!fdt_setprop_string(fdt, node, "compatible", "mediatek,mddriver"));
	assert(!fdt_setprop_string(fdt, node, "status", "disabled"));
	return node;
}
int main(void)
{
	char original[32768], snapshot[32768];
	struct bootm_headers images;
	int i, result, node, length;
	const unsigned char *descriptor;
	const fdt64_t *reg;
	fdt64_t cells[2];
	for (i = 0; i <= 12; i++) {
		reset(); tree(original); memcpy(snapshot, original, sizeof(snapshot));
		images = (struct bootm_headers){ original, sizeof(original) };
		fault = i;
		result = tetris_modem_publish_final(&images);
		assert((i == 0) == (result == 0));
		assert(!memcmp(original, snapshot, sizeof(snapshot)));
		assert(tetris_modem_publish_final(&images) == (result ? result : -EALREADY));
		if (result) {
			assert(images.ft_addr == original && images.ft_len == sizeof(original));
			assert(!blocks[0] && !blocks[1] && flushes == 0);
			continue;
		}
		assert(images.ft_addr == blocks[1] && images.ft_len == (unsigned long)fdt_totalsize(blocks[1]));
		assert(alloc_calls == 2 && encode_calls == 1 && frees == 0 && flushes == 2);
		node = fdt_node_offset_by_compatible(images.ft_addr, -1, "mediatek,mddriver");
		descriptor = fdt_getprop(images.ft_addr, node, "ccci,modem_info_v2", &length);
		assert(descriptor && length == 32);
		assert(get32(descriptor) == addresses[0] && get32(descriptor + 4) == 0);
		assert(get32(descriptor + 8) == 4096 && get32(descriptor + 16) == 2);
		assert(get32(descriptor + 20) == 21 && !get32(descriptor + 12));
		assert(!get32(descriptor + 24) && !get32(descriptor + 28));
		assert(!strcmp(fdt_getprop(images.ft_addr, node, "status", &length), "disabled"));
		node = fdt_path_offset(images.ft_addr, "/reserved-memory/tetris-modem-linux-tags");
		assert(node >= 0);
		reg = fdt_getprop(images.ft_addr, node, "reg", &length);
		assert(reg && length == 16);
		memcpy(cells, reg, sizeof(cells));
		assert(fdt64_to_cpu(cells[0]) == addresses[0]);
		assert(fdt64_to_cpu(cells[1]) == 65536);
		assert(fdt_getprop(images.ft_addr, node, "no-map", &length) && length == 0);
		assert(fdt_num_mem_rsv(images.ft_addr) == 2);
		assert(((unsigned char *)blocks[0])[4095] == 0x5a);
		assert(((unsigned char *)blocks[0])[4096] == 0);
		assert(((unsigned char *)blocks[0])[65535] == 0);
	}
	/* Disabled opt-in, active/missing/duplicate consumers and inherited tags. */
	for (i = 0; i < 7; i++) {
		reset(); node = tree(original);
		images = (struct bootm_headers){ original, sizeof(original) };
		if (i == 0) enabled = 0;
		if (i == 1) assert(!fdt_setprop_string(original, node, "status", "okay"));
		if (i == 2) assert(!fdt_delprop(original, node, "status"));
		if (i == 3) assert(!fdt_del_node(original, node));
		if (i == 4 || i == 5)
			assert(!fdt_setprop(original, node, i == 4 ? "ccci,modem_info" : "ccci,modem_info_v2", NULL, 0));
		if (i == 6) {
			node = fdt_add_subnode(original, 0, "second-modem");
			assert(node >= 0);
			assert(!fdt_setprop_string(original, node, "compatible", "mediatek,mddriver"));
		}
		assert(tetris_modem_publish_final(&images) < 0 && !alloc_calls && !encode_calls);
		assert(images.ft_addr == original);
	}
	/* Never adopt inherited tag nodes, malformed geometry or a missing parent. */
	for (i = 0; i < 4; i++) {
		reset(); tree(original);
		images = (struct bootm_headers){ original, sizeof(original) };
		node = fdt_path_offset(original, "/reserved-memory");
		if (i == 0) assert(fdt_add_subnode(original, node, "tetris-modem-linux-tags") >= 0);
		if (i == 1) assert(!fdt_setprop_u32(original, node, "#address-cells", 1));
		if (i == 2) assert(!fdt_setprop_u32(original, node, "ranges", 0));
		if (i == 3) assert(!fdt_del_node(original, node));
		memcpy(snapshot, original, sizeof(snapshot));
		assert(tetris_modem_publish_final(&images) < 0);
		assert(images.ft_addr == original && !blocks[0] && !blocks[1] && !flushes);
		assert(!memcmp(original, snapshot, sizeof(snapshot)));
	}
	for (i = 0; i < 3; i++) {
		reset(); tree(original);
		images = (struct bootm_headers){ original, sizeof(original) };
		encode_result = i == 0 ? 0 : i == 1 ? 65537 : -EKEYREJECTED;
		result = tetris_modem_publish_final(&images);
		assert(result == (i == 2 ? -EKEYREJECTED : -EPROTO));
		assert(alloc_calls == 1 && frees == 1 && !flushes);
	}
	/* Cleanup failures may not replace the first failure or trigger retries. */
	reset(); tree(original);
	images = (struct bootm_headers){ original, sizeof(original) };
	fault = 11; free_error = -EIO;
	result = tetris_modem_publish_final(&images);
	assert(result == -FDT_ERR_NOSPACE && frees == 2);
	assert(tetris_modem_publish_final(&images) == result && alloc_calls == 2);
	reset();
	puts("Tetris final metadata: actual publisher transactions PASS");
	return 0;
}
