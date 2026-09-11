#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Exercise the actual bounded header observer with synthetic memory."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "arch/arm/lib/save_prev_bl_data.c").read_text()
start = source.index("static int observe_mt6878_tag_header(void)")
end = source.index("\nint get_prev_bl_tag_header_error(void)", start)
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef unsigned long ulong;
typedef uint32_t u32;
typedef uint64_t phys_addr_t;
#define SZ_2M (2UL * 1024 * 1024)
static ulong reg0, reg4, reg5;
static unsigned char bytes[8];
static int maps, unmaps, ranges;
static bool allowed, available;
static u32 le32_to_cpu(u32 value)
{
    const unsigned char *p = (const unsigned char *)&value;
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static bool is_normal_memory_range(phys_addr_t address, size_t size)
{
    assert(address == reg0 && size == reg5);
    ranges++;
    return allowed;
}
static const void *map_sysmem(phys_addr_t address, size_t size)
{
    assert(address == reg0 && size == sizeof(bytes));
    maps++;
    return available ? bytes : NULL;
}
static void unmap_sysmem(const void *address)
{
    assert(address == bytes);
    unmaps++;
}
/* ACTUAL_OBSERVER */
static void reset(void)
{
    reg0 = reg4 = 0x1000;
    reg5 = 16;
    maps = unmaps = ranges = 0;
    allowed = available = true;
    memcpy(bytes, (unsigned char[]){8, 0, 0, 0, 1, 0, 0x61, 0x88}, 8);
}
static void check(int error, int expected_ranges, int expected_maps,
                  int expected_unmaps)
{
    unsigned char before[8];
    memcpy(before, bytes, 8);
    assert(observe_mt6878_tag_header() == error);
    assert(ranges == expected_ranges && maps == expected_maps);
    assert(unmaps == expected_unmaps && !memcmp(before, bytes, 8));
}
int main(void)
{
    reset(); reg0 = 0; check(-ENODATA, 0, 0, 0);
    reset(); reg4 = 0; check(-ENODATA, 0, 0, 0);
    reset(); reg5 = 0; check(-ENODATA, 0, 0, 0);
    reset(); reg4++; check(-EINVAL, 0, 0, 0);
    reset(); reg5 = 7; check(-EINVAL, 0, 0, 0);
    reset(); reg5 = SZ_2M + 1; check(-E2BIG, 0, 0, 0);
    reset(); allowed = false; check(-EFAULT, 1, 0, 0);
    reset(); available = false; check(-ENOMEM, 1, 1, 0);
    reset(); bytes[0] = 0; check(-EBADMSG, 1, 1, 1);
    reset(); bytes[0] = 7; check(-EBADMSG, 1, 1, 1);
    reset(); bytes[0] = 17; check(-EBADMSG, 1, 1, 1);
    reset(); bytes[3] = 0xff; check(-EBADMSG, 1, 1, 1);
    reset(); bytes[7] = 0; check(-EOPNOTSUPP, 1, 1, 1);
    reset(); check(0, 1, 1, 1);
    reset(); reg5 = 8; check(0, 1, 1, 1);
    reset(); reg5 = SZ_2M; check(0, 1, 1, 1);
    reset(); bytes[0] = 16; check(0, 1, 1, 1);
    puts("Tetris tag header host tests: PASS (17 cases)");
}
'''
program = fixture.replace("/* ACTUAL_OBSERVER */", source[start:end])
with tempfile.TemporaryDirectory(prefix="tetris-tag-header-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run([os.environ.get("HOSTCC", "cc"), "-std=c11", "-Wall",
                    "-Wextra", "-Werror", "-x", "c", "-", "-o", binary],
                   input=program, text=True, check=True)
    subprocess.run([binary], check=True)
