#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""CI-only execution of the native relative modem layout planner."""
import ctypes as c
import errno
import os
from pathlib import Path
import struct
import sys
import unittest


class Layout(c.Structure):
    _fields_ = [(name, c.c_uint) for name in (
        "memory_size", "logical_image_size", "rom_size", "dsp_offset",
        "dsp_capacity", "dsp_size", "region_count")]


library = c.CDLL(sys.argv.pop(1))
plan = library.tetris_modem_plan_layout
plan.argtypes = [c.c_void_p, c.c_size_t, c.c_size_t, c.c_size_t, c.POINTER(Layout)]
plan.restype = c.c_int


class Block(c.Structure):
    _fields_ = [(name, c.c_uint) for name in ("offset", "size", "info", "attributes")]
    _fields_.append(("physical", c.c_ulonglong))


class MemoryMap(c.Structure):
    _fields_ = [("count", c.c_uint), ("blocks", Block * 32)]


memory = library.tetris_modem_plan_memory
memory.argtypes = [c.c_void_p, c.c_size_t, c.c_size_t, c.c_ulonglong,
                   c.c_size_t, c.POINTER(MemoryMap)]
memory.restype = c.c_int

encode_memory = library.tetris_modem_encode_memory
encode_memory.argtypes = [c.POINTER(MemoryMap), c.c_ulonglong, c.c_size_t,
                          c.c_void_p, c.c_size_t]
encode_memory.restype = c.c_int


class Tag(c.Structure):
    _fields_ = [("name", c.c_char * 64), ("data", c.c_void_p), ("size", c.c_size_t)]


encode_tags = library.tetris_modem_encode_tags
encode_tags.argtypes = [c.POINTER(Tag), c.c_size_t, c.c_void_p, c.c_size_t]
encode_tags.restype = c.c_int


class TagEncodingTest(unittest.TestCase):
    def test_exact_wire_and_guards(self):
        first, second = c.create_string_buffer(b"abc"), c.create_string_buffer(b"12345")
        tags = (Tag * 2)(Tag(b"one", c.addressof(first), 3),
                         Tag(b"two", c.addressof(second), 5))
        output = c.create_string_buffer(b"\xa5" * 200, 200)
        self.assertEqual(encode_tags(tags, 2, c.byref(output, 1), 198), 160)
        expected = (struct.pack("<64sIII", b"one", 152, 3, 76) +
                    struct.pack("<64sIII", b"two", 155, 5, 0) + b"abc12345")
        self.assertEqual(output.raw, b"\xa5" + expected + b"\xa5" * 39)

    def test_atomic_rejections(self):
        data = c.create_string_buffer(b"abc")
        for name, pointer, length, count, capacity, error in (
                (b"", c.addressof(data), 3, 1, 128, errno.EINVAL),
                (b"x" * 64, c.addressof(data), 3, 1, 128, errno.EINVAL),
                (b"x", None, 3, 1, 128, errno.EINVAL),
                (b"x", c.addressof(data), 0, 1, 128, errno.EINVAL),
                (b"x", c.addressof(data), 3, 0, 128, errno.EINVAL),
                (b"x", c.addressof(data), 3, 129, 128, errno.EINVAL),
                (b"x", c.addressof(data), 3, 1, 78, errno.ENOSPC),
                (b"x", c.addressof(data), 65536, 1, 128, errno.E2BIG),
                (b"x", c.addressof(data), c.c_size_t(-1).value, 1, 128, errno.E2BIG)):
            output = c.create_string_buffer(b"\xa5" * 128, 128)
            tag = Tag(name, pointer, length)
            self.assertEqual(encode_tags(c.byref(tag), count, output, capacity), -error)
            self.assertEqual(output.raw, b"\xa5" * 128)

    def test_duplicates_and_aliases(self):
        data = c.create_string_buffer(b"abc")
        tags = (Tag * 2)(*[Tag(b"same", c.addressof(data), 3)] * 2)
        output = c.create_string_buffer(b"\xa5" * 256, 256)
        self.assertEqual(encode_tags(tags, 2, output, 256), -errno.EEXIST)
        tags[1].name = b"other"
        tags[1].data = c.addressof(output) + 155
        self.assertEqual(encode_tags(tags, 2, output, 256), -errno.EINVAL)
        self.assertEqual(output.raw, b"\xa5" * 256)
        before = bytes(tags)
        self.assertEqual(encode_tags(tags, 1, tags, 256), -errno.EINVAL)
        self.assertEqual(bytes(tags), before)
        tag = Tag(b"wrap", c.c_size_t(-2).value, 3)
        self.assertEqual(encode_tags(c.byref(tag), 1, output, 256), -errno.EINVAL)
        tag.data = c.addressof(data)
        self.assertEqual(encode_tags(c.byref(tag), 1, c.c_size_t(-2).value, 256),
                         -errno.EINVAL)

    def test_limits(self):
        payload = c.create_string_buffer(b"z" * (65536 - 76))
        tag = Tag(b"n" * 63, c.addressof(payload), 65536 - 76)
        output = c.create_string_buffer(65536)
        self.assertEqual(encode_tags(c.byref(tag), 1, output, 65536), 65536)
        self.assertEqual(output.raw[63], 0)
        self.assertEqual(output.raw[76:], payload.raw[:-1])
        tags = (Tag * 128)(*[Tag(str(i).encode(), c.addressof(payload), 1)
                             for i in range(128)])
        self.assertEqual(encode_tags(tags, 128, output, 65536), 128 * 77)
        self.assertEqual(struct.unpack_from("<I", output.raw, 127 * 76 + 72)[0], 0)


