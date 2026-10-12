// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#endif
#include "tetris_modem_smem_evidence.h"

static void encode32(unsigned char *p, unsigned int value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)(value >> (i * 8));
}

static void encode64(unsigned char *p, unsigned long long value)
{
	encode32(p, (unsigned int)value);
	encode32(p + 4, (unsigned int)(value >> 32));
}

int tetris_modem_build_smem_evidence(const struct tetris_modem_loaded_report *report,
		void *buffer, size_t capacity)
{
	unsigned char encoded[TETRIS_MODEM_SMEM_EVIDENCE_SIZE] = { 0 };
	const struct tetris_modem_bootstrap_report *hardware;
	const struct tetris_modem_bootstrap_plan *bootstrap;
	size_t address = (size_t)buffer, input = (size_t)report;
	unsigned int bank, group;
	int ret;

	if (!report || !buffer)
		return -EINVAL;
	if (capacity < sizeof(encoded))
		return -ENOSPC;
	if (address > (size_t)-1 - sizeof(encoded) ||
	    input > (size_t)-1 - sizeof(*report) ||
	    (address < input + sizeof(*report) && input < address + sizeof(encoded)))
		return -EINVAL;
	if (report->error)
		return report->error < 0 ? report->error : -EIO;
	if (report->stage != TETRIS_MD_LOAD_COMPLETE)
		return -EAGAIN;
	hardware = &report->hardware;
	bootstrap = &report->bootstrap;
	if (hardware->error)
		return hardware->error < 0 ? hardware->error : -EIO;
	if (hardware->emi.error)
		return hardware->emi.error < 0 ? hardware->emi.error : -EIO;
	if (hardware->cleanup.error)
		return hardware->cleanup.error < 0 ? hardware->cleanup.error : -EIO;
	if (hardware->stage != TETRIS_MD_BOOT_COMPLETE ||
	    hardware->cleanup.stage != TETRIS_MD_CLEANUP_NONE ||
	    hardware->emi.attempted != 1 || hardware->emi.slot != 43 ||
	    hardware->reply[0] || hardware->reply[2] != 0x100000001ULL ||
	    hardware->reply[3] != 0x100000001ULL)
		return -EAGAIN;
	for (bank = 0; bank < 2; bank++) {
		const struct tetris_modem_emi_row *row = &bootstrap->rows.row[10 + bank];
		const struct tetris_modem_emi_transaction *transaction =
			&hardware->emi.ranges[10 + bank];
		const struct tetris_modem_emi_window *window = bank ?
			&bootstrap->resources.cache : &bootstrap->resources.nc;
		struct tetris_modem_emi_range range;
		unsigned long long policy[TETRIS_MODEM_EMI_POLICY_WORDS];
		unsigned char *record = encoded + TETRIS_MODEM_SMEM_EVIDENCE_HEADER +
			bank * TETRIS_MODEM_SMEM_EVIDENCE_RECORD;

		if (row->kind != TETRIS_MD_EMI_RANGE || row->slot != 42 + bank ||
		    row->role != (bank ? 9U : 10U) ||
		    !window->base || !window->capacity || window->base > ~0ULL - window->capacity ||
		    window->base >= (1ULL << 35) || window->capacity > (1ULL << 35) - window->base ||
		    row->reservation.base != window->base ||
		    row->reservation.capacity != window->capacity ||
		    row->start != window->base || !row->size || row->size > window->capacity ||
		    row->size != (bank ? bootstrap->rows.smem.cache_capacity :
					bootstrap->rows.smem.nc_capacity))
			return -ERANGE;
		if (bank && bootstrap->resources.nc.base < window->base + window->capacity &&
		    window->base < bootstrap->resources.nc.base + bootstrap->resources.nc.capacity)
			return -ERANGE;
		ret = tetris_modem_plan_emi(row->start, row->size, row->slot, &range);
		if (ret)
			return ret;
		ret = tetris_modem_plan_emi_policy(bootstrap->preloader_sha256, row->slot, policy);
		if (ret)
			return ret;
		if (transaction->state != TETRIS_MODEM_EMI_RANGE_VERIFIED ||
		    transaction->step != TETRIS_MODEM_EMI_STEPS - 1 ||
		    transaction->slot != row->slot ||
		    transaction->reply[0] || transaction->reply[9] || transaction->reply[12] != 1 ||
		    transaction->reply[10] != range.start_readback ||
		    (transaction->reply[11] & ~TETRIS_MODEM_EMI_END_MARKER) != range.end_readback)
			return -EBADMSG;
		for (group = 0; group < TETRIS_MODEM_EMI_POLICY_WORDS; group++) {
			if (row->policy[group] != policy[group] ||
			    transaction->reply[1 + group] != policy[group] ||
			    transaction->reply[13 + group] != policy[group])
				return -EBADMSG;
			encode64(record + 64 + group * 8, transaction->reply[13 + group]);
		}
		encode32(record, bank);
		encode32(record + 4, row->slot);
		encode64(record + 8, window->base);
		encode64(record + 16, window->capacity);
		encode64(record + 24, row->start);
		encode64(record + 32, row->size);
		encode64(record + 40, transaction->reply[10]);
		encode64(record + 48, transaction->reply[11]);
		encode64(record + 56, transaction->reply[12]);
	}
	encode32(encoded, 1);
	encode32(encoded + 4, sizeof(encoded));
	encode32(encoded + 8, TETRIS_MODEM_SMEM_EVIDENCE_SCOPE_BL33);
	encode32(encoded + 12, 2);
	encode32(encoded + 16, 6878);
	encode32(encoded + 20, 6299);
	encode32(encoded + 24, report->stage);
	encode32(encoded + 28, hardware->stage);
	memcpy(encoded + 48, bootstrap->preloader_sha256, 32);
	memcpy(buffer, encoded, sizeof(encoded));
	return sizeof(encoded);
}
