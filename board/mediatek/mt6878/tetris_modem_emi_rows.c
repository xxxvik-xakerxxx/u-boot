// SPDX-License-Identifier: GPL-2.0+
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_modem_emi_rows.h"

static int overlap(const void *a, size_t na, const void *b, size_t nb)
{
	size_t x = (size_t)a, y = (size_t)b;

	return x > (size_t)-1 - na || y > (size_t)-1 - nb ||
		(x < y + nb && y < x + na);
}

static int window(const struct tetris_modem_emi_window *w,
		  const struct tetris_modem_emi_resources *r)
{
	return w->base && w->capacity && r->dram_size &&
		r->dram_base <= ~0ULL - r->dram_size &&
		w->base >= r->dram_base && w->base - r->dram_base < r->dram_size &&
		w->capacity <= r->dram_size - (w->base - r->dram_base);
}

static int intersect(unsigned long long a, unsigned long long na,
		     unsigned long long b, unsigned long long nb)
{
	return a < b + nb && b < a + na;
}

static int phy_size(const char *gear, unsigned int *size)
{
	unsigned int n = 0, i;

	*size = 0;
	if (!gear || !*gear)
		return 0;
	for (i = 0; i < 10; i++) {
		if (!gear[i]) {
			/* LK shifts a 32-bit gear by20; reject wraparound profiles. */
			if (n > 4095)
				return -ERANGE;
			*size = n > 1536 ? 0x60000000U : n << 20;
			return 0;
		}
		if (gear[i] < '0' || gear[i] > '9' ||
		    n > (0xffffffffU - (unsigned int)(gear[i] - '0')) / 10)
			return -EINVAL;
		n = n * 10 + (unsigned int)(gear[i] - '0');
	}
	return -EINVAL;
}

static void padding(struct tetris_modem_memory_map *map)
{
	unsigned int i, candidates = 0, biggest = 0, index = 0;

	/* Exact LK 0x27128..0x27260; one preset row (slot40) in this image. */
	for (i = 0; i < map->count; i++) {
		struct tetris_modem_block *b = &map->blocks[i];

		if (b->attributes & 8) {
			candidates++;
		} else if (b->attributes & 4) {
			if (i + 1 < map->count &&
			    (b->info & map->blocks[i + 1].info & 0xff)) {
				b->attributes |= 8;
				candidates++;
			} else {
				b->attributes |= 0x10;
			}
		}
	}
	if (candidates <= 1) {
		for (i = 0; i < map->count; i++)
			if (map->blocks[i].attributes & 8)
				map->blocks[i].attributes |= 0x10;
		return;
	}
	for (i = 0; i < map->count; i++) {
		const struct tetris_modem_block *b = &map->blocks[i];

		if ((b->attributes & 0x18) == 8 && b->size > biggest) {
			biggest = b->size;
			index = i;
		}
	}
	if (biggest)
		map->blocks[index].attributes |= 0x10;
}

int tetris_modem_emi_row_policy_b41(unsigned int slot, unsigned int role,
		unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS])
{
	static const unsigned char pin[32] =
		"\x5d\x2b\xed\xd0\x00\x49\xfc\xed\x98\x3d\x3a\xe5\x39\x89\x61\x6c"
		"\x5c\x9f\x46\xc4\xed\xdd\x32\xa0\x1a\x1a\xd4\x6f\xf8\xd2\xc5\x0f";
	unsigned int aid;

	if (!policy || slot < 32 || slot > 43)
		return -EINVAL;
	memset(policy, 0, sizeof(*policy) * TETRIS_MODEM_EMI_POLICY_WORDS);
	if (slot == 39) {
		/* Pinned preloader normal table row39, NOT its AEE alternative. */
		policy[47 / 32] |= 3ULL << (2 * (47 % 32));
		for (aid = 240; aid <= 241; aid++)
			policy[aid / 32] |= 2ULL << (2 * (aid % 32));
		return 0;
	}
	if (slot == 40) {
		static const unsigned char permission[4][3] = {
			{ 2, 2, 0 }, { 3, 3, 3 }, { 3, 2, 2 }, { 2, 3, 3 },
		};
		static const unsigned char aids[] = { 35, 47, 93 };
		unsigned int i;

		if (role > 3)
			return -EINVAL;
		for (i = 0; i < 3; i++)
			policy[aids[i] / 32] |= (unsigned long long)permission[role][i]
				<< (2 * (aids[i] % 32));
		return 0;
	}
	return tetris_modem_plan_emi_policy(pin, slot, policy);
}

static int row(struct tetris_modem_emi_rows *out, unsigned int slot,
		unsigned int role, unsigned int kind, unsigned long long start,
		unsigned long long size, const struct tetris_modem_emi_window *w)
{
	struct tetris_modem_emi_row *r = &out->row[slot - 32];
	struct tetris_modem_emi_range plan;
	int ret;

	if (r->kind || !size || start < w->base ||
	    start - w->base >= w->capacity || size > w->capacity - (start - w->base))
		return -ERANGE;
	ret = tetris_modem_plan_emi(start, size, slot, &plan);
	if (ret)
		return ret;
	r->kind = kind;
	r->role = role;
	r->slot = slot;
	r->start = start;
	r->size = size;
	r->reservation = *w;
	return tetris_modem_emi_row_policy_b41(slot, role, r->policy);
}

int tetris_modem_emi_rows_b41(const void *rom, size_t rom_size, size_t dsp_size,
		const struct tetris_modem_emi_resources *resources,
		unsigned int ccb_gear, const char *phy_gear,
		const unsigned char preloader_sha256[32],
		struct tetris_modem_emi_rows *out)
{
	struct tetris_modem_emi_rows result = { 0 };
	struct tetris_modem_layout layout;
	struct tetris_modem_remap remap;
	unsigned long long profile[8];
	unsigned int role, i, sib_size, used_preset = 0;
	int ret;

