/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __TETRIS_SCP_SECURITY_H
#define __TETRIS_SCP_SECURITY_H

#include <stddef.h>

struct tetris_scp_security_ops {
	void (*sha256)(const void *data, size_t size, unsigned char digest[32]);
	/* PKCS#1 DER public key; RSA-2048, PSS, SHA256, salt length 32. */
	int (*verify)(const void *key, size_t key_size,
		      const void *tbs, size_t tbs_size,
		      const unsigned char signature[256]);
};

struct tetris_scp_security_metadata {
	unsigned char ciphertext[32];
	unsigned char plaintext[32];
	unsigned char wrapped[32];
};

/* Caller supplies an independent root pin, not one extracted from cert1. */
int tetris_scp_authenticate(const void *cert1, size_t size1,
			    const void *cert2, size_t size2,
			    const void *image, size_t image_size,
			    const unsigned char root_pin[32],
			    const struct tetris_scp_security_ops *ops,
			    struct tetris_scp_security_metadata *metadata);

#ifndef TETRIS_SCP_SECURITY_HOST_TEST
extern const struct tetris_scp_security_ops tetris_scp_security_hw_ops;
#endif
#endif
