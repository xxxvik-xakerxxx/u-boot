#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""CI-only production C tests, synthetic signatures and mock SMC; no hardware."""
import ctypes as c
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

# Reuse existing real RSA verification, hash callbacks and crypto ABI types.
spec = importlib.util.spec_from_file_location(
    "scp_runtime", Path(__file__).with_name("test_tetris_scp_security_c.py"))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
lib = base.library


class Section(c.Structure):
    _fields_ = [("header", c.c_size_t), ("payload", c.c_size_t), ("size", c.c_size_t)]


class Layout(c.Structure):
    _fields_ = [("sections", Section * 6)]


class Attempt(c.Structure):
    _fields_ = [("state", c.c_int)]


class Plain(c.Structure):
    _fields_ = [("bytes", c.c_size_t), ("format", c.c_uint),
                ("covers_stock_copy", c.c_uint), ("gzip_isize_hint", c.c_uint)]


class Segment(c.Structure):
    _fields_ = [(name, c.c_uint) for name in (
        "file_offset", "file_bytes", "memory_offset", "memory_bytes", "flags",
        "pt_id", "pt_alignment")]


class Segments(c.Structure):
    _fields_ = [(name, c.c_uint) for name in (
        "format", "count", "memory_span", "entry_offset", "trailer_bytes")]
    _fields_ += [("segments", Segment * 16)]


class Report(c.Structure):
    _fields_ = [("plain", Plain), ("segments", Segments), ("digest", c.c_ubyte * 32)]


layout = lib.tetris_gpueb_parse_layout
layout.argtypes = [c.c_void_p, c.c_size_t, c.POINTER(Layout)]
prepare = lib.tetris_gpueb_transform_only
prepare.argtypes = [c.POINTER(Attempt), c.POINTER(base.Crypto), c.POINTER(base.Ops),
                   c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t,
                   c.c_void_p, c.POINTER(Report)]


def aligned(size):
    raw = c.create_string_buffer(size + 63)
    address = (c.addressof(raw) + 63) & ~63
    return raw, address


