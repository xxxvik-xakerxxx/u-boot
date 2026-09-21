#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Synthetic certificates only: no vendor keys, firmware or calibration."""
import datetime
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import tetris_scp_security as scp
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from pyasn1.codec.der import encoder
from pyasn1.type import univ


class SecurityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(x509.NameOID.COMMON_NAME, "test")])
        cls.template = scp.der(x509.CertificateBuilder().subject_name(name)
            .issuer_name(name).public_key(cls.root.public_key()).serial_number(1)
            .not_valid_before(datetime.datetime(2026, 1, 1))
            .not_valid_after(datetime.datetime(2027, 1, 1))
            .sign(cls.root, hashes.SHA256(), rsa_padding=padding.PSS(
                mgf=padding.MGF1(hashes.SHA256()), salt_length=32))
            .public_bytes(serialization.Encoding.DER))
        cls.root_pin = hashlib.sha256(encoder.encode(cls.template[0][6])).hexdigest()

    def cert(self, key, signer, fields):
        cert = univ.Sequence()
        tbs = univ.Sequence()
        for i in range(7):
            tbs[i] = self.template[0][i]
        tbs[6] = scp.der(key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo))
        for suffix, value in fields:
            tbs[len(tbs)] = univ.ObjectIdentifier(scp.PREFIX + suffix)
            tbs[len(tbs)] = value
        cert[0] = tbs
        cert[1] = self.template[1]
        cert[2] = univ.BitString.fromOctetString(signer.sign(encoder.encode(tbs),
            padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
            hashes.SHA256()))
        return encoder.encode(cert)

    def parts(self, duplicate=False, bad_digest=False, short_wrapped=False):
        payload = b"synthetic ciphertext" * 16
        image_spki = scp.der(self.key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo))
        cert1 = self.cert(self.root, self.root, [("1.2", image_spki)])
        fields = [("2.1", univ.BitString.fromOctetString(
            b"x" * 32 if bad_digest else hashlib.sha256(payload).digest())),
            ("2.8", univ.BitString.fromOctetString(b"w" * (31 if short_wrapped else 32))),
            ("4.2", univ.BitString.fromOctetString(b"d" * 32))]
        if duplicate:
            fields.append(fields[0])
        cert2 = self.cert(self.key, self.key, fields)
        return [payload, cert1, cert2] * 2

    def container(self, parts=None):
        data = bytearray()
        for name, payload in zip(scp.NAMES, parts or self.parts()):
            header = bytearray(512)
            struct.pack_into("<II", header, 0, 0x58881688, len(payload))
            header[8:8 + len(name)] = name.encode()
            struct.pack_into("<II", header, 48, 0x58891689, 512)
            struct.pack_into("<I", header, 68, 16)
            data.extend(header)
            data.extend(payload)
            data.extend(bytes((-len(data)) % 16))
        return data

    def test_valid_and_pinned(self):
        data = self.container()
        self.assertEqual(scp.inspect(data)["trust"], "self-consistent-only")
        self.assertEqual(scp.inspect(data, self.root_pin)["trust"], "caller-pinned-root")
        with self.assertRaises(ValueError):
            scp.inspect(data, "0" * 64)

    def test_corrupt_payload(self):
        data = self.container()
        data[512] ^= 1
        with self.assertRaisesRegex(ValueError, "ciphertext digest"):
            scp.inspect(data)

    def test_metadata_rejection(self):
        for kwargs in ({"duplicate": True}, {"bad_digest": True}, {"short_wrapped": True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                scp.inspect(self.container(self.parts(**kwargs)))

    def test_signature_rejection(self):
        parts = self.parts()
        parts[2] = parts[2][:-1] + bytes([parts[2][-1] ^ 1])
        from cryptography.exceptions import InvalidSignature
        with self.assertRaises(InvalidSignature):
            scp.inspect(self.container(parts))

    def test_bounds_and_layout(self):
        original = self.container()
        for end in (0, 511, 512, len(original) - 1):
            with self.subTest(end=end), self.assertRaises(ValueError):
                scp.inspect(original[:end])
        for offset in (0, 4, 8, 48, 52, 68):
            data = original.copy()
            data[offset] ^= 0xff
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                scp.inspect(data)
        with self.assertRaises(ValueError):
            scp.inspect(original + b"unexpected")

    def test_trailing_der(self):
        parts = self.parts()
        parts[2] += b"\0"
        with self.assertRaises(ValueError):
            scp.inspect(self.container(parts))

    def test_wrong_key_and_pss_parameters(self):
        parts = self.parts()
        cert = scp.der(parts[2])
        cert[0][6] = self.template[0][6]
        parts[2] = encoder.encode(cert)
        with self.assertRaisesRegex(ValueError, "cert2 key differs"):
            scp.inspect(self.container(parts))
        parts = self.parts()
        cert = scp.der(parts[2])
        cert[1][1][2] = cert[1][1][2].clone(20)
        parts[2] = encoder.encode(cert)
        with self.assertRaisesRegex(ValueError, "RSA-PSS parameters"):
            scp.inspect(self.container(parts))

    def test_report_does_not_expose_wrapped_material(self):
        import json
        report = json.dumps(scp.inspect(self.container()))
        self.assertNotIn((b"w" * 32).hex(), report)
        self.assertNotIn("w" * 32, report)


if __name__ == "__main__":
    unittest.main()
