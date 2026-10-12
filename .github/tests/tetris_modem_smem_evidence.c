// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../board/mediatek/mt6878/tetris_modem_smem_evidence.h"

static const unsigned char pin[32] =
	"\x5d\x2b\xed\xd0\x00\x49\xfc\xed\x98\x3d\x3a\xe5\x39\x89\x61\x6c"
	"\x5c\x9f\x46\xc4\xed\xdd\x32\xa0\x1a\x1a\xd4\x6f\xf8\xd2\xc5\x0f";

static void valid(struct tetris_modem_loaded_report *report)
{
	struct tetris_modem_bootstrap_report *hw;
	unsigned int bank, group;

	memset(report, 0, sizeof(*report));
	report->stage = TETRIS_MD_LOAD_COMPLETE;
	hw = &report->hardware;
	hw->stage = TETRIS_MD_BOOT_COMPLETE;
	hw->reply[2] = hw->reply[3] = 0x100000001ULL;
	hw->emi.attempted = 1;
	hw->emi.slot = 43;
	report->bootstrap.resources.nc = (struct tetris_modem_emi_window){ 0xa0000000ULL, 0x8000000 };
	report->bootstrap.resources.cache = (struct tetris_modem_emi_window){ 0xc0000000ULL, 0x8000000 };
	report->bootstrap.rows.smem.nc_capacity = 0x800000;
	report->bootstrap.rows.smem.cache_capacity = 0x4000000;
	memcpy(report->bootstrap.preloader_sha256, pin, sizeof(pin));
	for (bank = 0; bank < 2; bank++) {
		struct tetris_modem_emi_row *row = &report->bootstrap.rows.row[10 + bank];
		struct tetris_modem_emi_transaction *tx = &hw->emi.ranges[10 + bank];
		struct tetris_modem_emi_range range;

		row->kind = TETRIS_MD_EMI_RANGE;
		row->slot = 42 + bank;
		row->role = bank ? 9 : 10;
		row->reservation = bank ? report->bootstrap.resources.cache : report->bootstrap.resources.nc;
		row->start = row->reservation.base;
		row->size = bank ? 0x4000000 : 0x800000;
		assert(!tetris_modem_plan_emi_policy(pin, row->slot, row->policy));
		assert(!tetris_modem_plan_emi(row->start, row->size, row->slot, &range));
		tx->state = TETRIS_MODEM_EMI_RANGE_VERIFIED;
		tx->slot = row->slot;
		tx->step = TETRIS_MODEM_EMI_STEPS - 1;
		tx->reply[10] = range.start_readback;
		tx->reply[11] = range.end_readback | TETRIS_MODEM_EMI_END_MARKER;
		tx->reply[12] = 1;
		for (group = 0; group < 8; group++)
			tx->reply[1 + group] = tx->reply[13 + group] = row->policy[group];
	}
}