class Remap(c.Structure):
    _fields_ = [("value", c.c_uint * 6), ("mask", c.c_uint * 6)]


class EmiRange(c.Structure):
    _fields_ = [(name, c.c_ulonglong) for name in (
        "start_page", "end_page", "start_readback", "end_readback")]
    _fields_.append(("slot", c.c_uint))


emi = library.tetris_modem_plan_emi
emi.argtypes = [c.c_ulonglong, c.c_ulonglong, c.c_uint, c.POINTER(EmiRange)]
emi.restype = c.c_int


class EmiTest(unittest.TestCase):
    def call(self, start=0x80000000, size=0x200000, slot=32):
        out = EmiRange()
        c.memset(c.byref(out), 0xa5, c.sizeof(out))
        before = bytes(out)
        ret = emi(start, size, slot, c.byref(out))
        if ret:
            self.assertEqual(bytes(out), before)
        return ret, out

    def test_modem_slots_and_raw_readbacks(self):
        for slot in range(32, 44):
            for start in (0x40000000, 0x80000000, 0x120000000, 0x83fffe000):
                ret, out = self.call(start, 4096, slot)
                self.assertEqual(ret, 0)
                self.assertEqual(out.start_page, start >> 12)
                self.assertEqual(out.end_page, (start + 4096) >> 12)
                self.assertEqual(out.start_readback, start)
                self.assertEqual(out.end_readback, (start + 4096) | (1 << 43))
                self.assertEqual(out.slot, slot)

    def test_reject_truncation_and_bad_slots_before_programming(self):
        for kwargs in (dict(start=0), dict(start=0x3ffff000),
                       dict(start=0x80000001), dict(size=0), dict(size=4095),
                       dict(start=0x840000000), dict(start=0x83ffff000, size=4096),
                       dict(start=0x10080000000), dict(size=2**64 - 4096),
                       dict(slot=0), dict(slot=31), dict(slot=44), dict(slot=64)):
            with self.subTest(**kwargs):
                self.assertNotEqual(self.call(**kwargs)[0], 0)
        self.assertNotEqual(emi(0x80000000, 4096, 32, None), 0)


remap = library.tetris_modem_plan_remap
remap.argtypes = [c.c_ulonglong] * 4 + [c.POINTER(Remap)]
remap.restype = c.c_int


