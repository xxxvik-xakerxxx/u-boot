/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_CCCI_TAGS_H
#define __TETRIS_MODEM_CCCI_TAGS_H
#include "tetris_modem_loaded_boot.h"
#include "tetris_modem_bundle.h"
#define TETRIS_MODEM_LINUX_TAG_COUNT 21

/* Internal owner inputs ONLY: pointer-free authenticated load metadata/footer
 * captured before snapshot release, and actual successful load/BROM report.
 * Encodes the consumer's real ABI, not authentication or a READY assertion.
 * Does not read MD RAM, allocate/reserve, flush, publish DT or enable a driver.
 * Returns used bytes; payload framing/errors are atomic via existing encoder.
 */
int tetris_modem_build_linux_tags(const struct tetris_modem_boot_plan *loaded,
		const unsigned char check_header[512],
		const struct tetris_modem_loaded_report *report, void *buffer, size_t size);
#endif
