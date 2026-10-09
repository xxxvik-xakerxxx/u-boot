// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_GPUEB_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#endif
#include "tetris_gpueb_prepare.h"

static void wipe(void *data, size_t size)
{
	volatile unsigned char *p = data;

	while (size--)
		*p++ = 0;
}

static int overlap(const void *a, size_t na, const void *b, size_t nb)
{
	unsigned long x = (unsigned long)a, y = (unsigned long)b;

	if (na > ~0UL - x || nb > ~0UL - y)
		return 1;
	return x < y + nb && y < x + na;
}

int tetris_gpueb_transform_only(struct tetris_gpueb_prepare *attempt,
		struct tetris_scp_crypto *crypto,
		const struct tetris_scp_security_ops *security_ops,
		const void *container, size_t container_size,
		void *staging, size_t capacity, const unsigned char root_pin[32],
		struct tetris_gpueb_report *report)
{
	struct tetris_scp_security_metadata metadata = { 0 };
	struct tetris_gpueb_layout layout;
	struct tetris_gpueb_report result = { 0 };
	const struct tetris_scp_crypto_ops *ops;
	const unsigned char *p = container;
	const void *inputs[6], *outputs[3];
	size_t input_sizes[6], output_sizes[3];
	size_t size;
	unsigned int i, j;
	int ret;

	if (!attempt || !crypto || !security_ops || !root_pin || !report ||
	    !container || !container_size || container_size > TETRIS_GPUEB_MAX_CONTAINER ||
	    !staging || !capacity || capacity > TETRIS_GPUEB_MAX_IMAGE)
		return -EINVAL;
	if (attempt->state != TETRIS_GPUEB_PREPARE_NEW)
		return -EALREADY;
	if (crypto->state != TETRIS_SCP_CRYPTO_READY || !crypto->page || !crypto->ops)
		return -EINVAL;
	ops = crypto->ops;
	if (!ops->smc || !ops->flush || !ops->invalidate || !ops->sha256 ||
	    !ops->physical || ops->physical(crypto->page) != TETRIS_SCP_CRYPTO_PAGE_PA)
		return -EINVAL;
	inputs[0] = container;
	input_sizes[0] = container_size;
	inputs[1] = crypto;
	input_sizes[1] = sizeof(*crypto);
	inputs[2] = security_ops;
	input_sizes[2] = sizeof(*security_ops);
	inputs[3] = root_pin;
	input_sizes[3] = 32;
	inputs[4] = ops;
	input_sizes[4] = sizeof(*ops);
	inputs[5] = crypto->page;
	input_sizes[5] = TETRIS_SCP_CRYPTO_PAGE_SIZE;
	outputs[0] = staging;
	output_sizes[0] = capacity;
	outputs[1] = attempt;
	output_sizes[1] = sizeof(*attempt);
	outputs[2] = report;
	output_sizes[2] = sizeof(*report);
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 6; j++)
			if (overlap(outputs[i], output_sizes[i], inputs[j], input_sizes[j]))
				return -EINVAL;
		for (j = 0; j < i; j++)
			if (overlap(outputs[i], output_sizes[i], outputs[j], output_sizes[j]))
				return -EINVAL;
	}
	attempt->state = TETRIS_GPUEB_PREPARE_FAILED;
	/* Validate the whole erase span before touching it, even for malformed input. */
	ret = tetris_scp_crypto_check_image(ops, staging, capacity, capacity);
	if (ret)
		return ret;
	ret = tetris_gpueb_parse_layout(container, container_size, &layout);
	if (ret)
		goto out;
	size = layout.sections[0].payload_size;
	ret = tetris_scp_crypto_check_image(ops, staging, size, capacity);
	if (ret)
		goto out;
	ret = tetris_gpueb_authenticate(
		p + layout.sections[1].payload_offset, layout.sections[1].payload_size,
		p + layout.sections[2].payload_offset, layout.sections[2].payload_size,
		p + layout.sections[0].header_offset, 512,
		p + layout.sections[0].payload_offset, size, root_pin, security_ops, &metadata);
	if (ret)
		goto out;
	wipe(staging, capacity);
	memcpy(staging, p + layout.sections[0].payload_offset, size);
	ret = tetris_scp_crypto_decrypt(crypto, staging, size, capacity, 1,
		metadata.wrapped, metadata.ciphertext, metadata.plaintext);
	if (ret)
		goto out;
	ret = tetris_gpueb_describe_plain(staging, size, &result.plain);
	if (!ret)
		ret = tetris_gpueb_inspect_segments(staging, size, &result.segments);
	if (!ret) {
		memcpy(result.plaintext_sha256, metadata.plaintext, 32);
		*report = result;
		attempt->state = TETRIS_GPUEB_PREPARE_DONE;
	}
out:
	wipe(staging, capacity);
	ops->flush(staging, capacity);
	wipe(&metadata, sizeof(metadata));
	wipe(&result, sizeof(result));
	return ret;
}
