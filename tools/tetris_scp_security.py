#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Verify SCP container consistency offline; never decrypt or touch hardware.

MediaTek cert2 uses OID/value pairs directly in TBSCertificate, not X.509
extensions. A successful signature check is NOT an efuse-root trust decision.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from pyasn1.codec.der import decoder, encoder
from pyasn1.type import univ

PREFIX = "2.16.886.2454."
NAMES = ("tinysys-scp-RV55_A", "cert1", "cert2",
         "tinysys-scp-RV55_A_dram", "cert1", "cert2")
MAX_IMAGE = 32 * 1024 * 1024
MAX_CERT = 16384


def require(condition, message):
    if not condition:
        raise ValueError(message)


def der(data):
    value, tail = decoder.decode(bytes(data))
    require(not tail and encoder.encode(value) == data, "noncanonical DER")
    return value


def sections(data):
    require(len(data) <= MAX_IMAGE, "container exceeds size limit")
    result = []
    offset = 0
    for name in NAMES:
        require(offset + 512 <= len(data), "truncated section header")
        header = data[offset:offset + 512]
        magic, size = struct.unpack_from("<II", header)
        ext, header_size = struct.unpack_from("<II", header, 48)
        align = struct.unpack_from("<I", header, 68)[0]
        require(magic == 0x58881688 and ext == 0x58891689,
                "invalid container magic")
        require(header_size == 512 and align == 16, "unsupported header layout")
        require(header[8:40].split(b"\0", 1)[0] == name.encode(),
                "unexpected section name/order")
        require(0 < size <= len(data) - offset - 512, "invalid payload size")
        if name.startswith("cert"):
            require(size <= MAX_CERT, "oversized certificate")
        end = offset + 512 + size
        result.append(data[offset + 512:end])
        offset = (end + 15) & ~15
        require(offset <= len(data), "truncated alignment padding")
    require(not any(data[offset:]), "unexpected trailing payload")
    return result


def certificate(data):
    require(len(data) <= MAX_CERT, "oversized certificate")
    cert = der(data)
    require(isinstance(cert, univ.Sequence) and len(cert) == 3,
            "invalid certificate envelope")
    tbs, algorithm, signature = cert[0], cert[1], cert[2]
    require(isinstance(tbs, univ.Sequence) and len(tbs) >= 7 and
            (len(tbs) - 7) % 2 == 0, "invalid MediaTek certificate fields")
    require(isinstance(algorithm, univ.Sequence) and len(algorithm) == 2 and
            str(algorithm[0]) == "1.2.840.113549.1.1.10", "expected RSA-PSS")
    params = algorithm[1]
    require(len(params) in (3, 4) and
            [int(params[i].tagSet[-1].tagId) for i in range(len(params))] ==
            list(range(len(params))),
            "invalid RSA-PSS parameter tags")
    require(str(params[0][0]) == "2.16.840.1.101.3.4.2.1" and
            str(params[1][0]) == "1.2.840.113549.1.1.8" and
            str(params[1][1][0]) == "2.16.840.1.101.3.4.2.1" and
            int(params[2]) == 32 and (len(params) == 3 or int(params[3]) == 1),
            "unsupported RSA-PSS parameters")
    require(isinstance(signature, univ.BitString) and len(signature) % 8 == 0,
            "invalid signature bits")
    fields = {}
    for i in range(7, len(tbs), 2):
        require(isinstance(tbs[i], univ.ObjectIdentifier), "expected field OID")
        oid = str(tbs[i])
        require(oid not in fields, "duplicate certificate OID")
        fields[oid] = tbs[i + 1]
    return cert, fields


def public_key(spki):
    key = serialization.load_der_public_key(encoder.encode(spki))
    require(isinstance(key, rsa.RSAPublicKey) and key.key_size == 2048,
            "unsupported signing key")
    return key


def verify_signature(cert, key):
    key.verify(cert[2].asOctets(), encoder.encode(cert[0]),
               padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
               hashes.SHA256())


def field_bits(fields, suffix, size):
    value = fields[PREFIX + suffix]
    require(isinstance(value, univ.BitString) and len(value) == size * 8,
            "invalid metadata bit-string length")
    return value.asOctets()


def inspect(data, expected_root=None):
    parts = sections(data)
    report = {"trust": "self-consistent-only", "components": []}
    roots = set()
    for index in (0, 3):
        cert1, fields1 = certificate(parts[index + 1])
        cert2, fields2 = certificate(parts[index + 2])
        root_spki = encoder.encode(cert1[0][6])
        root_hash = hashlib.sha256(root_spki).hexdigest()
        roots.add(root_hash)
        if expected_root is not None:
            require(root_hash == expected_root, "root key pin mismatch")
        verify_signature(cert1, public_key(cert1[0][6]))
        image_key = fields1[PREFIX + "1.2"]
        require(encoder.encode(image_key) == encoder.encode(cert2[0][6]),
                "cert2 key differs from cert1 image key")
        verify_signature(cert2, public_key(image_key))
        digest = hashlib.sha256(parts[index]).digest()
        require(field_bits(fields2, "2.1", 32) == digest,
                "ciphertext digest mismatch")
        # Length checks only: their contents must not appear in diagnostics.
        field_bits(fields2, "2.8", 32)
        field_bits(fields2, "4.2", 32)
        report["components"].append({
            "name": NAMES[index], "payload_size": len(parts[index]),
            "ciphertext_sha256": digest.hex(), "signatures": "verified",
            "root_spki_sha256": root_hash,
            "wrapped_material_bytes": 32, "postprocess_digest_bytes": 32,
        })
    require(len(roots) == 1, "components use different root keys")
    if expected_root is not None:
        report["trust"] = "caller-pinned-root"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--expected-root-sha256",
                        help="independently obtained SHA256 of root SPKI DER")
    args = parser.parse_args()
    try:
        with args.image.open("rb") as image:
            data = image.read(MAX_IMAGE + 1)
        report = inspect(data, args.expected_root_sha256)
    except Exception as error:
        # Library exceptions can contain decoded material; never print them.
        parser.exit(1, f"SCP verification failed ({type(error).__name__})\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
