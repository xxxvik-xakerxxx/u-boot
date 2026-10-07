#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""CI-only execution of the native relative modem layout planner."""
import ctypes as c
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


if __name__ == "__main__":
    unittest.main()
