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
