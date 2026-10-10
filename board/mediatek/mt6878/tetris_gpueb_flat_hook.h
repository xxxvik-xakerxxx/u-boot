/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TETRIS_GPUEB_FLAT_HOOK_H
#define TETRIS_GPUEB_FLAT_HOOK_H
#include "tetris_gpueb_flat.h"
struct blk_desc;
struct bootm_headers;
/* Called ONCE inside the existing validated slot-A B4.1 crypto window.
 * Does not register/unlock the crypto service or replace its profile checks.
 */
int tetris_gpueb_flat_capture_slot_a(struct blk_desc *dev,
	struct tetris_scp_crypto *crypto,
	const struct tetris_scp_security_ops *security, const u8 root_pin[32]);
/* Call at END of board_prep_linux, after all remaining board DT edits.
 * Relocates final DT into a separate LMB reservation with actual extra capacity.
 */
int tetris_gpueb_flat_publish_final(struct bootm_headers *images);
#endif