	if (!rom || !resources || !preloader_sha256 || !out ||
	    overlap(rom, rom_size, out, sizeof(*out)) ||
	    overlap(resources, sizeof(*resources), out, sizeof(*out)) ||
	    overlap(preloader_sha256, 32, out, sizeof(*out)))
		return -EINVAL;
	/* Check actual pinned policy profile even if auxiliary rows are absent. */
	ret = tetris_modem_plan_emi_policy(preloader_sha256, 32, profile);
	if (ret)
		return ret;
	ret = tetris_modem_plan_remap(resources->firmware.base, resources->firmware.capacity,
		resources->dram_base, resources->dram_size, &remap);
	if (ret)
		return ret;
	ret = tetris_modem_plan_layout(rom, rom_size, dsp_size,
		resources->firmware.capacity, &layout);
	if (!ret)
		ret = tetris_modem_plan_memory(rom, rom_size, dsp_size,
			resources->firmware.base, resources->firmware.capacity, &result.memory);
	if (!ret)
		ret = tetris_modem_plan_smem_rom_b41(rom, rom_size, dsp_size,
			resources->firmware.capacity, ccb_gear, &result.smem_inputs, &result.smem);
	if (!ret)
		ret = phy_size(phy_gear, &sib_size);
	if (ret)
		return ret;
	if (!window(&resources->nc, resources) || !window(&resources->cache, resources) ||
	    ((resources->nc.base | resources->cache.base) & 0x1ffffffULL) ||
	    resources->nc.capacity < result.smem.nc_capacity ||
	    resources->cache.capacity < result.smem.cache_capacity ||
	    intersect(resources->nc.base, resources->nc.capacity,
		resources->cache.base, resources->cache.capacity) ||
	    intersect(resources->firmware.base, resources->firmware.capacity,
		resources->nc.base, resources->nc.capacity) ||
	    intersect(resources->firmware.base, resources->firmware.capacity,
		resources->cache.base, resources->cache.capacity))
		return -ERANGE;
	padding(&result.memory);
	for (role = 0; role < 4; role++) {
		unsigned int fragments = 0;

		for (i = 0; i < result.memory.count;) {
			const struct tetris_modem_block *b = &result.memory.blocks[i];
			unsigned long long start, size;
			unsigned int slot, kind;

			if (!(b->info & (1U << role)) || (b->attributes & 0x10)) {
				i++;
				continue;
			}
			start = b->physical;
			size = 0;
			do {
				size += result.memory.blocks[i].size;
				i++;
			} while (i < result.memory.count &&
				 (result.memory.blocks[i].info & (1U << role)) &&
				 !(result.memory.blocks[i].attributes & 0x10));
			slot = role ? role + 35 : 32;
			kind = TETRIS_MD_EMI_RANGE;
			if (fragments++) {
				if (used_preset++)
					return -ENOSPC;
				slot = 40;
				kind = TETRIS_MD_EMI_PRESET_RANGE;
			}
			ret = row(&result, slot, role, kind, start, size,
				&resources->firmware);
			if (ret)
				return ret;
		}
	}
	/* Exact LK 0x27390..0x274dc: first DSP, first info-bit8, first attr-bit6. */
	for (role = 4; role <= 6; role++) {
		for (i = 0; i < result.memory.count; i++) {
			const struct tetris_modem_block *b = &result.memory.blocks[i];
			int match = role == 4 ? !!(b->attributes & 2) :
				role == 5 ? !!(b->info & 0x100) : !!(b->attributes & 0x40);

			if (!match)
				continue;
			if (role == 4 && (b->attributes & 0x100))
				return -EBADMSG;
			ret = row(&result, role + 29, role, TETRIS_MD_EMI_RANGE,
				b->physical, b->size, &resources->firmware);
			if (ret)
				return ret;
			break;
		}
	}
	if (sib_size) {
		if (!window(&resources->sib, resources) || resources->sib.capacity < sib_size ||
		    intersect(resources->sib.base, resources->sib.capacity,
			resources->firmware.base, resources->firmware.capacity) ||
		    intersect(resources->sib.base, resources->sib.capacity,
			resources->nc.base, resources->nc.capacity) ||
		    intersect(resources->sib.base, resources->sib.capacity,
			resources->cache.base, resources->cache.capacity))
			return -ERANGE;
		ret = row(&result, 39, 7, TETRIS_MD_EMI_RANGE, resources->sib.base,
			sib_size, &resources->sib);
		if (ret)
			return ret;
	} else if (resources->sib.base || resources->sib.capacity) {
		return -EINVAL;
	}
	/* The CONSYS id22 cache row always has bit6: zero size is NOT absent in LK.
	 * Reject that active zero-length profile; never silently skip its protection.
	 */
	ret = row(&result, 41, 8, TETRIS_MD_EMI_RANGE,
		resources->cache.base + result.smem.cache[0].offset,
		result.smem.cache[0].size, &resources->cache);
	if (!ret)
		ret = row(&result, 42, 10, TETRIS_MD_EMI_RANGE, resources->nc.base,
			result.smem.nc_capacity, &resources->nc);
	if (!ret)
		ret = row(&result, 43, 9, TETRIS_MD_EMI_RANGE, resources->cache.base,
			result.smem.cache_capacity, &resources->cache);
	if (ret)
		return ret;
	*out = result;
	return 0;
}
