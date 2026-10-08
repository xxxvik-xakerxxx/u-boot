#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Audit pinned stock SIP registration/routing offline; never execute handlers."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import (UC_ARM64_REG_X0, UC_ARM64_REG_X1,
                                UC_ARM64_REG_X2, UC_ARM64_REG_X3,
                                UC_ARM64_REG_X4, UC_ARM64_REG_X6,
                                UC_ARM64_REG_X7, UC_ARM64_REG_LR,
                                UC_ARM64_REG_SP)

BASE, STACK, DONE = 0x48800000, 0x70000000, 0x7000F000
SHA = "05a247cb02696ce4fe1982ea00bba81236c352146c307159d3f9e380635ea32e"
TABLE, END = 0x58A20, 0x59540


def extract(path):
    data = path.read_bytes()
    if data[:4] == bytes.fromhex("88168858"):
        length = struct.unpack_from("<I", data, 4)[0]
        data = data[512:512 + length]
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError("unsupported ATF payload; re-audit offsets")
    return data


def audit(data, fid, ns, stage, policy):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, 0x100000)
    uc.mem_write(BASE, data)
    uc.mem_map(STACK, 0x10000)
    entries = [struct.unpack_from("<QIIQQ", data, offset)
               for offset in range(TABLE, END, 32)]
    handlers = {entry[0] for entry in entries}
    index_cells = {entry[4] for entry in entries}
    phase, stopped = "register", []

    def code(machine, address, size, context):
        if address == DONE or address in handlers or address == BASE + 0x3A608:
            stopped.append(address)
            machine.emu_stop()
            return
        offset = address - BASE
        allowed = ((0x23CB0, 0x23D14),) if phase == "register" else (
            (0x23030, 0x23CB0), (0x23D14, 0x244B8), (0x19BA0, 0x19BC8))
        assert any(start <= offset < end for start, end in allowed), hex(address)

    def write(machine, access, address, size, value, context):
        if STACK <= address and address + size <= STACK + 0xE000:
            return
        if phase == "register" and (
                (address in index_cells and size == 2) or
                (address == BASE + 0xEEFF0 and size == 8) or
                (address == BASE + 0xF659C and size == 2)):
            return
        raise AssertionError(f"unexpected {phase} write: {address:#x}/{size}")

    uc.hook_add(UC_HOOK_CODE, code)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    uc.reg_write(UC_ARM64_REG_SP, STACK + 0x8000)
    uc.reg_write(UC_ARM64_REG_LR, DONE)
    uc.emu_start(BASE + 0x23CB0, DONE + 4, count=4096)
    assert stopped == [DONE], "registration did not return"
    assert struct.unpack("<h", uc.mem_read(BASE + 0xF659C, 2))[0] == len(entries)
    for index, entry in enumerate(entries):
        assert struct.unpack("<h", uc.mem_read(entry[4], 2))[0] == index
    # Synthetic states only. These are not writes to a phone or a bypass.
    uc.mem_write(BASE + 0xF796A, bytes([stage]))
    uc.mem_write(BASE + 0x5AEA4, struct.pack("<I", policy))
    phase = "route"
    stopped.clear()
    for reg, value in ((UC_ARM64_REG_X0, fid), (UC_ARM64_REG_X1, 1),
                       (UC_ARM64_REG_X2, 0x80000), (UC_ARM64_REG_X3, 0x81000),
                       (UC_ARM64_REG_X4, 40), (UC_ARM64_REG_X6, STACK + 0x1000),
                       (UC_ARM64_REG_X7, ns), (UC_ARM64_REG_LR, DONE)):
        uc.reg_write(reg, value)
    uc.emu_start(BASE + 0x23030, DONE + 4, count=1024)
    assert len(stopped) == 1, "dispatcher exceeded instruction budget"
    target = stopped[0]
    return {"fid": hex(fid), "ns": ns, "stage_byte": stage,
            "policy_word": policy,
            "handler": hex(target - BASE) if target in handlers else None,
            "diagnostic": target == BASE + 0x3A608}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("atf", type=Path)
    args = parser.parse_args()
    data = extract(args.atf)
    results = [audit(data, fid | convention, ns, stage, policy)
               for fid in (0x8200040B, 0x82000505, 0x82000415, 0x8200050B)
               for convention in (0, 0x40000000)
               for ns in (0, 1) for stage in (0, 1) for policy in (0, 1)]
    expected = {0x8200040B: (0, "0xbe28"), 0x82000415: (0, "0x2f750"),
                0x82000505: (1, "0xbf2c"), 0x8200050B: (1, "0x2f6f8")}
    for result in results:
        stage, handler = expected[int(result["fid"], 16) & ~0x40000000]
        admitted = (result["ns"] == 1 and result["policy_word"] == 1 and
                    result["stage_byte"] == stage)
        assert result["handler"] == (handler if admitted else None), result
        assert result["diagnostic"] == (result["ns"] == 1 and
                                        result["policy_word"] == 0), result
    print(json.dumps({"sha256": SHA, "result": "PASS", "scenarios": len(results),
                      "results": results,
                      "scope": "synthetic state; stops before handlers; no MMIO"}, indent=2))


if __name__ == "__main__":
    main()
