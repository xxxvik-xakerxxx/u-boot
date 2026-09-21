#!/usr/bin/env python3
"""Exercise the U-Boot C parser with real RSA verification and synthetic keys."""
import ctypes as c
import hashlib
import importlib.util
import os
from pathlib import Path
import sys
import unittest

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

library = c.CDLL(sys.argv.pop(1))
spec = importlib.util.spec_from_file_location(
    "fixtures", Path(__file__).with_name("test_tetris_scp_security.py"))
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
Hash = c.CFUNCTYPE(None, c.c_void_p, c.c_size_t, c.c_void_p)
Verify = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t, c.c_void_p)


@Hash
def digest(data, size, out):
    c.memmove(out, hashlib.sha256(c.string_at(data, size)).digest(), 32)


@Verify
def verify(key, key_size, tbs, tbs_size, sig):
    try:
        public = serialization.load_der_public_key(c.string_at(key, key_size))
        if not isinstance(public, rsa.RSAPublicKey) or public.key_size != 2048:
            return -1
        public.verify(c.string_at(sig, 256), c.string_at(tbs, tbs_size),
                      padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
                      hashes.SHA256())
        return 0
    except Exception:
        return -1


class Ops(c.Structure):
    _fields_ = [("sha256", Hash), ("verify", Verify)]


ops = Ops(digest, verify)
authenticate = library.tetris_scp_authenticate
authenticate.argtypes = [c.c_void_p, c.c_size_t] * 3 + [c.c_void_p, c.POINTER(Ops), c.c_void_p]
authenticate.restype = c.c_int


class RuntimeSecurityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.SecurityTest.setUpClass()
        cls.fixture = fixtures.SecurityTest()

    def call(self, parts=None, pin=None):
        parts = parts or self.fixture.parts()
        pin = pin if pin is not None else bytes.fromhex(self.fixture.root_pin)
        output = c.create_string_buffer(b"x" * 96, 96)
        result = authenticate(parts[1], len(parts[1]), parts[2], len(parts[2]),
                              parts[0], len(parts[0]), pin, c.byref(ops), output)
        if result:
            self.assertEqual(output.raw, bytes(96))
        return result, output.raw

    def test_valid(self):
        result, output = self.call()
        self.assertEqual(result, 0)
        self.assertEqual(output[32:], b"d" * 32 + b"w" * 32)

    def test_root_pin(self):
        self.assertNotEqual(self.call(pin=bytes(32))[0], 0)

    def test_payload(self):
        parts = self.fixture.parts()
        parts[0] = b"x" + parts[0][1:]
        self.assertNotEqual(self.call(parts)[0], 0)

    def test_signatures(self):
        for index in (1, 2):
            parts = self.fixture.parts()
            parts[index] = parts[index][:-1] + bytes([parts[index][-1] ^ 1])
            self.assertNotEqual(self.call(parts)[0], 0)

    def test_metadata(self):
        for options in ({"duplicate": True}, {"bad_digest": True}, {"short_wrapped": True}):
            self.assertNotEqual(self.call(self.fixture.parts(**options))[0], 0)

    def test_wrong_key_and_algorithm(self):
        scp = fixtures.scp
        for change in ("key", "salt", "trailer", "inner_algorithm"):
            parts = self.fixture.parts()
            cert = scp.der(parts[2])
            if change == "key":
                cert[0][6] = self.fixture.template[0][6]
            elif change == "salt":
                cert[1][1][2] = cert[1][1][2].clone(20)
            elif change == "trailer":
                cert[1][0] = fixtures.univ.ObjectIdentifier("1.2.840.113549.1.1.11")
            else:
                cert[0][2][0] = fixtures.univ.ObjectIdentifier("1.2.840.113549.1.1.5")
            parts[2] = fixtures.encoder.encode(cert)
            self.assertNotEqual(self.call(parts)[0], 0)

    @unittest.skipUnless(os.environ.get("SCP_TEST_IMAGE"), "no local vendor fixture")
    def test_local_container_consistency_only(self):
        parts = fixtures.scp.sections(Path(os.environ["SCP_TEST_IMAGE"]).read_bytes())
        for index in (0, 3):
            cert, _ = fixtures.scp.certificate(parts[index + 1])
            # This pin is only for an offline parser equivalence test, NOT trust.
            pin = hashlib.sha256(fixtures.encoder.encode(cert[0][6])).digest()
            self.assertEqual(self.call(parts[index:index + 3], pin)[0], 0)

    def test_truncation_and_trailing(self):
        for index in (1, 2):
            original = self.fixture.parts()
            for size in range(len(original[index])):
                parts = original.copy()
                parts[index] = parts[index][:size]
                self.assertNotEqual(self.call(parts)[0], 0)
            parts = original.copy()
            parts[index] += b"\0"
            self.assertNotEqual(self.call(parts)[0], 0)


if __name__ == "__main__":
    unittest.main()