class RemapTest(unittest.TestCase):
    def call(self, base=0x80000000, capacity=0x20000000,
             dram_base=0x40000000, dram_size=0x100000000):
        out = Remap()
        c.memset(c.byref(out), 0xa5, c.sizeof(out))
        before = bytes(out)
        ret = remap(base, capacity, dram_base, dram_size, c.byref(out))
        if ret:
            self.assertEqual(bytes(out), before)
        return ret, out

    def test_all_representable_windows(self):
        for page in range(1, 1024 - 15):
            ret, out = self.call(base=page << 25, dram_base=0, dram_size=1 << 35)
            self.assertEqual(ret, 0)
            self.assertEqual(list(out.mask), [0x3fffffff] * 5 + [0x3ff])
            for index in range(16):
                decoded = (out.value[index // 3] >> (index % 3 * 10)) & 0x3ff
                self.assertEqual(decoded << 25, (page + index) << 25)
            for value, mask in zip(out.value, out.mask):
                self.assertEqual(value & ~mask, 0)

    def test_whole_window_and_reservation_must_fit(self):
        cases = (
            dict(base=0), dict(base=0x80000001), dict(base=0x81000000),
            dict(capacity=0), dict(capacity=0x1e000000),
            dict(base=(1 << 35) - 0x1e000000, dram_base=0, dram_size=1 << 36),
            dict(base=1 << 35, dram_base=0, dram_size=1 << 36),
            dict(base=0x3e000000), dict(base=0x140000000),
            dict(base=0x122000000), dict(dram_size=0),
            dict(dram_base=(1 << 64) - 16, dram_size=32),
            dict(capacity=(1 << 64) - 1), dict(capacity=0xc0000001),
        )
        for kwargs in cases:
            with self.subTest(**kwargs):
                self.assertNotEqual(self.call(**kwargs)[0], 0)
        self.assertEqual(self.call(base=0x120000000)[0], 0)
        self.assertEqual(self.call(base=0x40000000)[0], 0)
        self.assertEqual(self.call(capacity=0xc0000000)[0], 0)
        self.assertNotEqual(remap(0x80000000, 0x20000000,
                                  0x40000000, 0x100000000, None), 0)


class LayoutTest(unittest.TestCase):
    def fixture(self):
        rom = bytearray(1024)
        header = bytearray(512)
        header[:12] = b"CHECK_HEADER"
        for offset, value in ((12, 6), (16, 2), (20, 14), (168, 1),
                              (172, 0x4000), (176, 0x2000), (184, 0x3000),
                              (188, 0x1000), (192, 2), (196, 0), (200, 0x3000),
                              (204, 0x3000), (208, 0x1000), (0x190, 3), (508, 512)):
            struct.pack_into("<I", header, offset, value)
        rom[-512:] = header
        return rom

    def call(self, rom=None, dsp=256, capacity=0x4000, size=None):
        rom = self.fixture() if rom is None else rom
        out = Layout()
        c.memset(c.byref(out), 0xa5, c.sizeof(out))
        before = bytes(out)
        data = bytes(rom)
        result = plan(data, len(data) if size is None else size,
                      dsp, capacity, c.byref(out))
        if result:
            self.assertEqual(bytes(out), before, "failure modified the plan")
        return result, out

    def test_valid_and_logical_size(self):
        result, out = self.call()
        self.assertEqual(result, 0)
        self.assertEqual([getattr(out, name) for name, _ in Layout._fields_],
                         [0x4000, 0x2000, 1024, 0x3000, 0x1000, 256, 2])
        self.assertGreater(out.logical_image_size, out.rom_size)
        self.assertEqual(self.call(capacity=0x8000)[0], 0)
        self.assertEqual(self.call(dsp=0x1000)[0], 0)

    def test_invalid_fields(self):
        for offset, values in {
                0: [0], 12: [0, 5, 7], 16: [0, 1], 20: [0, 13, 15],
                168: [0, 2], 172: [0, 1023, 0x4001], 176: [0, 0x4001],
                184: [0, 512, 0x3001, 0xffffffff], 188: [0, 255, 0x1001, 0xffffffff],
                192: [0, 9, 0xffffffff], 196: [0xffffffff], 200: [0, 0x4001],
                204: [0, 0x2fff, 0xffffffff], 208: [0, 0x1001, 0xffffffff],
                0x190: [0, 1, 2, 4], 508: [0, 188, 344, 511, 513]}.items():
            for value in values:
                with self.subTest(offset=offset, value=value):
                    rom = self.fixture()
                    struct.pack_into("<I", rom, len(rom) - 512 + offset, value)
                    self.assertNotEqual(self.call(rom)[0], 0)

    def test_input_bounds(self):
        for size in (0, 1, 511, 513, 64 * 1024 * 1024 + 16):
            self.assertNotEqual(self.call(size=size)[0], 0)
        for dsp in (0, 1, 255, 0x1010, 64 * 1024 * 1024 + 16):
            self.assertNotEqual(self.call(dsp=dsp)[0], 0)
        for capacity in (0, 0x3fff):
            self.assertNotEqual(self.call(capacity=capacity)[0], 0)
        out = Layout()
        self.assertNotEqual(plan(None, 1024, 256, 0x4000, c.byref(out)), 0)
        self.assertNotEqual(plan(bytes(self.fixture()), 1024, 256, 0x4000, None), 0)

    @unittest.skipUnless(os.environ.get("MODEM_TEST_IMAGE"), "no local vendor fixture")
    def test_real_container(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
        import tetris_modem_security as modem
        with Path(os.environ["MODEM_TEST_IMAGE"]).open("rb") as stream:
            parts = modem.components(stream.read(modem.MAX_CONTAINER + 1))
        result, out = self.call(parts["md1rom"][3], len(parts["md1dsp"][3]),
                                0x20000000)
        self.assertEqual(result, 0)
        self.assertEqual(out.memory_size, 0x1e000000)
        self.assertEqual(out.dsp_offset, 0x1d780000)
        self.assertEqual(out.dsp_capacity, 0x880000)
        self.assertEqual(out.region_count, 4)
        mapped = MemoryMap()
        rom = parts["md1rom"][3]
        self.assertEqual(memory(rom, len(rom), len(parts["md1dsp"][3]),
                                0x100000000, 0x20000000, c.byref(mapped)), 0)
        self.assertEqual(mapped.count, 12)
        self.assertEqual(sum(b.size for b in mapped.blocks[:mapped.count]), 0x20000000)
        self.assertEqual(mapped.blocks[8].attributes, 0x41)
        self.assertEqual(mapped.blocks[10].offset, 0x1d780000)


class MemoryMapTest(unittest.TestCase):
    def fixture(self):
        rom = LayoutTest().fixture()
        struct.pack_into("<II", rom, len(rom) - 512 + 0x11c, 0x1000, 0x1000)
        return rom

    def call(self, rom=None, base=0x50000000, capacity=0x8000):
        rom = self.fixture() if rom is None else rom
        result = MemoryMap()
        c.memset(c.byref(result), 0xa5, c.sizeof(result))
        before = bytes(result)
        data = bytes(rom)
        ret = memory(data, len(data), 256, base, capacity, c.byref(result))
        if ret:
            self.assertEqual(bytes(result), before)
        else:
            self.assertLessEqual(result.count, 32)
            offset = 0
            for block in result.blocks[:result.count]:
                self.assertEqual(block.offset, offset)
                self.assertGreater(block.size, 0)
                self.assertEqual(block.physical, base + offset)
                offset += block.size
            self.assertEqual(offset, capacity)
        return ret, result

    def test_split_flags_and_reservation_tail(self):
        ret, result = self.call()
        self.assertEqual(ret, 0)
        self.assertEqual([(b.offset, b.size, b.info, b.attributes)
                          for b in result.blocks[:result.count]],
                         [(0, 0x1000, 1, 1), (0x1000, 0x1000, 1, 5),
                          (0x2000, 0x1000, 1, 1), (0x3000, 0x1000, 2, 3),
                          (0x4000, 0x4000, 0, 0)])
        self.assertEqual(self.call(base=0x120000000)[0], 0)
        self.assertEqual(self.call(capacity=0x4000)[0], 0)

    def test_ccci_encoding_matches_wire_format(self):
        for base in (0x50000000, 0x120000000):
            ret, mapped = self.call(base=base)
            self.assertEqual(ret, 0)
            expected = b"".join(struct.pack("<IIIIQ", b.offset, b.size,
                                           b.info, b.attributes, b.physical)
                                 for b in mapped.blocks[:mapped.count])
            arena = c.create_string_buffer(b"\xa5" * (len(expected) + 32))
            ret = encode_memory(c.byref(mapped), base, 0x8000,
                                c.addressof(arena) + 16, len(expected))
            self.assertEqual(ret, len(expected))
            self.assertEqual(arena.raw, b"\xa5" * 16 + expected + b"\xa5" * 16 + b"\0")
            # A caller may reuse the map's storage once validation finishes.
            self.assertEqual(encode_memory(c.byref(mapped), base, 0x8000,
                                           c.byref(mapped), c.sizeof(mapped)), len(expected))
            self.assertEqual(bytes(mapped)[:len(expected)], expected)

    def test_ccci_encoding_failure_preserves_output(self):
        for field, value in (("offset", 1), ("size", 0), ("size", 0xffffffff),
                             ("physical", 0x50000001)):
            for index in (0, 4):
                _, mapped = self.call()
                setattr(mapped.blocks[index], field, value)
                out = c.create_string_buffer(b"\xa5" * 768)
                before = out.raw
                self.assertLess(encode_memory(c.byref(mapped), 0x50000000,
                                              0x8000, out, 768), 0)
                self.assertEqual(out.raw, before)
        for count, size in ((0, 768), (33, 768), (4, 768), (5, 119)):
            _, mapped = self.call()
            mapped.count = count
            out = c.create_string_buffer(b"\xa5" * 768)
            before = out.raw
            self.assertLess(encode_memory(c.byref(mapped), 0x50000000,
                                          0x8000, out, size), 0)
            self.assertEqual(out.raw, before)

    def test_bad_base_and_capacity(self):
        for base, capacity in ((0, 0x8000), (2**64 - 0x4000, 0x8000),
                               (0x50000000, 0), (0x50000000, 0x3fff),
                               (0x50000000, 2**32)):
            self.assertNotEqual(self.call(base=base, capacity=capacity)[0], 0)

    def test_padding_must_not_cover_images_or_cross_regions(self):
        for offset, size in ((0, 0x1000), (0x3000, 0x1000), (0x2800, 0x1000),
                              (0xffffffff, 0x1000), (0x1000, 0xffffffff)):
            rom = self.fixture()
            struct.pack_into("<II", rom, len(rom) - 512 + 0x11c, offset, size)
            self.assertNotEqual(self.call(rom)[0], 0)

    def test_duplicate_padding_rejected(self):
        rom = self.fixture()
        struct.pack_into("<II", rom, len(rom) - 512 + 0x124, 0x1000, 0x1000)
        self.assertNotEqual(self.call(rom)[0], 0)

    def test_optional_window_flags(self):
        for field, flag in ((0x16c, 0x20), (0x174, 0x40), (0x164, 0x80)):
            rom = self.fixture()
            struct.pack_into("<II", rom, len(rom) - 512 + field, 0x2000, 0x1000)
            ret, result = self.call(rom)
            self.assertEqual(ret, 0)
            self.assertEqual(result.blocks[2].attributes, 1 | flag)
            struct.pack_into("<II", rom, len(rom) - 512 + field, 0xffffffff, 16)
            self.assertNotEqual(self.call(rom)[0], 0)

    def test_block_budget_is_atomic(self):
        rom = self.fixture()
        head = len(rom) - 512
        for field, value in ((172, 0x80000), (184, 0x7e000), (192, 8)):
            struct.pack_into("<I", rom, head + field, value)
        for i in range(8):
            struct.pack_into("<II", rom, head + 196 + 8 * i, i * 0x10000, 0x10000)
            struct.pack_into("<II", rom, head + 0x11c + 8 * i,
                             i * 0x10000 + 0x2000, 0x1000)
        for field, offset in ((0x16c, 0x4000), (0x174, 0x6000), (0x164, 0x8000)):
            struct.pack_into("<II", rom, head + field, offset, 0x1000)
        self.assertNotEqual(self.call(rom, capacity=0x90000)[0], 0)


if __name__ == "__main__":
    unittest.main()
