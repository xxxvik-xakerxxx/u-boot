/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_GPUEB_PREPARE_H
#define __TETRIS_GPUEB_PREPARE_H
#include "tetris_gpueb_layout.h"
#include "tetris_scp_crypto.h"
#include "tetris_scp_security.h"

enum tetris_gpueb_prepare_state {
	TETRIS_GPUEB_PREPARE_NEW,
	TETRIS_GPUEB_PREPARE_FAILED,
	TETRIS_GPUEB_PREPARE_DONE,
};

struct tetris_gpueb_prepare {
	enum tetris_gpueb_prepare_state state;
};

struct tetris_gpueb_report {
	struct tetris_gpueb_plain_info plain;
	unsigned char plaintext_sha256[32];
};

/*
 * One attempt on an already registered/unlocked READY crypto context.
 * Caller proves matching ATF/boot window and exclusively reserves staging's
 * full capacity and the service page. Container and metadata remain immutable,
 * with all buffers disjoint. No registration/allocation/publication/start.
 * Staging is erased on every path after its safe capacity/ownership checks,
 * including success. Only bounded format metadata and verified hash survive.
 * Report remains unchanged on failure; validated attempts cannot be retried.
 * Invalid pointer/alias/context rejection is preflight only: no buffer writes.
 */
int tetris_gpueb_transform_only(struct tetris_gpueb_prepare *attempt,
		struct tetris_scp_crypto *crypto,
		const struct tetris_scp_security_ops *security_ops,
		const void *container, size_t container_size,
		void *staging, size_t capacity, const unsigned char root_pin[32],
		struct tetris_gpueb_report *report);
#endif
