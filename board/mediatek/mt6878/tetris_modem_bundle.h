/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_MODEM_BUNDLE_H
#define __TETRIS_MODEM_BUNDLE_H

#include "tetris_modem_layout.h"

struct tetris_scp_security_ops;

/* Offsets into the immutable caller-owned container, not physical addresses. */
struct tetris_modem_member {
	size_t header_offset;
	size_t payload_offset;
	size_t payload_size;
};

struct tetris_modem_bundle {
	/* Fixed order: md1rom, md1drdi, md1dsp. */
	struct tetris_modem_member members[3];
	size_t consumed;
	struct tetris_modem_layout layout;
};

/*
 * Authenticate adjacent image/certificate groups from one immutable snapshot
 * and validate the signed ROM layout. Output is unchanged on error. Scans at
 * most 128 headers and stops after the three groups; trailing partition data
 * is not certified. No allocation, destination write, hardware or DT change.
 *
 * The caller supplies independent root trust and keeps the snapshot immutable
 * through any later copy. Success is not SKU/rollback/pairing policy, memory
 * ownership, secure-reset authority or permission to execute the images.
 */
int tetris_modem_authenticate_bundle(const void *container, size_t size,
				     const unsigned char root_pin[32],
				     const struct tetris_scp_security_ops *ops,
				     size_t reserved_capacity,
				     struct tetris_modem_bundle *bundle);

/*
 * Authenticate the complete supported bundle before copying ROM and DSP into
 * caller-owned, mapped destination RAM. DRDI mode 3 is authenticated but not
 * copied, matching the layout planner. Gaps and unused capacity are untouched.
 * Snapshot, destination and output must not overlap. All errors leave both
 * destination and output unchanged; snapshot must stay immutable throughout.
 * Caller must establish exclusive ownership, independent trust and platform
 * policy. This does NOT flush caches, program protection/remaps, release reset
 * or publish CCCI tags. There is deliberately no automatic boot caller.
 */
int tetris_modem_place_bundle(const void *container, size_t size,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity,
		struct tetris_modem_layout *layout);

#endif
