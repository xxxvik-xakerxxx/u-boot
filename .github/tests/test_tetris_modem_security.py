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
storage_native = None
stage_native = None
place_native = None
load_native = None
sync_native = None
prepare_native = None
place_b41_native = None
load_b41_native = None
initialize_smem_native = None
service_banks_native = None
Flush = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_ulong, c.c_ulong)


class CacheOps(c.Structure):
    _fields_ = [("flush", Flush), ("ctx", c.c_void_p)]

ReadBlocks = c.CFUNCTYPE(c.c_ulonglong, c.c_void_p, c.c_ulonglong,
                        c.c_ulonglong, c.c_void_p)
Acquire = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_size_t, c.POINTER(c.c_void_p))
Release = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p, c.c_size_t)


class StagingOps(c.Structure):
    _fields_ = [("acquire", Acquire), ("release", Release), ("ctx", c.c_void_p)]


class Storage(c.Structure):
    _fields_ = [(name, c.c_ulonglong) for name in ("device_blocks", "start", "blocks")]
    _fields_ += [("block_size", c.c_uint), ("read", ReadBlocks), ("ctx", c.c_void_p)]


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


class SmemInputs(c.Structure):
    _fields_ = [(name, c.c_uint) for name in (
        "drdi_version", "udc_en", "consys_size", "nv_cache_size", "ccb_gear")]


class SmemEntry(c.Structure):
    _fields_ = [(name, c.c_uint) for name in ("id", "offset", "size", "flags")]


class SmemPlan(c.Structure):
    _fields_ = [("nc", SmemEntry * 18), ("cache", SmemEntry * 5)]
    _fields_ += [(name, c.c_uint) for name in (
        "nc_capacity", "cache_capacity", "nc_rows", "cache_rows")]


class BootPlan(c.Structure):
    _fields_ = [("layout", Layout), ("inputs", SmemInputs), ("smem", SmemPlan)]


class ServiceBanks(c.Structure):
    _fields_ = [(name, c.c_size_t) for name in
                ("firmware_capacity", "nc_offset", "nc_capacity",
                 "cache_offset", "cache_capacity")]


class PreparedBundle(c.Structure):
    _fields_ = [("bundle", Bundle), ("inputs", SmemInputs), ("smem", SmemPlan)]


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
    prepare_native = library.tetris_modem_prepare_bundle_b41
    prepare_native.argtypes = [c.c_void_p, c.c_size_t, c.c_void_p,
                               c.POINTER(Ops), c.c_size_t, c.c_uint,
                               c.POINTER(PreparedBundle)]
    prepare_native.restype = c.c_int
    storage_native = library.tetris_modem_read_bundle
    storage_native.argtypes = [c.POINTER(Storage), c.c_void_p, c.c_size_t,
                               c.c_void_p, c.POINTER(Ops), c.c_size_t,
                               c.POINTER(Bundle)]
    storage_native.restype = c.c_int
    stage_native = library.tetris_modem_stage_bundle
    stage_native.argtypes = [c.POINTER(Storage), c.POINTER(StagingOps),
                             c.c_void_p, c.POINTER(Ops), c.c_size_t,
                             c.POINTER(Layout)]
    stage_native.restype = c.c_int
    place_native = library.tetris_modem_place_bundle
    place_native.argtypes = [c.c_void_p, c.c_size_t, c.c_void_p,
                             c.POINTER(Ops), c.c_void_p, c.c_size_t,
                             c.POINTER(Layout)]
    place_native.restype = c.c_int
    load_native = library.tetris_modem_load_bundle
    load_native.argtypes = [c.POINTER(Storage), c.POINTER(StagingOps),
                            c.c_void_p, c.POINTER(Ops), c.c_void_p,
                            c.c_size_t, c.POINTER(Layout)]
    load_native.restype = c.c_int
    place_b41_native = library.tetris_modem_place_bundle_b41
    place_b41_native.argtypes = [c.c_void_p, c.c_size_t, c.c_void_p,
                                c.POINTER(Ops), c.c_void_p, c.c_size_t,
                                c.c_uint, c.POINTER(BootPlan)]
    place_b41_native.restype = c.c_int
    load_b41_native = library.tetris_modem_load_bundle_b41
    load_b41_native.argtypes = [c.POINTER(Storage), c.POINTER(StagingOps),
                               c.c_void_p, c.POINTER(Ops), c.c_void_p,
                               c.c_size_t, c.c_uint, c.POINTER(BootPlan)]
    load_b41_native.restype = c.c_int
    sync_native = library.tetris_modem_sync_payloads
    sync_native.argtypes = [c.c_void_p, c.c_size_t, c.POINTER(Layout),
                            c.c_size_t, c.POINTER(CacheOps)]
    sync_native.restype = c.c_int
    initialize_smem_native = library.tetris_modem_initialize_smem_b41
    initialize_smem_native.argtypes = [c.POINTER(BootPlan), c.c_void_p, c.c_size_t,
                                      c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t,
                                      c.c_size_t, c.POINTER(CacheOps)]
    initialize_smem_native.restype = c.c_int
    service_banks_native = library.tetris_modem_plan_service_banks_b41
    service_banks_native.argtypes = [c.POINTER(BootPlan), c.c_size_t,
                                    c.POINTER(ServiceBanks)]
    service_banks_native.restype = c.c_int


class ModemSecurityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.SecurityTest.setUpClass()
        cls.fixture = fixtures.SecurityTest()

    def smem_fixture(self, consys=65):
        data = b"".join(self.signed_groups(smem_fields={0x180: consys, 0x18c: 64}))
        prepared = PreparedBundle()
        self.assertEqual(prepare_native(data, len(data), bytes.fromhex(self.fixture.root_pin),
                                        c.byref(ops), 0x4000, 3, c.byref(prepared)), 0)
        plan = BootPlan(prepared.bundle.layout, prepared.inputs, prepared.smem)

        def aligned(size):
            backing = c.create_string_buffer(size + 128)
            address = (c.addressof(backing) + 127) & ~63
            c.memset(c.addressof(backing), 0xa5, c.sizeof(backing))
            return backing, address, size

        banks = [aligned(0x4000), aligned(plan.smem.nc_capacity),
                 aligned(plan.smem.cache_capacity)]
        return plan, banks

    @unittest.skipUnless(service_banks_native, "physical service-bank planning executes in CI")
    def test_service_banks_fit_one_owned_window(self):
        plan, _ = self.smem_fixture(0xd80000)
        plan.layout.memory_size = 0x3ffffff
        banks = ServiceBanks()
        self.assertEqual(service_banks_native(c.byref(plan), 512 << 20, c.byref(banks)), 0)
        self.assertEqual(banks.firmware_capacity, 0x4000000)
        self.assertEqual(banks.nc_offset, banks.firmware_capacity)
        self.assertEqual(banks.nc_capacity, plan.smem.nc_capacity)
        self.assertEqual(banks.cache_offset, banks.nc_offset + banks.nc_capacity)
        self.assertEqual(banks.cache_capacity, plan.smem.cache_capacity)
        end = banks.cache_offset + banks.cache_capacity
        self.assertEqual(service_banks_native(c.byref(plan), end, c.byref(banks)), 0)
        before = bytes(banks)
        for capacity in (0, 0xffff, banks.firmware_capacity - 1, end - 1):
            self.assertLess(service_banks_native(c.byref(plan), capacity, c.byref(banks)), 0)
            self.assertEqual(bytes(banks), before)
        self.assertLess(service_banks_native(None, 512 << 20, c.byref(banks)), 0)
        self.assertLess(service_banks_native(c.byref(plan), 512 << 20, None), 0)
        plan.smem.nc_capacity += 0x10000
        self.assertLess(service_banks_native(c.byref(plan), 512 << 20, c.byref(banks)), 0)
        self.assertEqual(bytes(banks), before)

    @unittest.skipUnless(service_banks_native, "physical service-bank planning executes in CI")
    def test_real_sized_firmware_requires_separate_services(self):
        data = b"".join(self.signed_groups(smem_fields={0x180: 0xd80000,
                                                       0x18c: 0x163780}))
        prepared = PreparedBundle()
        self.assertEqual(prepare_native(data, len(data), bytes.fromhex(self.fixture.root_pin),
                                        c.byref(ops), 0x4000, 1, c.byref(prepared)), 0)
        plan = BootPlan(prepared.bundle.layout, prepared.inputs, prepared.smem)
        plan.layout.memory_size = 480 << 20
        banks = ServiceBanks()
        before = bytes(banks)
        self.assertLess(service_banks_native(c.byref(plan), 512 << 20, c.byref(banks)), 0)
        self.assertEqual(bytes(banks), before)
        self.assertEqual(service_banks_native(c.byref(plan), c.c_size_t(-1).value,
                                             c.byref(banks)), 0)
        self.assertEqual(banks.firmware_capacity, 480 << 20)
        self.assertEqual(banks.nc_capacity, plan.smem.nc_capacity)
        self.assertEqual(banks.cache_capacity, plan.smem.cache_capacity)
        self.assertGreater(banks.nc_capacity + banks.cache_capacity, 32 << 20)
        self.assertEqual((banks.nc_capacity + banks.cache_capacity) % 0x10000, 0)

    @unittest.skipUnless(initialize_smem_native, "service RAM initialization executes in CI")
    def test_smem_initialize_preserves_consys_and_firmware(self):
        for consys in (0, 65, 0xd80000):
            plan, banks = self.smem_fixture(consys)
            calls = []
            cleared = []

            @Flush
            def flush(ctx, start, end):
                calls.append((start, end))
                cleared.append(c.string_at(start, end - start) == bytes(end - start))
                return 0

            cache_ops = CacheOps(flush, None)
            firmware, nc, cache = banks
            before_plan = bytes(plan)
            self.assertEqual(initialize_smem_native(c.byref(plan), firmware[1], firmware[2],
                                                    nc[1], nc[2], cache[1], cache[2], 64,
                                                    c.byref(cache_ops)), 0)
            preserve = (consys + 63) & ~63
            self.assertEqual(c.string_at(firmware[1], firmware[2]), b"\xa5" * firmware[2])
            self.assertEqual(c.string_at(cache[1], preserve), b"\xa5" * preserve)
            self.assertEqual(calls, [(nc[1], nc[1] + nc[2]),
                                     (cache[1] + preserve, cache[1] + cache[2])])
            self.assertEqual(cleared, [True, True])
            for _, address, size in banks:
                self.assertEqual(c.string_at(address - 1, 1), b"\xa5")
                self.assertEqual(c.string_at(address + size, 1), b"\xa5")
            self.assertEqual(bytes(plan), before_plan)

    @unittest.skipUnless(initialize_smem_native, "service RAM initialization executes in CI")
    def test_smem_invalid_input_never_writes(self):
        plan, banks = self.smem_fixture()
        firmware, nc, cache = banks
        calls = []

        @Flush
        def flush(ctx, start, end):
            calls.append((start, end))
            return 0

        cache_ops = CacheOps(flush, None)
        arguments = [c.byref(plan), firmware[1], firmware[2], nc[1], nc[2],
                     cache[1], cache[2], 64, c.byref(cache_ops)]
        cases = [(0, None), (1, None), (3, None), (5, None), (8, None),
                 (3, nc[1] + 1), (4, nc[2] - 64), (6, cache[2] - 64),
                 (7, 0), (7, 3), (3, firmware[1]), (5, nc[1]),
                 (0, c.cast(nc[1], c.POINTER(BootPlan))),
                 (8, c.cast(cache[1], c.POINTER(CacheOps))),
                 (3, c.c_void_p(-64)), (2, 1)]
        for index, value in cases:
            changed = arguments.copy()
            changed[index] = value
            self.assertLess(initialize_smem_native(*changed), 0, (index, value))
        original = bytes(plan)
        for mutation in ("entry", "rows", "capacity", "policy"):
            c.memmove(c.byref(plan), original, len(original))
            if mutation == "entry":
                plan.smem.cache[0].size = 0
            elif mutation == "rows":
                plan.smem.nc_rows += 1
            elif mutation == "capacity":
                plan.smem.cache_capacity += 64
            else:
                plan.inputs.ccb_gear = 99
            self.assertLess(initialize_smem_native(*arguments), 0, mutation)
        self.assertEqual(calls, [])
        for _, address, size in banks:
            self.assertEqual(c.string_at(address, size), b"\xa5" * size)

    @unittest.skipUnless(initialize_smem_native, "service RAM initialization executes in CI")
    def test_smem_flush_failure_stops_without_ready_state(self):
        for failed, error in ((0, -5), (1, -6), (0, 1)):
            plan, banks = self.smem_fixture()
            calls = []

            @Flush
            def flush(ctx, start, end):
                calls.append((start, end))
                return error if len(calls) - 1 == failed else 0

            cache_ops = CacheOps(flush, None)
            firmware, nc, cache = banks
            before = bytes(plan)
            self.assertEqual(initialize_smem_native(c.byref(plan), firmware[1], firmware[2],
                                                    nc[1], nc[2], cache[1], cache[2], 64,
                                                    c.byref(cache_ops)), error if error < 0 else -5)
            self.assertEqual(len(calls), failed + 1)
            self.assertEqual(bytes(plan), before)

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

    def signed_groups(self, bad_layout=False, smem_fields=None):
        rom = bytearray(1024)
        rom[512:524] = b"CHECK_HEADER"
        for offset, value in ((12, 6), (16, 2), (20, 14), (168, 1),
                              (172, 0x4000), (176, 0x2000), (184, 0x3000),
                              (188, 0x1000), (192, 2), (196, 0), (200, 0x3000),
                              (204, 0x3000), (208, 0x1000), (0x190, 3), (508, 512)):
            struct.pack_into("<I", rom, 512 + offset, value)
        for offset, value in (smem_fields or {}).items():
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

    @unittest.skipUnless(prepare_native, "authenticated service plan executes in CI")
    def test_authenticated_service_plan(self):
        # Actual stock field values, synthetic firmware and test signatures.
        fields = {0x180: 0xd80000, 0x184: 0, 0x18c: 0x16a040, 0x190: 3}
        data = b"".join(self.signed_groups(smem_fields=fields))
        pin = bytes.fromhex(self.fixture.root_pin)
        out = PreparedBundle()
        self.assertEqual(prepare_native(data, len(data), pin, c.byref(ops),
                                        0x4000, 1, c.byref(out)), 0)
        self.assertEqual((out.inputs.drdi_version, out.inputs.udc_en,
                          out.inputs.consys_size, out.inputs.nv_cache_size,
                          out.inputs.ccb_gear), (3, 0, 0xd80000, 0x16a040, 1))
        self.assertEqual(out.smem.cache[1].size, fields[0x18c])
        self.assertEqual(out.smem.cache[2].offset, 0xf00000)
        self.assertEqual(out.smem.cache_rows, 6)
        self.assertEqual(out.smem.cache_capacity, 0x2560000)
        self.assertEqual(out.bundle.layout.rom_size, 1024)

        changed = bytearray(data)
        changed[out.bundle.members[0].payload_offset + 512 + 0x180] ^= 1
        invalid = [bytes(changed)]
        invalid += [b"".join(self.signed_groups(smem_fields={0x184: 2})),
                    b"".join(self.signed_groups(smem_fields={0x180: 0xffffffff})),
                    b"".join(self.signed_groups(bad_layout=True))]
        for value, gear, root in [(v, 1, pin) for v in invalid] + [
                (data, 16, pin), (data, 1, bytes(32))]:
            c.memset(c.byref(out), 0xa5, c.sizeof(out))
            before = bytes(out)
            self.assertLess(prepare_native(value, len(value), root, c.byref(ops),
                                           0x4000, gear, c.byref(out)), 0)
            self.assertEqual(bytes(out), before)
        backing = c.create_string_buffer(data)
        self.assertLess(prepare_native(backing, len(data), pin, c.byref(ops),
                                       0x4000, 1, c.cast(backing, c.POINTER(PreparedBundle))), 0)
        self.assertEqual(backing.raw[:-1], data)

    @unittest.skipUnless(sync_native, "native cache synchronization executes only in CI")
    def test_cache_ranges_and_first_failure(self):
        # Synthetic addresses; the callback never accesses hardware or memory.
        base = 0x100000000
        layout = Layout(0x4000, 0x2000, 1009, 0x3011, 0xfef, 255, 2)
        for alignment in (64, 128, 256, 4096):
            mask = alignment - 1
            expected = [(base, base + ((1009 + mask) & ~mask)),
                        (base + (0x3011 & ~mask),
                         base + ((0x3011 + 255 + mask) & ~mask))]
            for failure in (0, 1, 2):
                calls = []

                @Flush
                def flush(ctx, start, end):
                    calls.append((start, end))
                    return -5 if len(calls) == failure else 0

                cache = CacheOps(flush, None)
                before = bytes(layout)
                ret = sync_native(base, 0x4000, c.byref(layout), alignment,
                                  c.byref(cache))
                self.assertEqual(ret, -5 if failure else 0)
                self.assertEqual(calls, expected[:failure] if failure else expected)
                self.assertEqual(bytes(layout), before)

    @unittest.skipUnless(sync_native, "native cache synchronization executes only in CI")
    def test_invalid_cache_ranges_have_no_effect(self):
        calls = []

        @Flush
        def flush(ctx, start, end):
            calls.append((start, end))
            return 0

        cache = CacheOps(flush, None)
        base = 0x100000000
        cases = [(base, 0x4000, 0, {}), (base, 0x4000, 96, {}),
                 (base + 1, 0x4000, 64, {}), (base, 0x3fff, 64, {}),
                 (c.c_ulong(-64).value, 0x4000, 64, {}),
                 (base, 0x4000, 64, {"memory_size": 0x4001}),
                 (base, 0x4000, 64, {"rom_size": 0}),
                 (base, 0x4000, 64, {"dsp_size": 0}),
                 (base, 0x4000, 64, {"dsp_offset": 1}),
                 (base, 0x4000, 64, {"dsp_capacity": 0x1001}),
                 (base, 0x4000, 64, {"dsp_size": 0x1001})]
        for address, capacity, alignment, changes in cases:
            layout = Layout(0x4000, 0x2000, 1024, 0x3000, 0x1000, 256, 2)
            for name, value in changes.items():
                setattr(layout, name, value)
            self.assertNotEqual(sync_native(address, capacity, c.byref(layout),
                                            alignment, c.byref(cache)), 0)
            self.assertEqual(calls, [])

    @unittest.skipUnless(place_native, "native payload placement executes only in CI")
    def test_place_authenticated_payloads(self):
        groups = self.signed_groups()
        # Exercise different group order, with guards outside the destination.
        for order in ((0, 1, 2), (2, 0, 1), (1, 2, 0)):
            data = b"".join(groups[i] for i in order)
            arena = c.create_string_buffer(b"\xa5" * (0x5000 + 32))
            out = Layout()
            ret = place_native(data, len(data), bytes.fromhex(self.fixture.root_pin),
                               c.byref(ops), c.addressof(arena) + 16, 0x5000,
                               c.byref(out))
            self.assertEqual(ret, 0)
            expected = bytearray(b"\xa5" * (0x5000 + 32))
            expected[16:16 + 1024] = groups[0][512:512 + 1024]
            expected[16 + 0x3000:16 + 0x3100] = groups[2][512:768]
            self.assertEqual(arena.raw[:-1], bytes(expected))
            self.assertEqual((out.rom_size, out.dsp_offset, out.dsp_size),
                             (1024, 0x3000, 256))

    @unittest.skipUnless(place_native, "native payload placement executes only in CI")
    def test_place_failures_never_write(self):
        data = b"".join(self.signed_groups())
        pin = bytes.fromhex(self.fixture.root_pin)
        cases = [(data, bytes(32), 0x4000), (data[:-1], pin, 0x4000),
                 (data, pin, 0x3fff),
                 (b"".join(self.signed_groups(True)), pin, 0x4000)]
        groups = self.signed_groups()
        offset = 0
        for group in groups:
            damaged = bytearray(b"".join(groups))
            damaged[offset + 512] ^= 1
            cases.append((bytes(damaged), pin, 0x4000))
            offset += len(group)
        for value, root, capacity in cases:
            target = c.create_string_buffer(b"\xa5" * 0x4000)
            out = Layout()
            c.memset(c.byref(out), 0xa5, c.sizeof(out))
            before = target.raw, bytes(out)
            ret = place_native(value, len(value), root, c.byref(ops), target,
                               capacity, c.byref(out))
            self.assertNotEqual(ret, 0)
            self.assertEqual((target.raw, bytes(out)), before)

    @unittest.skipUnless(place_native, "native payload placement executes only in CI")
    def test_place_rejects_aliases_and_wrapping_spans(self):
        data = b"".join(self.signed_groups())
        source = c.create_string_buffer(data + bytes(0x8000))
        target = c.create_string_buffer(b"\xa5" * 0x4000)
        out = Layout()
        src, dst, output = c.addressof(source), c.addressof(target), c.addressof(out)
        maximum = c.c_size_t(-1).value
        cases = [(src, len(data), src, 0x4000, output),
                 (src, len(data), src + len(data) - 1, 0x4000, output),
                 (src + 16, len(data), src, 0x4000, output),
                 (src, len(data), dst, 0x4000, src + 16),
                 (src, len(data), dst, 0x4000, dst + 16),
                 (maximum - 15, len(data), dst, 0x4000, output),
                 (src, len(data), maximum - 15, 0x4000, output),
                 (src, len(data), dst, 0x4000, maximum - 15),
                 (src, len(data), 0, 0x4000, output),
                 (src, len(data), dst, 0, output)]
        before = source.raw, target.raw, bytes(out)
        for start, size, dest, capacity, result in cases:
            ret = place_native(start, size, bytes.fromhex(self.fixture.root_pin),
                               c.byref(ops), dest, capacity,
                               c.cast(result, c.POINTER(Layout)))
            self.assertNotEqual(ret, 0)
            self.assertEqual((source.raw, target.raw, bytes(out)), before)

    def storage_call(self, data, block_size=512, fail_at=None, changes=None,
                     capacity=None, wrong_pin=False, staging=None, load=False,
                     b41=False, gear=1):
        data += bytes((-len(data)) % block_size)
        calls = []
        violations = []
        events = []
        start = 17

        @ReadBlocks
        def read(ctx, block, count, output):
            events.append("read")
            calls.append((block, count))
            offset = (block - start) * block_size
            length = count * block_size
            if ctx != 123 or block < start or offset + length > len(data):
                violations.append((ctx, block, count))
                return 0
            if len(calls) == fail_at:
                return count - 1
            c.memmove(output, data[offset:offset + length], length)
            return count

        storage = Storage(start + len(data) // block_size, start,
                          len(data) // block_size, block_size, read, 123)
        for name, value in (changes or {}).items():
            setattr(storage, name, value)
        buffer = c.create_string_buffer(len(data))
        target = c.create_string_buffer(b"\xa5" * (0x4000 + 32))
        target_before = target.raw
        target_at_release = []
        out = BootPlan() if b41 else (Bundle() if staging is None else Layout())
        c.memset(c.byref(out), 0xa5, c.sizeof(out))
        before = bytes(out)
        pin = bytes(32) if wrong_pin else bytes.fromhex(self.fixture.root_pin)
        if staging is None:
            ret = storage_native(c.byref(storage), buffer,
                                 len(data) if capacity is None else capacity,
                                 pin, c.byref(ops), 0x4000, c.byref(out))
        else:
            @Acquire
            def acquire(ctx, size, output):
                events.append("acquire")
                if ctx != 456 or size != len(data):
                    violations.append("invalid allocation request")
                    return -1
                if staging.get("acquire_error"):
                    return -12
                output[0] = None if staging.get("null_buffer") else c.addressof(buffer)
                return 0

            @Release
            def release(ctx, address, size):
                events.append("release")
                expected = None if staging.get("null_buffer") else c.addressof(buffer)
                if ctx != 456 or address != expected or size != len(data):
                    violations.append("invalid release")
                target_at_release.append(target.raw)
                c.memset(buffer, 0xdd, len(data))  # Invalidate the entire snapshot.
                return -16 if staging.get("release_error") else 0

            memory = StagingOps(acquire, release, 456)
            if b41:
                ret = load_b41_native(c.byref(storage), c.byref(memory), pin,
                                      c.byref(ops), c.addressof(target) + 16,
                                      0x4000, gear, c.byref(out))
            elif load:
                ret = load_native(c.byref(storage), c.byref(memory), pin,
                                  c.byref(ops), c.addressof(target) + 16,
                                  0x4000, c.byref(out))
            else:
                ret = stage_native(c.byref(storage), c.byref(memory), pin,
                                   c.byref(ops), 0x4000, c.byref(out))
            if changes:
                self.assertEqual(events, [], "invalid geometry must not allocate")
            if events:
                self.assertEqual(events[0], "acquire")
                self.assertEqual(events.count("acquire"), 1)
                if staging.get("acquire_error"):
                    self.assertEqual(events, ["acquire"])
                else:
                    self.assertEqual(events[-1], "release")
                    self.assertEqual(events.count("release"), 1)
        self.assertFalse(violations, "reader escaped the partition")
        if load:
            # A cleanup error is after placement; all earlier failures are before it.
            if ret in (0, -16) and not wrong_pin and fail_at is None:
                expected = bytearray(target_before)
                expected[16:1040] = data[512:1536]
                dsp = data.index(b"md1dsp") - 8 + 512
                expected[16 + 0x3000:16 + 0x3100] = data[dsp:dsp + 256]
                self.assertEqual(target.raw, bytes(expected))
                self.assertEqual(target_at_release, [bytes(expected)],
                                 "placement must precede snapshot release")
            else:
                self.assertEqual(target.raw, target_before)
        if ret:
            self.assertEqual(bytes(out), before)
        elif staging is None:
            self.assertEqual(buffer.raw, data)
        else:
            layout = out.layout if b41 else out
            self.assertEqual(layout.rom_size, 1024)
            self.assertEqual(layout.dsp_offset, 0x3000)
            if b41:
                self.assertEqual(out.inputs.drdi_version, 3)
                self.assertEqual(out.inputs.ccb_gear, gear)
                self.assertEqual(out.inputs.consys_size, 0xd80000)
                self.assertEqual(out.inputs.nv_cache_size, 0x16a040)
                self.assertEqual(out.smem.cache_rows, 6)
                self.assertEqual(out.smem.cache_capacity, 0x2560000)
        return ret, calls

    @unittest.skipUnless(load_b41_native, "integrated B4.1 loader executes only in CI")
    def test_b41_load_signed_metadata_before_placement(self):
        fields = {0x180: 0xd80000, 0x18c: 0x16a040}
        data = b"".join(self.signed_groups(smem_fields=fields))
        data += bytes(3 * 65536 - len(data))
        for block_size in (512, 4096):
            self.assertEqual(self.storage_call(data, block_size, staging={},
                                              load=True, b41=True)[0], 0)
            for failure in (1, 2, 3):
                self.assertLess(self.storage_call(data, block_size, fail_at=failure,
                                                 staging={}, load=True, b41=True)[0], 0)
        for settings in ({"acquire_error": True}, {"release_error": True},
                         {"null_buffer": True}):
            self.assertLess(self.storage_call(data, staging=settings,
                                             load=True, b41=True)[0], 0)
        self.assertLess(self.storage_call(data, staging={}, load=True,
                                         b41=True, wrong_pin=True)[0], 0)
        self.assertLess(self.storage_call(data, staging={}, load=True,
                                         b41=True, gear=16)[0], 0)
        for invalid in ({0x184: 2}, {0x180: 0xffffffff}, {0x190: 2}):
            bad = b"".join(self.signed_groups(smem_fields=invalid))
            self.assertLess(self.storage_call(bad, staging={}, load=True, b41=True)[0], 0)
        changed = bytearray(data)
        changed[1024 + 0x180] ^= 1
        self.assertLess(self.storage_call(bytes(changed), staging={},
                                         load=True, b41=True)[0], 0)
        for geometry in ({"blocks": 0}, {"start": 99999}, {"block_size": 1024}):
            self.assertLess(self.storage_call(data, changes=geometry, staging={},
                                             load=True, b41=True)[0], 0)
        first = self.storage_call(data, fail_at=1, staging={}, load=True, b41=True)[0]
        both = self.storage_call(data, fail_at=1, staging={"release_error": True},
                                 load=True, b41=True)[0]
        self.assertEqual(first, both)

    @unittest.skipUnless(place_b41_native, "B4.1 placement executes only in CI")
    def test_b41_placement_rejects_aliases_and_wrapping(self):
        data = b"".join(self.signed_groups())
        source = c.create_string_buffer(data)
        target = c.create_string_buffer(b"\xa5" * 0x4000)
        out = BootPlan()
        src, dst, output = c.addressof(source), c.addressof(target), c.addressof(out)
        maximum = c.c_size_t(-1).value
        before = source.raw, target.raw, bytes(out)
        cases = [(src, len(data), src, 0x4000, output),
                 (src, len(data), dst, 0x4000, src + 16),
                 (src, len(data), dst, 0x4000, dst + 16),
                 (maximum - 15, len(data), dst, 0x4000, output),
                 (src, len(data), maximum - 15, 0x4000, output),
                 (src, len(data), dst, 0x4000, maximum - 15),
                 (src, len(data), 0, 0x4000, output)]
        for start, size, dest, capacity, result in cases:
            self.assertLess(place_b41_native(start, size,
                            bytes.fromhex(self.fixture.root_pin), c.byref(ops),
                            dest, capacity, 1, c.cast(result, c.POINTER(BootPlan))), 0)
            self.assertEqual((source.raw, target.raw, bytes(out)), before)

    @unittest.skipUnless(load_native, "native loading executes only in CI")
    def test_load_snapshot_lifetime(self):
        data = b"".join(self.signed_groups())
        data += bytes(3 * 65536 - len(data))
        for block_size in (512, 4096):
            self.assertEqual(self.storage_call(data, block_size, staging={}, load=True)[0], 0)
            for failure in (1, 2, 3):
                ret, calls = self.storage_call(data, block_size, fail_at=failure,
                                               staging={}, load=True)
                self.assertNotEqual(ret, 0)
                self.assertEqual(len(calls), failure)
        for settings in ({"acquire_error": True}, {"null_buffer": True},
                         {"release_error": True}):
            self.assertNotEqual(self.storage_call(data, staging=settings, load=True)[0], 0)
        self.assertNotEqual(self.storage_call(data, staging={}, load=True, wrong_pin=True)[0], 0)
        first = self.storage_call(data, fail_at=1, staging={}, load=True)[0]
        both = self.storage_call(data, fail_at=1, staging={"release_error": True}, load=True)[0]
        self.assertEqual(first, both)
        for changes in ({"blocks": 0}, {"block_size": 0}, {"device_blocks": 17}):
            ret, calls = self.storage_call(data, changes=changes, staging={}, load=True)
            self.assertNotEqual(ret, 0)
            self.assertEqual(calls, [])

    @unittest.skipUnless(load_native, "native loading executes only in CI")
    def test_load_rejects_aliases_before_storage_read(self):
        self.check_load_aliases()

    @unittest.skipUnless(load_b41_native, "integrated B4.1 loader executes only in CI")
    def test_b41_load_rejects_aliases_before_storage_read(self):
        self.check_load_aliases(b41=True)

    def check_load_aliases(self, b41=False):
        arena = c.create_string_buffer(b"\xa5" * 0x8000)
        output = BootPlan() if b41 else Layout()
        base = c.addressof(arena)
        for alias in (base, base + 0x3fff, c.addressof(output), 0,
                      c.c_size_t(-1).value - 15):
            events = []

            @ReadBlocks
            def read(ctx, block, count, buffer):
                events.append("read")
                return 0

            @Acquire
            def acquire(ctx, size, buffer):
                events.append("acquire")
                buffer[0] = alias
                return 0

            @Release
            def release(ctx, buffer, size):
                events.append("release")
                return 0

            storage = Storage(32, 1, 4, 512, read, None)
            memory = StagingOps(acquire, release, None)
            before = arena.raw, bytes(output)
            args = [c.byref(storage), c.byref(memory),
                    bytes.fromhex(self.fixture.root_pin), c.byref(ops), base, 0x4000]
            if b41:
                args.append(1)
            args.append(c.byref(output))
            ret = (load_b41_native if b41 else load_native)(*args)
            self.assertNotEqual(ret, 0)
            self.assertEqual(events, ["acquire", "release"])
            self.assertEqual((arena.raw, bytes(output)), before)

    @unittest.skipUnless(stage_native, "native staging executes only in CI")
    def test_staging_lifetime_and_cleanup(self):
        data = b"".join(self.signed_groups())
        data += bytes(3 * 65536 - len(data))
        for block_size in (512, 4096):
            self.assertEqual(self.storage_call(data, block_size, staging={})[0], 0)
            for failure in (1, 2, 3):
                ret, calls = self.storage_call(data, block_size, fail_at=failure, staging={})
                self.assertNotEqual(ret, 0)
                self.assertEqual(len(calls), failure)
        for staging in ({"acquire_error": True}, {"release_error": True},
                        {"null_buffer": True}):
            ret, calls = self.storage_call(data, staging=staging)
            self.assertNotEqual(ret, 0)
            if not staging.get("release_error"):
                self.assertEqual(calls, [])
        self.assertNotEqual(self.storage_call(data, wrong_pin=True, staging={})[0], 0)
        for changes in ({"blocks": 0}, {"block_size": 0},
                        {"device_blocks": 17}):
            ret, calls = self.storage_call(data, changes=changes, staging={})
            self.assertNotEqual(ret, 0)
            self.assertEqual(calls, [])
        first = self.storage_call(data, fail_at=1, staging={})[0]
        both = self.storage_call(data, fail_at=1, staging={"release_error": True})[0]
        self.assertEqual(first, both, "preserve the first I/O failure")

    @unittest.skipUnless(storage_native, "native storage executes only in CI")
    def test_storage_snapshot_then_authentication(self):
        data = b"".join(self.signed_groups())
        data += bytes(3 * 65536 - len(data))
        for block_size in (512, 4096):
            ret, calls = self.storage_call(data, block_size)
            self.assertEqual(ret, 0)
            self.assertEqual(calls, [(17 + i * (65536 // block_size),
                                     65536 // block_size) for i in range(3)])
            for failure in (1, 2, 3):
                ret, calls = self.storage_call(data, block_size, fail_at=failure)
                self.assertNotEqual(ret, 0)
                self.assertEqual(len(calls), failure, "short read must not retry")
        self.assertNotEqual(self.storage_call(data, wrong_pin=True)[0], 0)
        damaged = bytearray(data)
        damaged[512] ^= 1
        self.assertNotEqual(self.storage_call(bytes(damaged))[0], 0)

    @unittest.skipUnless(storage_native, "native storage executes only in CI")
    def test_storage_bounds_before_any_read(self):
        data = b"".join(self.signed_groups())
        for changes in ({"block_size": 0}, {"block_size": 1024},
                        {"blocks": 0}, {"blocks": (256 * 1024 * 1024 // 512) + 1},
                        {"start": 2**64 - 1}, {"device_blocks": 17},
                        {"start": 2**64 - 16, "blocks": 32,
                         "device_blocks": 2**64 - 1}):
            ret, calls = self.storage_call(data, changes=changes)
            self.assertNotEqual(ret, 0)
            self.assertEqual(calls, [])
        ret, calls = self.storage_call(data, capacity=len(data) - 1)
        self.assertNotEqual(ret, 0)
        self.assertEqual(calls, [])

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