static unsigned int get32(const unsigned char *p)
{
	return p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static unsigned long long get64(const unsigned char *p)
{
	return get32(p) | (unsigned long long)get32(p + 4) << 32;
}

int main(void)
{
	struct tetris_modem_loaded_report report, snapshot;
	unsigned char output[337], before[337];
	unsigned int bank, group, which, count = 0;

	valid(&report);
	memset(output, 0xa5, sizeof(output));
	snapshot = report;
	assert(tetris_modem_build_smem_evidence(&report, output, sizeof(output)) == 336);
	assert(!memcmp(&report, &snapshot, sizeof(report)) && output[336] == 0xa5);
	assert(get32(output) == 1 && get32(output + 4) == 336 && get32(output + 8) == 1);
	assert(get32(output + 12) == 2 && get32(output + 16) == 6878 && get32(output + 20) == 6299);
	assert(!memcmp(output + 48, pin, 32));
	for (bank = 0; bank < 2; bank++) {
		unsigned char *p = output + 80 + bank * 128;
		const struct tetris_modem_emi_row *row = &report.bootstrap.rows.row[10 + bank];

		assert(get32(p) == bank && get32(p + 4) == 42 + bank);
		assert(get64(p + 8) == row->reservation.base && get64(p + 16) == row->reservation.capacity);
		assert(get64(p + 24) == row->start && get64(p + 32) == row->size && get64(p + 56) == 1);
		for (group = 0; group < 8; group++)
			assert(get64(p + 64 + group * 8) == row->policy[group]);
	}
	for (bank = 0; bank < 2; bank++) {
		valid(&report);
		report.hardware.emi.ranges[10 + bank].reply[11] &= ~TETRIS_MODEM_EMI_END_MARKER;
		assert(tetris_modem_build_smem_evidence(&report, output, sizeof(output)) == 336);
		assert(!(get64(output + 80 + bank * 128 + 48) & TETRIS_MODEM_EMI_END_MARKER));
	}
	for (which = 0; which < 29; which++) {
		valid(&report);
		memset(output, 0xa5, sizeof(output));
		memcpy(before, output, sizeof(output));
		switch (which) {
		case 0: report.error = -EACCES; break;
		case 1: report.stage--; break;
		case 2: report.hardware.error = -EIO; break;
		case 3: report.hardware.emi.error = -EPERM; break;
		case 4: report.hardware.cleanup.error = -ETIMEDOUT; break;
		case 5: report.hardware.stage--; break;
		case 6: report.hardware.cleanup.stage++; break;
		case 7: report.hardware.emi.attempted = 0; break;
		case 8: report.hardware.emi.slot--; break;
		case 9: report.hardware.reply[0]++; break;
		case 10: report.hardware.reply[2]--; break;
		case 11: report.hardware.reply[3]--; break;
		case 12: report.bootstrap.preloader_sha256[0] ^= 1; break;
		case 13: report.bootstrap.rows.row[10].kind = TETRIS_MD_EMI_ABSENT; break;
		case 14: report.bootstrap.rows.row[11].slot--; break;
		case 15: report.bootstrap.rows.row[10].role--; break;
		case 16: report.bootstrap.resources.nc.capacity = 0; break;
		case 17: report.bootstrap.rows.row[11].reservation.capacity--; break;
		case 18: report.bootstrap.rows.row[10].start++; break;
		case 19: report.bootstrap.rows.row[11].size = 0; break;
		case 20: report.bootstrap.rows.smem.nc_capacity--; break;
		case 21: report.hardware.emi.ranges[10].state = TETRIS_MODEM_EMI_ATTEMPTED; break;
		case 22: report.hardware.emi.ranges[11].step--; break;
		case 23: report.hardware.emi.ranges[10].slot--; break;
		case 24: report.hardware.emi.ranges[11].reply[0]++; break;
		case 25: report.hardware.emi.ranges[10].reply[9]++; break;
		case 26: report.hardware.emi.ranges[11].reply[12] = 0; break;
		case 27: report.hardware.emi.ranges[10].reply[10] ^= 1; break;
		case 28: report.hardware.emi.ranges[11].reply[11] ^= 1; break;
		}
		assert(tetris_modem_build_smem_evidence(&report, output, sizeof(output)) < 0);
		assert(!memcmp(output, before, sizeof(output)));
		count++;
	}
	for (bank = 0; bank < 2; bank++)
		for (group = 0; group < 8; group++)
			for (which = 0; which < 3; which++) {
				valid(&report);
				memset(output, 0xa5, sizeof(output));
				memcpy(before, output, sizeof(output));
				if (!which)
					report.bootstrap.rows.row[10 + bank].policy[group] ^= 1;
				else
					report.hardware.emi.ranges[10 + bank].reply[(which == 1 ? 1 : 13) + group] ^= 1;
				assert(tetris_modem_build_smem_evidence(&report, output, sizeof(output)) == -EBADMSG);
				assert(!memcmp(output, before, sizeof(output)));
				count++;
			}
	valid(&report);
	snapshot = report;
	assert(tetris_modem_build_smem_evidence(&report, output, 335) == -ENOSPC);
	assert(tetris_modem_build_smem_evidence(NULL, output, sizeof(output)) == -EINVAL);
	assert(tetris_modem_build_smem_evidence(&report, NULL, sizeof(output)) == -EINVAL);
	assert(tetris_modem_build_smem_evidence(&report, &report, sizeof(report)) == -EINVAL);
	assert(!memcmp(&report, &snapshot, sizeof(report)));
	printf("actual SMEM boot-evidence encoder PASS: %u fault vectors, no hardware access\n", count);
	return 0;
}