class GPUEB(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base.fixtures.SecurityTest.setUpClass()
        cls.fixture = base.fixtures.SecurityTest()

    def image(self, profile=0x10000, bad_header=False, zero_wrapped=False, plaintext=None):
        self.ciphertext = b"C" * 128
        self.plaintext = b"P" * 128 if plaintext is None else plaintext
        self.assertEqual(len(self.plaintext), 128)
        root_spki = base.fixtures.scp.der(self.fixture.key.public_key().public_bytes(
            base.fixtures.serialization.Encoding.DER,
            base.fixtures.serialization.PublicFormat.SubjectPublicKeyInfo))
        cert1 = self.fixture.cert(self.fixture.root, self.fixture.root, [("1.2", root_spki)])
        names = ("tinysys-gpueb-RV33_A", "cert1", "cert2",
                 "tinysys-gpueb-RV33_A_xfile", "cert1", "cert2")

        def header(index, size):
            data = bytearray(512)
            struct.pack_into("<II", data, 0, 0x58881688, size)
            data[8:40] = names[index].encode().ljust(32, b"\0")
            struct.pack_into("<8I", data, 48, 0x58891689, 512, 1,
                             (0, 0x02000000, 0x02000002)[index % 3], 0, 16, 0,
                             0x22345678 if index % 3 else 0)
            return bytes(data)

        bits = base.fixtures.univ.BitString.fromOctetString
        fields = [("2.1", bits(hashlib.sha256(self.ciphertext).digest())),
                  ("2.4", bits(bytes(32) if bad_header else hashlib.sha256(header(0, 128)).digest())),
                  ("2.8", bits(bytes(32) if zero_wrapped else b"W" * 32)),
                  ("4.2", bits(hashlib.sha256(self.plaintext).digest())),
                  ("2.9", base.fixtures.univ.Integer(profile))]
        cert2 = self.fixture.cert(self.fixture.key, self.fixture.key, fields)
        result = bytearray()
        for i, part in enumerate((self.ciphertext, cert1, cert2, b"auxiliary", cert1, cert2)):
            result += header(i, len(part)) + part
            result += bytes((-len(result)) % 16)
        return bytes(result)

    def setup(self, image, failure=None):
        self.raw = image
        self.container = c.create_string_buffer(image, len(image))
        self.staging_raw, self.staging = aligned(256)
        self.page_raw, self.page = aligned(4096)
        self.calls = []
        self.flushes = []
        self.attempt = Attempt(0)
        self.report = Report()
        c.memset(c.byref(self.report), 0xa5, c.sizeof(self.report))
        c.memset(self.staging, 0xcc, 256)

        @base.Smc
        def smc(function, arg1, arg2):
            self.calls.append((function, arg1, arg2))
            if function != 0xc2000133 or arg1 != 1 or arg2 != 0:
                return 1
            output = b"X" * 128 if failure == "digest" else self.plaintext
            c.memmove(self.staging, output, len(output))
            return 9 if failure == "secure" else 0

        @base.Cache
        def flush(pointer, size):
            self.flushes.append((pointer, size))

        @base.Cache
        def invalidate(pointer, size):
            pass

        @base.CryptoHash
        def digest(pointer, size, output):
            c.memmove(output, hashlib.sha256(c.string_at(pointer, size)).digest(), 32)

        @base.Physical
        def physical(pointer):
            if pointer == self.page:
                return 0x48401000
            if self.staging <= pointer < self.staging + 256:
                return 0x60000000 + pointer - self.staging
            return 0

        self.ops = base.CryptoOps(smc, flush, invalidate, digest, physical, 64)
        self.crypto = base.Crypto(c.pointer(self.ops), self.page, 1, 0)
        self.pin = bytes.fromhex(self.fixture.root_pin)

    def call(self):
        return prepare(c.byref(self.attempt), c.byref(self.crypto), c.byref(base.ops),
                       self.container, len(self.raw), self.staging, 256,
                       self.pin, c.byref(self.report))

    def test_success_no_init_and_no_plaintext_retained(self):
        self.setup(self.image())
        self.assertEqual(self.call(), 0)
        self.assertEqual(self.calls, [(0xc2000133, 1, 0)])
        self.assertEqual(self.attempt.state, 2)
        self.assertEqual(self.crypto.state, 1)
        self.assertEqual(self.report.plain.bytes, 128)
        self.assertFalse(self.report.plain.covers_stock_copy)
        self.assertEqual(self.report.segments.format, 0)
        self.assertEqual(self.report.segments.count, 0)
        self.assertEqual(bytes(self.report.digest), hashlib.sha256(self.plaintext).digest())
        self.assertEqual(c.string_at(self.staging, 256), bytes(256))
        self.assertEqual(c.string_at(self.page + 0x40, 32), bytes(32))
        self.assertEqual(c.string_at(self.page + 0x100, 32), bytes(32))
        self.assertEqual(self.container.raw, self.raw)
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(len(self.calls), 1)

    def test_authenticated_pt_metadata_and_malformed_known_format(self):
        plaintext = bytearray(128)
        struct.pack_into("<6I", plaintext, 0, 0x58901690, 24, 104, 4, 99, 0)
        self.setup(self.image(plaintext=bytes(plaintext)))
        self.assertEqual(self.call(), 0)
        self.assertEqual(self.report.segments.format, 3)
        self.assertEqual(self.report.segments.count, 1)
        self.assertEqual(self.report.segments.memory_span, 0)
        segment = self.report.segments.segments[0]
        self.assertEqual((segment.file_offset, segment.file_bytes, segment.pt_id),
                         (24, 104, 99))
        self.assertEqual(c.string_at(self.staging, 256), bytes(256))
        struct.pack_into("<I", plaintext, 12, 3)
        self.setup(self.image(plaintext=bytes(plaintext)))
        before = bytes(self.report)
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(bytes(self.report), before)
        self.assertEqual(c.string_at(self.staging, 256), bytes(256))
        self.assertEqual(self.crypto.state, 1)
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(len(self.calls), 1)

    def test_signed_profile_header_and_material_fail_before_smc(self):
        for options in ({"profile": 0}, {"profile": 0x110000},
                        {"bad_header": True}, {"zero_wrapped": True}):
            self.setup(self.image(**options))
            before = bytes(self.report)
            self.assertNotEqual(self.call(), 0)
            self.assertEqual(self.calls, [])
            self.assertEqual(bytes(self.report), before)
            self.assertEqual(c.string_at(self.staging, 256), bytes(256))
            self.assertNotEqual(self.call(), 0)

    def test_root_signature_header_and_ciphertext_fail_without_poisoning_scp(self):
        original = self.image()
        parsed = Layout()
        self.assertEqual(layout(original, len(original), c.byref(parsed)), 0)
        signature_end = parsed.sections[2].payload + parsed.sections[2].size - 1
        for offset in (40, 512, signature_end):
            changed = bytearray(original)
            changed[offset] ^= 1
            self.setup(bytes(changed))
            self.assertNotEqual(self.call(), 0)
            self.assertEqual(self.calls, [])
            self.assertEqual(self.crypto.state, 1)
            self.assertEqual(c.string_at(self.staging, 256), bytes(256))
        self.setup(original)
        self.pin = bytes(32)
        self.assertNotEqual(self.call(), 0)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.crypto.state, 1)

    def test_secure_partial_write_and_wrong_digest_fail_without_retry(self):
        for failure in ("secure", "digest"):
            self.setup(self.image(), failure)
            before = bytes(self.report)
            self.assertNotEqual(self.call(), 0)
            self.assertEqual(self.crypto.state, 2)
            self.assertEqual(c.string_at(self.staging, 256), bytes(256))
            self.assertEqual(bytes(self.report), before)
            self.assertNotEqual(self.call(), 0)
            self.assertEqual(len(self.calls), 1)

    def test_container_alias_is_rejected_without_erasure(self):
        self.setup(self.image())
        ret = prepare(c.byref(self.attempt), c.byref(self.crypto), c.byref(base.ops),
                      self.container, len(self.raw), self.container, 256,
                      self.pin, c.byref(self.report))
        self.assertNotEqual(ret, 0)
        self.assertEqual(self.container.raw, self.raw)
        self.assertEqual(self.calls, [])

    def test_framing_faults_leave_layout_atomic_and_never_call_smc(self):
        original = self.image()
        for offset in (0, 4, 8, 48, 52, 56, 60, 68, 72, 76):
            changed = bytearray(original)
            changed[offset] ^= 255
            output = Layout()
            c.memset(c.byref(output), 0xa5, c.sizeof(output))
            before = bytes(output)
            self.assertNotEqual(layout(bytes(changed), len(changed), c.byref(output)), 0)
            self.assertEqual(bytes(output), before)
            self.setup(bytes(changed))
            self.assertNotEqual(self.call(), 0)
            self.assertEqual(self.calls, [])
            self.assertEqual(c.string_at(self.staging, 256), bytes(256))


if __name__ == "__main__":
    unittest.main()
