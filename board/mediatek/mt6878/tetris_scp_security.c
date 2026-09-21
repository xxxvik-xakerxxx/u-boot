// SPDX-License-Identifier: GPL-2.0+
#ifdef TETRIS_SCP_SECURITY_HOST_TEST
#include <errno.h>
#include <string.h>
#else
#include <linux/errno.h>
#include <linux/string.h>
#include <image.h>
#include <u-boot/rsa.h>
#include <u-boot/sha256.h>
#endif
#include <linux/asn1_decoder.h>
#include "tetris_scp_fields.asn1.h"
#include "tetris_scp_security.h"
#include "tetris_scp_crypto.h"

#define MAX_CERT 16384U
#define MAX_FIELDS 48U

struct field {
	const unsigned char *data;
	size_t size;
	size_t header;
	unsigned char tag;
};

struct fields {
	struct field items[MAX_FIELDS];
	size_t count;
};

struct certificate {
	struct field tbs, key, raw_key, signature;
	struct fields body;
};

/* The generic decoder accepts BER; this consumer requires bounded DER. */
static int der_header(const unsigned char *p, size_t size, size_t *header)
{
	size_t n, len = 0, i;

	if (size < 2 || (p[0] & 31) == 31)
		return -EINVAL;
	if (p[1] < 128) {
		*header = 2;
		return p[1] == size - 2 ? 0 : -EINVAL;
	}
	n = p[1] & 127;
	if (!n || n > 2 || n + 2 > size || !p[2])
		return -EINVAL;
	for (i = 0; i < n; i++)
		len = (len << 8) | p[2 + i];
	*header = n + 2;
	if (len < 128 || (n == 2 && len < 256) || len != size - *header)
		return -EINVAL;
	return 0;
}

int tetris_scp_note_field(void *context, size_t hdrlen, unsigned char tag,
			  const void *value, size_t vlen)
{
	struct fields *out = context;
	struct field *field;
	size_t header;
	const unsigned char *data = (const unsigned char *)value - hdrlen;

	if (out->count == MAX_FIELDS ||
	    der_header(data, hdrlen + vlen, &header) || header != hdrlen)
		return -EINVAL;
	field = &out->items[out->count++];
	field->data = data;
	field->size = hdrlen + vlen;
	field->header = hdrlen;
	field->tag = tag;
	return 0;
}

static int sequence(const void *data, size_t size, struct fields *out)
{
	size_t header;

	memset(out, 0, sizeof(*out));
	if (!data || size > MAX_CERT || der_header(data, size, &header) ||
	    *(const unsigned char *)data != 0x30)
		return -EINVAL;
	return asn1_ber_decoder(&tetris_scp_fields_decoder, out, data, size);
}

static int equal(const struct field *a, const struct field *b)
{
	return a->size == b->size && !memcmp(a->data, b->data, a->size);
}

static int bytes(const struct field *field, const unsigned char *data, size_t n)
{
	return field->size == n && !memcmp(field->data, data, n);
}

static int bits(const struct field *field, size_t n, const unsigned char **value)
{
	if (field->tag != 3 || field->size - field->header != n + 1 ||
	    field->data[field->header])
		return -EINVAL;
	*value = field->data + field->header + 1;
	return 0;
}

static int pss(const struct field *field)
{
	/* Explicit SHA256, MGF1-SHA256, salt 32; default trailer or explicit 1. */
	static const unsigned char parameters[] = {
		0x30, 0x41, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
		0x01, 0x01, 0x0a, 0x30, 0x34, 0xa0, 0x0f, 0x30, 0x0d,
		0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
		0x01, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
		0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08,
		0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
		0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa2, 0x03, 0x02, 0x01, 0x20
	};
	unsigned char explicit_trailer[sizeof(parameters) + 5];
	static const unsigned char trailer[] = { 0xa3, 3, 2, 1, 1 };

	if (bytes(field, parameters, sizeof(parameters)))
		return 0;
	memcpy(explicit_trailer, parameters, sizeof(parameters));
	explicit_trailer[1] += sizeof(trailer);
	explicit_trailer[14] += sizeof(trailer);
	memcpy(explicit_trailer + sizeof(parameters), trailer, sizeof(trailer));
	return bytes(field, explicit_trailer, sizeof(explicit_trailer)) ? 0 : -EINVAL;
}

