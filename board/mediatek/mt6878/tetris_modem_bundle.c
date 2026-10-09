// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_MODEM_LAYOUT_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#endif
#include "tetris_modem_bundle.h"
#include "tetris_modem_security.h"
#include "tetris_scp_security.h"

#define HEADER_SIZE 512U
#define MAX_CONTAINER (256U * 1024 * 1024)
#define MAX_PAYLOAD (64U * 1024 * 1024)
#define MAX_CERT 16384U

static unsigned int le_word(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
	       (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static int next_member(const unsigned char *data, size_t size, size_t *cursor,
		       struct tetris_modem_member *member, const char **name)
{
	const unsigned char *header;
	size_t length, end;
	unsigned int i;

	if (*cursor > size || size - *cursor < HEADER_SIZE)
		return -EMSGSIZE;
	header = data + *cursor;
	if (le_word(header) != 0x58881688 ||
	    le_word(header + 48) != 0x58891689 ||
	    le_word(header + 52) != HEADER_SIZE || le_word(header + 68) != 16)
		return -EPROTONOSUPPORT;
	for (i = 8; i < 40 && header[i]; i++)
		if (header[i] > 127)
			return -EBADMSG;
	if (i == 40)
		return -EBADMSG;
	length = le_word(header + 4);
	if (!length || length > size - *cursor - HEADER_SIZE)
		return -EMSGSIZE;
	member->header_offset = *cursor;
	member->payload_offset = *cursor + HEADER_SIZE;
	member->payload_size = length;
	end = member->payload_offset + length;
	/* The container bound makes this alignment addition overflow-safe. */
	*cursor = (end + 15) & ~(size_t)15;
	if (*cursor > size)
		return -EMSGSIZE;
	*name = (const char *)header + 8;
	return 0;
}

int tetris_modem_authenticate_bundle(const void *container, size_t size,
				     const unsigned char root_pin[32],
				     const struct tetris_scp_security_ops *ops,
				     size_t reserved_capacity,
				     struct tetris_modem_bundle *bundle)
{
	static const char *const names[] = { "md1rom", "md1drdi", "md1dsp" };
	struct tetris_modem_bundle out = { 0 };
	struct tetris_modem_member member, cert1 = { 0 };
	const unsigned char *data = container;
	const char *name, *expected;
	size_t cursor = 0;
	unsigned int count, found = 0, stage = 0;
	int active = -1, index, ret;

	if (!data || !size || size > MAX_CONTAINER || !root_pin || !ops ||
	    !ops->sha256 || !ops->verify || !bundle || !reserved_capacity)
		return -EINVAL;
	for (count = 0; count < 128 && found != 7; count++) {
		ret = next_member(data, size, &cursor, &member, &name);
		if (ret)
			return ret;
		if (!stage) {
			for (index = 0; index < 3; index++)
				if (!strcmp(name, names[index]))
					break;
			if (index == 3)
				continue;
			if (found & (1U << index))
				return -EEXIST;
			if (member.payload_size > MAX_PAYLOAD ||
			    member.payload_size % 16)
				return -ERANGE;
			active = index;
			out.members[index] = member;
			stage = 1;
			continue;
		}
		expected = stage == 1 ? (active == 0 ? "cert1md" : "cert1") : "cert2";
		if (strcmp(name, expected) || member.payload_size > MAX_CERT)
			return -EBADMSG;
		if (stage == 1) {
			cert1 = member;
			stage = 2;
			continue;
		}
		ret = tetris_modem_verify_signature(data + cert1.payload_offset,
			cert1.payload_size, data + member.payload_offset,
			member.payload_size, data + out.members[active].header_offset,
			HEADER_SIZE, data + out.members[active].payload_offset,
			out.members[active].payload_size, root_pin, ops);
		if (ret)
			return ret;
		found |= 1U << active;
		stage = 0;
	}
	if (found != 7)
		return -E2BIG;
	ret = tetris_modem_plan_layout(data + out.members[0].payload_offset,
		out.members[0].payload_size, out.members[2].payload_size,
		reserved_capacity, &out.layout);
	if (ret)
		return ret;
	out.consumed = cursor;
	*bundle = out;
	return 0;
}

static int valid_span(const void *base, size_t size)
{
	return base && size && size <= ~0UL - (unsigned long)base;
}

/* Call only with validated non-wrapping spans. */
static int overlaps(const void *a, size_t a_size, const void *b, size_t b_size)
{
	unsigned long first = (unsigned long)a, second = (unsigned long)b;

	return first < second + b_size && second < first + a_size;
}

int tetris_modem_prepare_bundle_b41(const void *container, size_t size,
				    const unsigned char root_pin[32],
				    const struct tetris_scp_security_ops *ops,
				    size_t reserved_capacity, unsigned int ccb_gear,
				    struct tetris_modem_prepared_bundle *prepared)
{
	struct tetris_modem_prepared_bundle out;
	const unsigned char *data = container;
	int ret;

	if (!valid_span(container, size) || !valid_span(prepared, sizeof(*prepared)) ||
	    overlaps(container, size, prepared, sizeof(*prepared)))
		return -EINVAL;
	ret = tetris_modem_authenticate_bundle(container, size, root_pin, ops,
					       reserved_capacity, &out.bundle);
	if (ret)
		return ret;
	ret = tetris_modem_plan_smem_rom_b41(data + out.bundle.members[0].payload_offset,
					     out.bundle.members[0].payload_size,
					     out.bundle.members[2].payload_size,
					     reserved_capacity, ccb_gear,
					     &out.smem_inputs, &out.smem);
	if (ret)
		return ret;
	*prepared = out;
	return 0;
}

int tetris_modem_place_bundle(const void *container, size_t size,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity,
		struct tetris_modem_layout *layout)
{
	struct tetris_modem_bundle bundle;
	const unsigned char *source = container;
	unsigned char *target = destination;
	int ret;

	if (!valid_span(container, size) || !valid_span(destination, capacity) ||
	    !valid_span(layout, sizeof(*layout)))
		return -EINVAL;
	if (overlaps(container, size, destination, capacity) ||
	    overlaps(container, size, layout, sizeof(*layout)) ||
	    overlaps(destination, capacity, layout, sizeof(*layout)))
		return -EINVAL;
	ret = tetris_modem_authenticate_bundle(container, size, root_pin, ops,
					      capacity, &bundle);
	if (ret)
		return ret;
	/* No fallible operation after the first destination write. */
	memcpy(target, source + bundle.members[0].payload_offset,
	       bundle.layout.rom_size);
	memcpy(target + bundle.layout.dsp_offset,
	       source + bundle.members[2].payload_offset, bundle.layout.dsp_size);
	*layout = bundle.layout;
	return 0;
}

int tetris_modem_place_bundle_b41(const void *container, size_t size,
		const unsigned char root_pin[32],
		const struct tetris_scp_security_ops *ops,
		void *destination, size_t capacity, unsigned int ccb_gear,
		struct tetris_modem_boot_plan *plan)
{
	struct tetris_modem_prepared_bundle prepared;
	struct tetris_modem_boot_plan out;
	const unsigned char *source = container;
	unsigned char *target = destination;
	int ret;

	if (!valid_span(container, size) || !valid_span(destination, capacity) ||
	    !valid_span(plan, sizeof(*plan)) ||
	    overlaps(container, size, destination, capacity) ||
	    overlaps(container, size, plan, sizeof(*plan)) ||
	    overlaps(destination, capacity, plan, sizeof(*plan)))
		return -EINVAL;
	ret = tetris_modem_prepare_bundle_b41(container, size, root_pin, ops,
					    capacity, ccb_gear, &prepared);
	if (ret)
		return ret;
	out.layout = prepared.bundle.layout;
	out.smem_inputs = prepared.smem_inputs;
	out.smem = prepared.smem;
	/* Complete authentication and service planning precede both copies. */
	memcpy(target, source + prepared.bundle.members[0].payload_offset,
	       out.layout.rom_size);
	memcpy(target + out.layout.dsp_offset,
	       source + prepared.bundle.members[2].payload_offset, out.layout.dsp_size);
	*plan = out;
	return 0;
}

int tetris_modem_sync_payloads(void *destination, size_t capacity,
		const struct tetris_modem_layout *layout, size_t alignment,
		const struct tetris_modem_cache_ops *ops)
{
	unsigned long base = (unsigned long)destination;
	unsigned long start[2], end[2], mask;
	int ret, i;

	if (!valid_span(destination, capacity) || !layout || !ops || !ops->flush ||
	    !alignment || (alignment & (alignment - 1)))
		return -EINVAL;
	mask = alignment - 1;
	if ((base & mask) || (capacity & mask) || !layout->memory_size ||
	    layout->memory_size > capacity || !layout->rom_size ||
	    layout->rom_size > layout->memory_size || !layout->dsp_size ||
	    layout->dsp_size > layout->dsp_capacity ||
	    layout->dsp_offset < layout->rom_size ||
	    layout->dsp_offset >= layout->memory_size ||
	    layout->dsp_capacity > layout->memory_size - layout->dsp_offset)
		return -ERANGE;
	/* Aligned capacity bounds rounding without overflowing the address space. */
	start[0] = base;
	end[0] = base + (((layout->rom_size - 1UL) | mask) + 1);
	start[1] = base + (layout->dsp_offset & ~mask);
	end[1] = base + (((layout->dsp_offset +
			  (unsigned long)layout->dsp_size - 1) | mask) + 1);
	for (i = 0; i < 2; i++) {
		ret = ops->flush(ops->ctx, start[i], end[i]);
		if (ret)
			return ret;
	}
	return 0;
}

int tetris_modem_initialize_smem_b41(const struct tetris_modem_boot_plan *plan,
		void *firmware, size_t firmware_capacity,
		void *nc, size_t nc_capacity, void *cache, size_t cache_capacity,
		size_t alignment, const struct tetris_modem_cache_ops *ops)
{
	struct tetris_modem_smem_plan expected;
	unsigned long nc_base = (unsigned long)nc, cache_base = (unsigned long)cache;
	size_t preserve, mask;
	int ret;

	if (!valid_span(plan, sizeof(*plan)) || !valid_span(ops, sizeof(*ops)) ||
	    !ops->flush || !alignment || (alignment & (alignment - 1)) ||
	    !valid_span(firmware, firmware_capacity) ||
	    !valid_span(nc, nc_capacity) || !valid_span(cache, cache_capacity))
		return -EINVAL;
	if (overlaps(nc, nc_capacity, cache, cache_capacity) ||
	    overlaps(nc, nc_capacity, firmware, firmware_capacity) ||
	    overlaps(cache, cache_capacity, firmware, firmware_capacity) ||
	    overlaps(nc, nc_capacity, plan, sizeof(*plan)) ||
	    overlaps(cache, cache_capacity, plan, sizeof(*plan)) ||
	    overlaps(nc, nc_capacity, ops, sizeof(*ops)) ||
	    overlaps(cache, cache_capacity, ops, sizeof(*ops)))
		return -EINVAL;
	ret = tetris_modem_plan_smem_b41(&plan->smem_inputs, &expected);
	if (ret)
		return ret;
	if (memcmp(&expected, &plan->smem, sizeof(expected)))
		return -EBADMSG;
	mask = alignment - 1;
	if ((nc_base & mask) || (cache_base & mask) ||
	    (nc_capacity & mask) || (cache_capacity & mask) ||
	    expected.nc_capacity > nc_capacity ||
	    expected.cache_capacity > cache_capacity ||
	    !plan->layout.memory_size || plan->layout.memory_size > firmware_capacity)
		return -ERANGE;
	/* CONSYS has another owner; do not clear or flush its last cache line. */
	preserve = expected.cache[0].size;
	if (preserve) {
		if (mask > cache_capacity - preserve)
			return -ERANGE;
		preserve = (preserve + mask) & ~mask;
	}
	if (preserve >= cache_capacity)
		return -ERANGE;
	/* No fallible validation after the first RAM write. */
	memset(nc, 0, nc_capacity);
	memset((unsigned char *)cache + preserve, 0, cache_capacity - preserve);
	ret = ops->flush(ops->ctx, nc_base, nc_base + nc_capacity);
	if (!ret)
		ret = ops->flush(ops->ctx, cache_base + preserve,
				 cache_base + cache_capacity);
	return ret > 0 ? -EIO : ret;
}

int tetris_modem_plan_service_banks_b41(const struct tetris_modem_boot_plan *plan,
		size_t capacity, struct tetris_modem_service_banks *banks)
{
	struct tetris_modem_smem_plan expected;
	struct tetris_modem_service_banks out;
	size_t mask = 0xffff;
	int ret;

	if (!plan || !banks || !capacity || !plan->layout.memory_size)
		return -EINVAL;
	ret = tetris_modem_plan_smem_b41(&plan->smem_inputs, &expected);
	if (ret)
		return ret;
	if (memcmp(&expected, &plan->smem, sizeof(expected)))
		return -EBADMSG;
	if (capacity <= mask || plan->layout.memory_size > capacity - mask)
		return -ENOSPC;
	out.firmware_capacity = ((size_t)plan->layout.memory_size + mask) & ~mask;
	out.nc_offset = out.firmware_capacity;
	out.nc_capacity = expected.nc_capacity;
	if (out.nc_capacity > capacity - out.nc_offset)
		return -ENOSPC;
	out.cache_offset = out.nc_offset + out.nc_capacity;
	out.cache_capacity = expected.cache_capacity;
	if (out.cache_capacity > capacity - out.cache_offset)
		return -ENOSPC;
	*banks = out;
	return 0;
}
