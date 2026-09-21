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

Smc = c.CFUNCTYPE(c.c_uint64, c.c_uint32, c.c_uint64, c.c_uint64)
Cache = c.CFUNCTYPE(None, c.c_void_p, c.c_size_t)
CryptoHash = c.CFUNCTYPE(None, c.c_void_p, c.c_uint32, c.c_void_p)
Physical = c.CFUNCTYPE(c.c_uint64, c.c_void_p)


class CryptoOps(c.Structure):
    _fields_ = [("smc", Smc), ("flush", Cache), ("invalidate", Cache),
                ("sha256", CryptoHash), ("physical", Physical),
                ("cache_alignment", c.c_size_t)]


class Crypto(c.Structure):
    _fields_ = [("ops", c.POINTER(CryptoOps)), ("page", c.c_void_p),
                ("state", c.c_int), ("secure_error", c.c_uint64)]


class Component(c.Structure):
    _fields_ = [("image", c.c_void_p), ("size", c.c_size_t),
                ("capacity", c.c_size_t), ("cert1", c.c_void_p),
                ("cert1_size", c.c_size_t), ("cert2", c.c_void_p),
                ("cert2_size", c.c_size_t)]


prepare = library.tetris_scp_prepare_component
prepare.argtypes = [c.POINTER(Crypto), c.c_void_p, c.POINTER(CryptoOps),
                    c.POINTER(Ops), c.POINTER(Component), c.c_void_p]
prepare.restype = c.c_int


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


class PrepareComponentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.SecurityTest.setUpClass()
        cls.fixture = fixtures.SecurityTest()

    def setUp(self):
        self.parts = self.fixture.parts()
        self.plain = b"P" * len(self.parts[0])
        self.parts[2] = self.fixture.cert(self.fixture.key, self.fixture.key, [
            ("2.1", fixtures.univ.BitString.fromOctetString(
                hashlib.sha256(self.parts[0]).digest())),
            ("2.8", fixtures.univ.BitString.fromOctetString(b"w" * 32)),
            ("4.2", fixtures.univ.BitString.fromOctetString(
                hashlib.sha256(self.plain).digest()))])
        self.storage = c.create_string_buffer(8192)
        self.page = (c.addressof(self.storage) + 4095) & ~4095
        self.image_storage = c.create_string_buffer(1024)
        self.image = (c.addressof(self.image_storage) + 63) & ~63
        self.image_pa = 0x50000000
        self.events = []
        self.smc_error = 0
        self.init_error = 0
        self.corrupt_plain = False

        @Smc
        def smc(function, arg1, arg2):
            self.events.append(("smc", function, arg1, arg2))
            if function == 0xc2000133:
                c.memmove(self.image, b"X" * len(self.plain) if self.corrupt_plain
                          else self.plain, len(self.plain))
                return self.smc_error
            return self.init_error

        @Cache
        def flush(address, size):
            self.events.append(("flush", address, size))

        @Cache
        def invalidate(address, size):
            self.events.append(("invalidate", address, size))

        @CryptoHash
        def sha(data, size, out):
            digest(data, size, out)

        @Physical
        def physical(address):
            return 0x48401000 if address == self.page else self.image_pa

        self.crypto_ops = CryptoOps(smc, flush, invalidate, sha, physical, 64)
        self.context = Crypto()

    def call(self, pin=None, capacity=512, offset=0):
        cert1 = c.create_string_buffer(self.parts[1])
        cert2 = c.create_string_buffer(self.parts[2])
        c.memmove(self.image + offset, self.parts[0], len(self.parts[0]))
        component = Component(self.image + offset, len(self.parts[0]), capacity,
                              c.addressof(cert1), len(self.parts[1]),
                              c.addressof(cert2), len(self.parts[2]))
        return prepare(c.byref(self.context), self.page, c.byref(self.crypto_ops),
                       c.byref(ops), c.byref(component),
                       bytes.fromhex(self.fixture.root_pin) if pin is None else pin)

    def smcs(self):
        return [event[1] for event in self.events if event[0] == "smc"]

    def test_authenticated_prepare_and_reuse(self):
        self.assertEqual(self.call(), 0)
        self.assertEqual(self.context.state, 1)
        self.assertEqual(c.string_at(self.image, len(self.plain)), self.plain)
        self.assertEqual(self.smcs(), [0xc200010b, 0xc2000133])
        self.assertEqual(c.string_at(self.page + 0x40, 32), bytes(32))
        self.assertEqual(c.string_at(self.page + 0x100, 32), bytes(32))
        self.assertEqual(self.call(), 0)
        self.assertEqual(self.smcs(), [0xc200010b, 0xc2000133, 0xc2000133])

    def test_bad_pin_never_calls_secure_monitor(self):
        self.assertNotEqual(self.call(pin=bytes(32)), 0)
        self.assertEqual(self.events, [])
        self.assertEqual(self.context.state, 2)
        self.assertEqual(c.string_at(self.image, len(self.parts[0])), self.parts[0])

    def test_bad_signature_never_calls_secure_monitor(self):
        self.parts[2] = self.parts[2][:-1] + bytes([self.parts[2][-1] ^ 1])
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(self.events, [])

    def test_invalid_memory_never_calls_secure_monitor(self):
        for pa, align, capacity, offset in (
                (0x50000000, 64, 16, 0), (0x50000000, 64, 512, 1),
                (0x48401000, 64, 512, 0), (0x100000000, 64, 512, 0),
                (0xffffffc0, 64, 512, 0), (0x50000000, 0, 512, 0),
                (0x50000000, 3, 512, 0)):
            with self.subTest(pa=pa, align=align, capacity=capacity, offset=offset):
                self.context = Crypto()
                self.image_pa = pa
                self.crypto_ops.cache_alignment = align
                self.assertNotEqual(self.call(capacity=capacity, offset=offset), 0)
                self.assertEqual(self.events, [])

    def test_secure_init_failure_cannot_retry(self):
        self.init_error = 1
        self.assertNotEqual(self.call(), 0)
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(self.smcs(), [0xc200010b])
        self.assertEqual(self.context.state, 2)

    def test_secure_decrypt_failure_erases_image_and_cannot_retry(self):
        self.smc_error = 1
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(c.string_at(self.image, 320), bytes(320))
        self.assertEqual(self.context.state, 2)
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(self.smcs(), [0xc200010b, 0xc2000133])

    def test_wrong_plaintext_erases_image(self):
        self.corrupt_plain = True
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(c.string_at(self.image, 320), bytes(320))
        self.assertEqual(self.context.state, 2)


if __name__ == "__main__":
    unittest.main()