static int public_key(const struct field *spki, struct field *raw)
{
	static const unsigned char rsa_algorithm[] = {
		0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
		0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00
	};
	static const unsigned char exponent[] = { 2, 3, 1, 0, 1 };
	struct fields fields, integers;
	const unsigned char *value;
	const struct field *modulus;
	size_t size;

	if (sequence(spki->data, spki->size, &fields) || fields.count != 2 ||
	    !bytes(&fields.items[0], rsa_algorithm, sizeof(rsa_algorithm)))
		return -EINVAL;
	size = fields.items[1].size - fields.items[1].header;
	if (!size || bits(&fields.items[1], size - 1, &value) ||
	    sequence(value, size - 1, &integers) || integers.count != 2)
		return -EINVAL;
	modulus = &integers.items[0];
	if (modulus->tag != 2 || modulus->size - modulus->header != 257 ||
	    modulus->data[modulus->header] ||
	    !(modulus->data[modulus->header + 1] & 0x80) ||
	    !(modulus->data[modulus->size - 1] & 1) ||
	    !bytes(&integers.items[1], exponent, sizeof(exponent)))
		return -EINVAL;
	raw->data = value;
	raw->size = size - 1;
	return 0;
}

static int parse_cert(const void *data, size_t size, struct certificate *cert)
{
	/* Audited MediaTek certificates retain this legacy TBS label with PSS. */
	static const unsigned char legacy_label[] = {
		0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
		0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00
	};
	struct fields envelope;
	const unsigned char *signature;
	size_t i, j;
	int ret;

	memset(cert, 0, sizeof(*cert));
	ret = sequence(data, size, &envelope);
	if (ret || envelope.count != 3 || pss(&envelope.items[1]) ||
	    bits(&envelope.items[2], 256, &signature))
		return -EINVAL;
	cert->tbs = envelope.items[0];
	cert->signature = envelope.items[2];
	ret = sequence(cert->tbs.data, cert->tbs.size, &cert->body);
	if (ret || cert->body.count < 7 || (cert->body.count - 7) % 2)
		return -EINVAL;
	/* Never choose the verification algorithm from the legacy inner label. */
	if (!equal(&cert->body.items[2], &envelope.items[1]) &&
	    !bytes(&cert->body.items[2], legacy_label, sizeof(legacy_label)))
		return -EINVAL;
	cert->key = cert->body.items[6];
	if (public_key(&cert->key, &cert->raw_key))
		return -EINVAL;
	for (i = 7; i < cert->body.count; i += 2) {
		if (cert->body.items[i].tag != 6)
			return -EINVAL;
		for (j = 7; j < i; j += 2)
			if (equal(&cert->body.items[i], &cert->body.items[j]))
				return -EINVAL;
	}
	return 0;
}

static const struct field *metadata_field(const struct certificate *cert,
					 unsigned char group, unsigned char item)
{
	unsigned char oid[] = { 0x06, 0x07, 0x60, 0x86, 0x76, 0x93, 0x16, 0, 0 };
	size_t i;

	oid[7] = group;
	oid[8] = item;
	for (i = 7; i < cert->body.count; i += 2)
		if (bytes(&cert->body.items[i], oid, sizeof(oid)))
			return &cert->body.items[i + 1];
	return NULL;
}

static int get_digest(const struct certificate *cert, unsigned char group,
		      unsigned char item, unsigned char out[32])
{
	const struct field *field = metadata_field(cert, group, item);
	const unsigned char *value;

	if (!field || bits(field, 32, &value))
		return -EINVAL;
	memcpy(out, value, 32);
	return 0;
}

int tetris_scp_authenticate(const void *cert1, size_t size1,
			    const void *cert2, size_t size2,
			    const void *image, size_t image_size,
			    const unsigned char root_pin[32],
			    const struct tetris_scp_security_ops *ops,
			    struct tetris_scp_security_metadata *metadata)
{
	struct certificate root, leaf;
	const struct field *delegated;
	unsigned char digest[32];
	int ret = -EINVAL;

