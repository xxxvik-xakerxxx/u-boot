// SPDX-License-Identifier: GPL-2.0+
#ifndef TETRIS_MODEM_OBSERVE_HOST_TEST
#include <blk.h>
#include <bootm.h>
#include <dm/uclass-id.h>
#include <env.h>
#include <lmb.h>
#include <mapmem.h>
#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#endif
#include "tetris_modem_emi.h"
#include "tetris_scp_security.h"

static int read_smc(void *context, unsigned int function, unsigned int operation,
		    unsigned long long a, unsigned long long b,
		    unsigned long long c, unsigned long long *reply)
{
	struct arm_smccc_res result = { 0 };

	/* A dedicated adapter cannot accidentally expose the range writer. */
	if (function != 0xc2000415U || operation != 2 || b < 32 || b > 43 ||
	    !((a <= 1 && !c) || (a == 3 && !c) || (a == 4 && c < 8)))
		return -EINVAL;
	arm_smccc_smc(function, operation, a, b, c, 0, 0, 0, &result);
	*reply = result.a0;
	return 0;
}

int tetris_modem_observe_diagnostic(struct bootm_headers *images)
{
	static bool attempted;
	const struct tetris_modem_emi_ops ops = { .smc = read_smc };
	fdt64_t snapshot[12][TETRIS_MODEM_EMI_READ_WORDS] = { 0 };
	struct tetris_modem_emi_observation observation;
	struct blk_desc *dev;
	phys_addr_t address;
	void *fdt;
	u64 low, mapsize;
	unsigned int bytes;
	unsigned int slot = 32, word, step = 0;
	int node, ret, report;

	if (attempted)
		return -EALREADY;
	attempted = true;
	if (!images || !images->ft_addr)
		return -EINVAL;
	fdt = images->ft_addr;
	ret = fdt_check_header(fdt);
	if (ret)
		return ret;
	bytes = fdt_totalsize(fdt);
	if (bytes > 0x7fffffffU - 4096)
		return -ERANGE;
	bytes += 4096;
	low = env_get_bootm_low();
	mapsize = env_get_bootm_mapsize();
	if (!mapsize || low > ~0ULL - mapsize)
		return -ERANGE;
	address = low + mapsize;
	/* image_setup_linux() has already shrunk the original FDT. Never grow
	 * that buffer in place beyond its LMB reservation. Keep the old FDT
	 * reserved and intact; allocate a separate bounded Linux handoff copy.
	 */
	ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 4096, &address, bytes, LMB_NONE);
	if (ret)
		return ret;
	if (address < low) {
		lmb_free(address, bytes, LMB_NONE);
		return -ERANGE;
	}
	ret = fdt_open_into(fdt, map_sysmem(address, bytes), bytes);
	if (ret) {
		lmb_free(address, bytes, LMB_NONE);
		return ret;
	}
	fdt = map_sysmem(address, bytes);
	images->ft_addr = fdt;
	images->ft_len = bytes;
	node = fdt_path_offset(fdt, "/chosen");
	if (node < 0)
		return node;
	/* Allocate every report property before the first secure query. */
	ret = fdt_setprop(fdt, node, "tetris,modem-emi-snapshot", snapshot,
			  sizeof(snapshot));
	if (ret)
		return ret;
	ret = fdt_setprop_u32(fdt, node, "tetris,modem-emi-error", -EINPROGRESS);
	if (ret)
		return ret;
	ret = fdt_setprop_u32(fdt, node, "tetris,modem-emi-slot", slot);
	if (ret)
		return ret;
	ret = fdt_setprop_u32(fdt, node, "tetris,modem-emi-step", step);
	if (ret)
		return ret;
	ret = blk_get_desc(UCLASS_SCSI, 2, &dev);
	if (!ret)
		ret = tetris_scp_check_atf_profile(dev);
	if (ret)
		goto out;
	/* Serialized queries: ATF shares a selector register across policy reads. */
	for (slot = 32; slot <= 43; slot++) {
		memset(&observation, 0, sizeof(observation));
		ret = tetris_modem_read_emi_slot(slot, &ops, &observation);
		step = observation.step;
		if (ret)
			goto out;
		for (word = 0; word < TETRIS_MODEM_EMI_READ_WORDS; word++)
			snapshot[slot - 32][word] = cpu_to_fdt64(observation.words[word]);
	}
	slot = 43;
	ret = fdt_setprop(fdt, node, "tetris,modem-emi-snapshot", snapshot,
			  sizeof(snapshot));
out:
	report = fdt_setprop_u32(fdt, node, "tetris,modem-emi-slot", slot);
	if (!report)
		report = fdt_setprop_u32(fdt, node, "tetris,modem-emi-step", step);
	if (!report)
		report = fdt_setprop_u32(fdt, node, "tetris,modem-emi-error", ret);
	return ret ? ret : report;
}
