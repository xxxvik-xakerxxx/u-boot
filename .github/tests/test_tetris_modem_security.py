#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Shared synthetic checks for the Python reference and native C verifier."""
import ctypes as c
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

import test_tetris_scp_security as fixtures
import tetris_modem_security as modem

native = None
bundle_native = None


class Member(c.Structure):
    _fields_ = [(name, c.c_size_t) for name in
                ("header_offset", "payload_offset", "payload_size")]


class Layout(c.Structure):
    _fields_ = [(name, c.c_uint) for name in (
        "memory_size", "logical_image_size", "rom_size", "dsp_offset",
        "dsp_capacity", "dsp_size", "region_count")]


class Bundle(c.Structure):
    _fields_ = [("members", Member * 3), ("consumed", c.c_size_t),
                ("layout", Layout)]


if len(sys.argv) > 1 and sys.argv[1].endswith(".so"):
    library = c.CDLL(sys.argv.pop(1))
    Hash = c.CFUNCTYPE(None, c.c_void_p, c.c_size_t, c.c_void_p)
    Verify = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_size_t,
                        c.c_void_p, c.c_size_t, c.c_void_p)

    @Hash
    def digest(data, size, out):
        c.memmove(out, hashlib.sha256(c.string_at(data, size)).digest(), 32)

    @Verify
    def signature(key, key_size, tbs, tbs_size, sig):
        try:
            public = fixtures.serialization.load_der_public_key(c.string_at(key, key_size))
            public.verify(c.string_at(sig, 256), c.string_at(tbs, tbs_size),
                          fixtures.padding.PSS(mgf=fixtures.padding.MGF1(fixtures.hashes.SHA256()),
                                               salt_length=32), fixtures.hashes.SHA256())
            return 0
        except Exception:
            return -1

    class Ops(c.Structure):
        _fields_ = [("sha256", Hash), ("verify", Verify)]

    ops = Ops(digest, signature)
    native = library.tetris_modem_verify_signature
    native.argtypes = [c.c_void_p, c.c_size_t] * 4 + [c.c_void_p, c.POINTER(Ops)]
    native.restype = c.c_int
    bundle_native = library.tetris_modem_authenticate_bundle
    bundle_native.argtypes = [c.c_void_p, c.c_size_t, c.c_void_p,
                              c.POINTER(Ops), c.c_size_t, c.POINTER(Bundle)]
    bundle_native.restype = c.c_int


class ModemSecurityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.SecurityTest.setUpClass()
        cls.fixture = fixtures.SecurityTest()

    def parts(self, bad_marker=None, omit_header=False):
        payload, root, _ = self.fixture.parts()[:3]
        header = bytes(512)
        bits = fixtures.univ.BitString.fromOctetString
        fields = [("2.1", bits(hashlib.sha256(payload).digest())),
                  ("2.4", bits(hashlib.sha256(header).digest())),
                  *[(suffix, bits(b"\1" if bad_marker == suffix else b"\0"))
                    for suffix in ("2.6", "2.8", "4.2")]]
        if omit_header:
            fields.pop(1)
        leaf = self.fixture.cert(self.fixture.key, self.fixture.key, fields)
        return [root, leaf, header, payload]

    def accepted(self, parts, pin=None):
        pin = bytes.fromhex(self.fixture.root_pin) if pin is None else pin
        if native:
            args = [item for value in parts for item in (value, len(value))]
            return native(*args, pin, c.byref(ops)) == 0
        try:
            modem.verify(*parts, pin)
            return True
        except Exception:
            return False

    def test_valid(self):
        self.assertTrue(self.accepted(self.parts()))

    def test_corruption(self):
        for index in range(4):
            parts = self.parts()
            parts[index] = parts[index][:-1] + bytes([parts[index][-1] ^ 1])
            self.assertFalse(self.accepted(parts), index)

    def test_wrong_pin_and_profile(self):
        self.assertFalse(self.accepted(self.parts(), bytes(32)))
        for suffix in ("2.6", "2.8", "4.2"):
            self.assertFalse(self.accepted(self.parts(bad_marker=suffix)))
        self.assertFalse(self.accepted(self.parts(omit_header=True)))

    def test_truncation(self):
        original = self.parts()
        for index in range(4):
            for length in (0, 1, len(original[index]) - 1):
                parts = original.copy()
                parts[index] = parts[index][:length]
                self.assertFalse(self.accepted(parts), (index, length))

    def test_scp_profile_rejected(self):
        payload, root, leaf = self.fixture.parts()[:3]
        self.assertFalse(self.accepted([root, leaf, bytes(512), payload]))

    @staticmethod
    def member_header(name, size):
        header = bytearray(512)
        struct.pack_into("<II", header, 0, 0x58881688, size)
        header[8:8 + len(name)] = name.encode()
        struct.pack_into("<II", header, 48, 0x58891689, 512)
        struct.pack_into("<I", header, 68, 16)
        return bytes(header)

    def section(self, name, value):
        return self.member_header(name, len(value)) + value + bytes((-len(value)) % 16)

    def signed_groups(self, bad_layout=False):
        rom = bytearray(1024)
        rom[512:524] = b"CHECK_HEADER"
        for offset, value in ((12, 6), (16, 2), (20, 14), (168, 1),
                              (172, 0x4000), (176, 0x2000), (184, 0x3000),
                              (188, 0x1000), (192, 2), (196, 0), (200, 0x3000),
                              (204, 0x3000), (208, 0x1000), (0x190, 3), (508, 512)):
            struct.pack_into("<I", rom, 512 + offset, value)
        if bad_layout:
            struct.pack_into("<I", rom, 512 + 184, 16)  # Signed ROM/DSP overlap.
        root = self.parts()[0]
        bits = fixtures.univ.BitString.fromOctetString
        groups = []
        for name, payload in zip(modem.NAMES, (bytes(rom), bytes(32), bytes(256))):
            header = self.member_header(name, len(payload))
            fields = [("2.1", bits(hashlib.sha256(payload).digest())),
                      ("2.4", bits(hashlib.sha256(header).digest())),
                      *[(suffix, bits(b"\0")) for suffix in ("2.6", "2.8", "4.2")]]
            leaf = self.fixture.cert(self.fixture.key, self.fixture.key, fields)
            groups.append(header + payload +
                          self.section("cert1md" if name == "md1rom" else "cert1", root) +
                          self.section("cert2", leaf))
        return groups

    def bundle_call(self, data, capacity=0x4000, pin=None, size=None):
        out = Bundle()
        c.memset(c.byref(out), 0xa5, c.sizeof(out))
        before = bytes(out)
        pin = bytes.fromhex(self.fixture.root_pin) if pin is None else pin
        ret = bundle_native(data, len(data) if size is None else size, pin,
                            c.byref(ops), capacity, c.byref(out))
        if ret:
            self.assertEqual(bytes(out), before, "failure published a partial bundle")
        return ret, out

    def test_bundle_reference_signatures(self):
        for parts in modem.components(b"".join(self.signed_groups())).values():
            self.assertTrue(self.accepted(parts))

    @unittest.skipUnless(bundle_native, "native bundle executes only in CI")
    def test_bundle_signed_groups_and_layout(self):
        groups = self.signed_groups()
        for order in ((0, 1, 2), (2, 0, 1)):
            data = b"".join(groups[index] for index in order)
            ret, out = self.bundle_call(data + b"unexamined partition tail")
            self.assertEqual(ret, 0)
            self.assertEqual(out.consumed, len(data))
            self.assertEqual(out.layout.dsp_offset, 0x3000)
            self.assertEqual(out.layout.rom_size, 1024)
            for index, member in enumerate(out.members):
                self.assertEqual(data[member.header_offset + 8:].split(b"\0", 1)[0],
                                 modem.NAMES[index].encode())
                self.assertEqual(member.payload_offset, member.header_offset + 512)
                self.assertLessEqual(member.payload_offset + member.payload_size, len(data))
            for parts in modem.components(data).values():
                self.assertTrue(self.accepted(parts))

    @unittest.skipUnless(bundle_native, "native bundle executes only in CI")
    def test_bundle_authentication_and_atomic_failures(self):
        groups = self.signed_groups()
        data = b"".join(groups)
        for index in range(3):
            for field in (80, 512):  # Signed header and payload of every component.
                damaged = bytearray(data)
                damaged[sum(map(len, groups[:index])) + field] ^= 1
                self.assertNotEqual(self.bundle_call(bytes(damaged))[0], 0)
        self.assertNotEqual(self.bundle_call(data, pin=bytes(32))[0], 0)
        self.assertNotEqual(self.bundle_call(data, capacity=0x3fff)[0], 0)
        self.assertNotEqual(self.bundle_call(b"".join(self.signed_groups(True)))[0], 0)
        for length in (0, 511, 512, len(groups[0]), len(data) - 1):
            self.assertNotEqual(self.bundle_call(data[:length])[0], 0)
        for index in range(3):
            malformed = groups[index] + groups[index] + b"".join(
                groups[j] for j in range(3) if j != index)
            self.assertNotEqual(self.bundle_call(malformed)[0], 0)

    @unittest.skipUnless(bundle_native, "native bundle executes only in CI")
    def test_bundle_parser_budget_and_adjacency(self):
        groups = self.signed_groups()
        data = b"".join(groups)
        for offset in (0, 4, 48, 52, 68, 512 + 1024 + 8):
            damaged = bytearray(data)
            damaged[offset] ^= 0xff
            self.assertNotEqual(self.bundle_call(bytes(damaged))[0], 0)
        damaged = bytearray(data)
        damaged[8:40] = b"x" * 32
        self.assertNotEqual(self.bundle_call(bytes(damaged))[0], 0)
        skipped = self.section("unrelated", bytes(16))
        self.assertEqual(self.bundle_call(skipped * 119 + data)[0], 0)
        self.assertNotEqual(self.bundle_call(skipped * 120 + data)[0], 0)
        self.assertNotEqual(self.bundle_call(data, size=modem.MAX_CONTAINER + 1)[0], 0)

    def test_member_pairing_and_bounds(self):
        root, leaf, _, payload = self.parts()

        def section(name, value):
            header = bytearray(512)
            struct.pack_into("<II", header, 0, 0x58881688, len(value))
            header[8:8 + len(name)] = name.encode()
            struct.pack_into("<II", header, 48, 0x58891689, 512)
            struct.pack_into("<I", header, 68, 16)
            return bytes(header) + value + bytes((-len(value)) % 16)

        data = b"".join(section(name, payload) +
                        section("cert1md" if name == "md1rom" else "cert1", root) +
                        section("cert2", leaf) for name in modem.NAMES)
        self.assertEqual(tuple(modem.components(data)), modem.NAMES)
        for end in (0, 511, 512, len(data) - 1):
            with self.assertRaises(ValueError):
                modem.components(data[:end])
        for offset in (0, 4, 48, 52, 68, 512 + len(payload) + 8):
            damaged = bytearray(data)
            damaged[offset] ^= 0xff
            with self.assertRaises((ValueError, UnicodeError)):
                modem.components(damaged)

    @unittest.skipUnless(native, "native ABI bounds tested only in CI")
    def test_native_invalid_arguments(self):
        parts = self.parts()
        pin = bytes.fromhex(self.fixture.root_pin)
        args = [item for value in parts for item in (value, len(value))]
        for index in (0, 2, 4, 6):
            bad = args.copy()
            bad[index] = None
            self.assertNotEqual(native(*bad, pin, c.byref(ops)), 0)
        bad = args.copy()
        bad[7] = modem.MAX_PAYLOAD + 16
        self.assertNotEqual(native(*bad, pin, c.byref(ops)), 0)
        self.assertNotEqual(native(*args, None, c.byref(ops)), 0)
        self.assertNotEqual(native(*args, pin, None), 0)

    @unittest.skipUnless(os.environ.get("MODEM_TEST_IMAGE"), "no local vendor fixture")
    def test_real_consistency_only(self):
        with Path(os.environ["MODEM_TEST_IMAGE"]).open("rb") as stream:
            data = stream.read(modem.MAX_CONTAINER + 1)
        for parts in modem.components(data).values():
            root, _ = fixtures.scp.certificate(parts[0])
            # Image-derived pin for offline equivalence, never device trust.
            pin = hashlib.sha256(fixtures.encoder.encode(root[0][6])).digest()
            self.assertTrue(self.accepted(parts, pin))
        if bundle_native:
            self.assertEqual(self.bundle_call(data, capacity=512 * 1024 * 1024,
                                              pin=pin)[0], 0)


if __name__ == "__main__":
    unittest.main()