	if (!metadata)
		return -EINVAL;
	memset(metadata, 0, sizeof(*metadata));
	if (!root_pin || !ops || !ops->sha256 || !ops->verify || !image ||
	    !image_size || image_size > 0xe00000 || image_size % 16 ||
	    parse_cert(cert1, size1, &root) || parse_cert(cert2, size2, &leaf))
		return -EINVAL;
	ops->sha256(root.key.data, root.key.size, digest);
	if (memcmp(digest, root_pin, 32))
		return -EACCES;
	delegated = metadata_field(&root, 1, 2);
	if (!delegated || !equal(delegated, &leaf.key))
		return -EACCES;
	ret = ops->verify(root.raw_key.data, root.raw_key.size, root.tbs.data, root.tbs.size,
			  root.signature.data + root.signature.header + 1);
	if (ret)
		return ret;
	ret = ops->verify(leaf.raw_key.data, leaf.raw_key.size, leaf.tbs.data, leaf.tbs.size,
			  leaf.signature.data + leaf.signature.header + 1);
	if (ret)
		return ret;
	if (get_digest(&leaf, 2, 1, metadata->ciphertext) ||
	    get_digest(&leaf, 4, 2, metadata->plaintext) ||
	    get_digest(&leaf, 2, 8, metadata->wrapped))
		goto failed;
	ops->sha256(image, image_size, digest);
	if (memcmp(digest, metadata->ciphertext, 32))
		goto failed;
	return 0;
failed:
	memset(metadata, 0, sizeof(*metadata));
	return -EACCES;
}

int tetris_scp_prepare_component(struct tetris_scp_crypto *crypto, void *page,
		const struct tetris_scp_crypto_ops *crypto_ops,
		const struct tetris_scp_security_ops *security_ops,
		const struct tetris_scp_component_input *input,
		const unsigned char root_pin[32])
{
	struct tetris_scp_security_metadata metadata;
	volatile unsigned char *erase = (volatile unsigned char *)&metadata;
	size_t i;
	int ret;

	if (!crypto || !input ||
	    (crypto->state != TETRIS_SCP_CRYPTO_NEW &&
	     crypto->state != TETRIS_SCP_CRYPTO_READY))
		return -EINVAL;
	ret = tetris_scp_authenticate(input->cert1, input->cert1_size,
		input->cert2, input->cert2_size, input->image, input->size,
		root_pin, security_ops, &metadata);
	if (ret)
		goto out;
	ret = tetris_scp_crypto_check_image(crypto_ops, input->image,
					  input->size, input->capacity);
	if (ret)
		goto out;
	if (crypto->state == TETRIS_SCP_CRYPTO_NEW) {
		ret = tetris_scp_crypto_init(crypto, page,
				TETRIS_SCP_CRYPTO_PAGE_SIZE, crypto_ops);
		if (ret)
			goto out;
	} else if (page != crypto->page || crypto_ops != crypto->ops) {
		ret = -EINVAL;
		goto out;
	}
	ret = tetris_scp_crypto_decrypt(crypto, input->image, input->size,
		input->capacity, 1, metadata.wrapped, metadata.ciphertext,
		metadata.plaintext);
out:
	for (i = 0; i < sizeof(metadata); i++)
		erase[i] = 0;
	if (ret)
		crypto->state = TETRIS_SCP_CRYPTO_FAILED;
	return ret;
}

#ifndef TETRIS_SCP_SECURITY_HOST_TEST
static void digest_hw(const void *data, size_t size, unsigned char digest[32])
{
	sha256_csum_wd(data, size, digest, CHUNKSZ_SHA256);
}

static int strict_pss(struct image_sign_info *info, const unsigned char *msg,
		      int msg_len, const unsigned char *hash, int hash_len)
{
	return padding_pss_verify_with_salt(info, msg, msg_len, hash, hash_len, 32);
}

static int verify_hw(const void *key, size_t key_size, const void *tbs,
		     size_t tbs_size, const unsigned char signature[256])
{
	struct image_sign_info info = { 0 };
	struct padding_algo padding = { .name = "pss", .verify = strict_pss };
	unsigned char hash[32];

	info.key = key;
	info.keylen = key_size;
	info.checksum = image_get_checksum_algo("sha256,rsa2048");
	info.crypto = image_get_crypto_algo("sha256,rsa2048");
	info.padding = &padding;
	if (!info.checksum || !info.crypto || !info.padding)
		return -ENOSYS;
	digest_hw(tbs, tbs_size, hash);
	return rsa_verify_with_pkey(&info, hash, (unsigned char *)signature, 256);
}

const struct tetris_scp_security_ops tetris_scp_security_hw_ops = {
	.sha256 = digest_hw,
	.verify = verify_hw,
};
#endif
