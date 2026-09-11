#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Compile the actual header walker; reject any synthetic payload access."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "arch/arm/lib/save_prev_bl_data.c").read_text()
start = source.index("static int observe_mt6878_tag_list(")
end = source.index("\nint get_prev_bl_tag_list_diagnostics(", start)
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint64_t phys_addr_t;
static unsigned long reg0 = 0x1000, reg5;
static int prev_tag_header_error;
static unsigned char bytes[4096], allowed[4096];
static int maps, unmaps, fail_map, cases;
static u32 le32_to_cpu(u32 value)
{
    const unsigned char *p = (const unsigned char *)&value;
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static const void *map_sysmem(phys_addr_t address, size_t size)
{
    assert(address >= reg0 && size == 4);
    size_t offset = address - reg0;
    assert(offset <= reg5 && size <= reg5 - offset);
    for (size_t i = offset; i < offset + size; i++)
        assert(allowed[i]);
    maps++;
    return maps == fail_map ? NULL : bytes + offset;
}
static void unmap_sysmem(const void *address)
{
    assert((const unsigned char *)address >= bytes);
    assert((const unsigned char *)address < bytes + sizeof(bytes));
    unmaps++;
}
/* ACTUAL_WALKER */
static void word(size_t offset, u32 value)
{
    assert(offset + 4 <= sizeof(bytes));
    for (size_t i = 0; i < 4; i++) {
        bytes[offset + i] = value >> (8 * i);
        allowed[offset + i] = 1;
    }
}
static void tag(size_t offset, u32 size, u32 id)
{
    word(offset, size);
    word(offset + 4, id);
}
static void reset(size_t length)
{
    memset(bytes, 0xa5, sizeof(bytes));
    memset(allowed, 0, sizeof(allowed));
    reg5 = length;
    prev_tag_header_error = 0;
    maps = unmaps = fail_map = 0;
}
static void check(int expected, u32 count, u32 low, u32 high, int reads)
{
    unsigned char before[sizeof(bytes)];
    u32 c = 99, l = 99, h = 99;
    memcpy(before, bytes, sizeof(bytes));
    assert(observe_mt6878_tag_list(&c, &l, &h) == expected);
    assert(c == count && l == low && h == high);
    assert(maps == reads && unmaps == maps - (fail_map > 0));
    assert(!memcmp(before, bytes, sizeof(bytes)));
    cases++;
}
int main(void)
{
    reset(12); prev_tag_header_error = -EFAULT; check(-EFAULT, 0, 0, 0, 0);
    reset(0); check(-ENODATA, 0, 0, 0, 0);
    reset(3); check(-ENODATA, 0, 0, 0, 0);
    reset(4); word(0, 0); check(0, 0, 0, 0, 1);
    reset(12); tag(0, 8, 0x88610001); word(8, 0); check(0, 1, 2, 0, 3);
    reset(13); tag(0, 9, 0x8861002e); word(9, 0); check(0, 1, 0, 1U << 14, 3);
    reset(44); tag(0, 40, 0x8861001f); word(40, 0); check(0, 1, 1U << 31, 0, 3);
    reset(20); tag(0, 8, 0x88610020); tag(8, 8, 0x8861003f); word(16, 0);
    check(0, 2, 0, 1U | (1U << 31), 5);
    reset(20); tag(0, 8, 0x88610000); tag(8, 8, 0x88610000); word(16, 0);
    check(0, 2, 1, 0, 5);
    reset(12); tag(0, 8, 0x8861ffff); word(8, 0); check(0, 1, 0, 0, 3);
    reset(12); tag(0, 7, 0x88610001); check(-EBADMSG, 0, 0, 0, 1);
    reset(12); tag(0, 13, 0x88610001); check(-EBADMSG, 0, 0, 0, 1);
    reset(12); tag(0, UINT32_MAX, 0x88610001); check(-EBADMSG, 0, 0, 0, 1);
    reset(12); tag(0, 8, 0x11223344); check(-EOPNOTSUPP, 0, 0, 0, 2);
    reset(8); tag(0, 8, 0x88610001); check(-ENODATA, 0, 0, 0, 2);
    reset(11); tag(0, 8, 0x88610001); check(-ENODATA, 0, 0, 0, 2);
    reset(16); tag(0, 8, 0x88610001); tag(8, 7, 0x88610001);
    check(-EBADMSG, 0, 0, 0, 3);
    reset(12); tag(0, 8, 0x88610001); word(8, 0); fail_map = 1;
    check(-ENOMEM, 0, 0, 0, 1);
    reset(12); tag(0, 8, 0x88610001); word(8, 0); fail_map = 2;
    check(-ENOMEM, 0, 0, 0, 2);
    reset(12); tag(0, 8, 0x88610001); word(8, 0); fail_map = 3;
    check(-ENOMEM, 0, 0, 0, 3);
    reset(2052);
    for (size_t i = 0; i < 256; i++) tag(i * 8, 8, 0x88610001);
    word(2048, 0); check(0, 256, 2, 0, 513);
    reset(2060);
    for (size_t i = 0; i < 257; i++) tag(i * 8, 8, 0x88610001);
    word(2056, 0); check(-E2BIG, 0, 0, 0, 513);
    printf("Tetris bounded tag-list host tests: PASS (%d cases)\n", cases);
}
'''
program = fixture.replace("/* ACTUAL_WALKER */", source[start:end])
with tempfile.TemporaryDirectory(prefix="tetris-tag-list-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run([os.environ.get("HOSTCC", "cc"), "-std=c11", "-Wall",
                    "-Wextra", "-Werror", "-x", "c", "-", "-o", binary],
                   input=program, text=True, check=True)
    subprocess.run([binary], check=True)
