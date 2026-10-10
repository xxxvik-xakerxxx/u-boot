/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TETRIS_GPUEB_FLAT_PUBLISH_H
#define TETRIS_GPUEB_FLAT_PUBLISH_H
#include "tetris_gpueb_flat.h"
/* Atomic DT commit. Success consumes *image for boot-lifetime ownership.
 * Failure discards it, unless discard fails (handle remains for recovery).
 * fdt_capacity is actual writable buffer capacity, not an inferred DT size.
 */
int tetris_gpueb_flat_publish(struct tetris_gpueb_flat **image,
	void *fdt, size_t fdt_capacity);
#endif
