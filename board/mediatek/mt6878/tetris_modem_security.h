/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_SECURITY_H
#define __TETRIS_MODEM_SECURITY_H

#include <stddef.h>

struct tetris_scp_security_ops;

/*
 * Verify the certificate chain and signed header/payload digests for the
 * observed B4.1 non-transform profile. The caller supplies an independent root
 * pin and immutable, bounded input buffers. No allocation, SMC or writes.
 *
 * Success is signature verification only, NOT permission to execute firmware:
 * slot/SKU, rollback policy, header semantics and memory/reset ownership still
 * require validation by a loader. There is no modem boot-path caller yet.
 */
int tetris_modem_verify_signature(const void *cert1, size_t size1,
				  const void *cert2, size_t size2,
				  const void *header, size_t header_size,
				  const void *image, size_t image_size,
				  const unsigned char root_pin[32],
				  const struct tetris_scp_security_ops *ops);

#endif
