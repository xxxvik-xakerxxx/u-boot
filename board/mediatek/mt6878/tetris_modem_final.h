/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_FINAL_H
#define __TETRIS_MODEM_FINAL_H
#include <stddef.h>
struct bootm_headers;
/* Only the real private loader owner produces these bytes after actual BROM. */
int tetris_modem_loaded_encode_tags(void *buffer, size_t size);
/* Separate opt-in final publication. Does not enable the MD consumer or power MD.
 * Requires a disabled sole consumer node, rejects inherited descriptors, clones
 * the CURRENT final DT (including earlier GPU changes), and commits once.
 * An explicit chosen diagnostic marker may expose only the native observation-
 * only ON-adoption supplier after successful fresh metadata publication.
 */
int tetris_modem_publish_final(struct bootm_headers *images);
/* BROM-only experiment, no CCCI descriptor publication, no retry. */
int tetris_modem_brom_only_board(struct bootm_headers *images);
#endif
