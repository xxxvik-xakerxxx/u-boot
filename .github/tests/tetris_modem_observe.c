// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libfdt.h>

typedef uint64_t u64;
typedef uint64_t phys_addr_t;
struct bootm_headers { char *ft_addr; unsigned long ft_len; };
struct blk_desc { int unused; };
struct arm_smccc_res { unsigned long a0; };
enum lmb_mem_type { LMB_MEM_ALLOC_MAX };
enum { UCLASS_SCSI = 2, LMB_NONE = 0 };

static _Alignas(8) unsigned char relocated[8192];
static struct blk_desc device;
static unsigned int allocations, releases, queries, hashes;
static int allocation_error, hash_error, device_error, rejected_slot;
static u64 low = 0x40000000, mapsize = 0x40000000;
static phys_addr_t allocated_address = 0x50000000;

static u64 env_get_bootm_low(void) { return low; }
static u64 env_get_bootm_mapsize(void) { return mapsize; }

static int lmb_alloc_mem(enum lmb_mem_type type, u64 alignment,
			 phys_addr_t *address, unsigned int size, unsigned int flags)
{
	assert(type == LMB_MEM_ALLOC_MAX && alignment == 4096 && flags == LMB_NONE);
	assert(*address == low + mapsize && size <= sizeof(relocated));
	allocations++;
	*address = allocated_address;
	return allocation_error;
}

static long lmb_free(phys_addr_t address, unsigned int size, unsigned int flags)
{
	assert(address == allocated_address && size <= sizeof(relocated));
	assert(flags == LMB_NONE);
	releases++;
	return 0;
}

static void *map_sysmem(phys_addr_t address, unsigned int size)
{
	assert(address == allocated_address && size <= sizeof(relocated));
	return relocated;
}

static int blk_get_desc(unsigned int type, unsigned int index, struct blk_desc **out)
{
	assert(type == UCLASS_SCSI && index == 2);
	*out = &device;
	return device_error;
}

int tetris_scp_check_atf_profile(struct blk_desc *dev)
{
	assert(dev == &device);
	hashes++;
	return hash_error;
}

static void arm_smccc_smc(unsigned long function, unsigned long operation,
			  unsigned long query, unsigned long slot, unsigned long group,
			  unsigned long x5, unsigned long x6, unsigned long x7,
			  struct arm_smccc_res *result)
{
	unsigned int step = queries % 11;

	assert(function == 0xc2000415 && operation == 2);
	assert(slot == 32 + queries / 11 && !x5 && !x6 && !x7);
	assert(query == (step == 0 ? 3 : step < 3 ? step - 1 : 4));
	assert(group == (step < 3 ? 0 : step - 3));
	queries++;
	if (!step)
		result->a0 = slot == (unsigned int)rejected_slot ? ~0UL : slot & 1;
	else if (step == 1)
		result->a0 = 0x120000000ULL;
	else if (step == 2)
		result->a0 = 0x122000000ULL | (1ULL << 43);
	else
		result->a0 = ~0UL;
}

#define TETRIS_MODEM_OBSERVE_HOST_TEST
#define TETRIS_MODEM_LAYOUT_HOST_TEST
#include "../../board/mediatek/mt6878/tetris_modem_emi.c"
#include "../../board/mediatek/mt6878/tetris_modem_observe.c"

static unsigned int get_word(void *fdt, const char *name)
{
	int length, node = fdt_path_offset(fdt, "/chosen");
	const fdt32_t *value = fdt_getprop(fdt, node, name, &length);

	assert(value && length == 4);
	return fdt32_to_cpu(*value);
}

static void run_case(unsigned int scenario)
{
	_Alignas(8) unsigned char original[1024], before[1024];
	struct bootm_headers images = { .ft_addr = (char *)original };
	const fdt64_t *snapshot;
	unsigned int i, size;
	int node, length, ret;

	memset(original, 0xa5, sizeof(original));
	assert(!fdt_create_empty_tree(original, sizeof(original)));
	assert(fdt_add_subnode(original, 0, "chosen") >= 0);
	assert(!fdt_pack(original));
	size = fdt_totalsize(original);
	images.ft_len = size;
	/* A packed source has no room for even the first report property. */
	node = fdt_path_offset(original, "/chosen");
	assert(fdt_setprop_u32(original, node, "no-space", 1) == -FDT_ERR_NOSPACE);
	memcpy(before, original, sizeof(before));
	if (scenario >= 1 && scenario <= 12)
		rejected_slot = 31 + scenario;
	else if (scenario == 13)
		allocation_error = -ENOMEM;
	else if (scenario == 14)
		hash_error = -EKEYREJECTED;
	else if (scenario == 15)
		device_error = -ENODEV;
	else if (scenario == 16)
		mapsize = 0;
	else if (scenario == 17)
		low = ~0ULL - mapsize + 1;
	else if (scenario == 18)
		allocated_address = low - 4096;

	ret = tetris_modem_observe_diagnostic(&images);
	assert(!memcmp(original, before, sizeof(before)));
	if (scenario == 13 || scenario >= 16) {
		assert(ret == (scenario == 13 ? -ENOMEM : -ERANGE));
		assert(images.ft_addr == (char *)original && images.ft_len == size);
		assert(!queries && !hashes);
		assert(releases == (scenario == 18));
		assert(allocations == (scenario == 13 || scenario == 18));
	} else {
		assert(images.ft_addr == (char *)relocated && images.ft_len == size + 4096);
		assert(allocations == 1 && !releases);
		assert((int)get_word(relocated, "tetris,modem-emi-error") == ret);
		node = fdt_path_offset(relocated, "/chosen");
		snapshot = fdt_getprop(relocated, node, "tetris,modem-emi-snapshot", &length);
		assert(snapshot && length == 12 * 11 * 8);
		if (scenario) {
			assert(ret == (scenario == 14 ? -EKEYREJECTED :
				       scenario == 15 ? -ENODEV : -EIO));
			assert(queries == (scenario <= 12 ? (scenario - 1) * 11 + 1 : 0));
			assert(hashes == (scenario != 15));
			assert(get_word(relocated, "tetris,modem-emi-slot") ==
			       (unsigned int)(scenario <= 12 ? rejected_slot : 32));
			assert(get_word(relocated, "tetris,modem-emi-step") == 0);
			for (i = 0; i < 12 * 11; i++)
				assert(fdt64_to_cpu(snapshot[i]) == 0);
		} else {
			assert(!ret && queries == 132 && hashes == 1);
			assert(get_word(relocated, "tetris,modem-emi-slot") == 43);
			assert(get_word(relocated, "tetris,modem-emi-step") == 10);
			for (i = 0; i < 12; i++) {
				assert(fdt64_to_cpu(snapshot[i * 11]) == (i & 1));
				assert(fdt64_to_cpu(snapshot[i * 11 + 1]) == 0x120000000ULL);
				assert(fdt64_to_cpu(snapshot[i * 11 + 2]) ==
				       (0x122000000ULL | (1ULL << 43)));
				assert(fdt64_to_cpu(snapshot[i * 11 + 10]) == ~0ULL);
			}
		}
	}
	i = queries;
	assert(tetris_modem_observe_diagnostic(&images) == -EALREADY);
	assert(queries == i && !memcmp(original, before, sizeof(before)));
}

int main(void)
{
	unsigned int scenario;
	int status;
	pid_t child;

	/* Every child represents a fresh U-Boot session; never reset its guard. */
	for (scenario = 0; scenario <= 18; scenario++) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			run_case(scenario);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
	puts("modem EMI production observer: PASS (19 offline FDT/SMC fault scenarios)");
	return 0;
}
