#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Offline, hash-pinned LK/ATF EMI dispatch audit. No device access."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1
from unicorn.arm64_const import UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_LR
from unicorn.arm64_const import UC_ARM64_REG_SP

ATF_HASH = "05a247cb02696ce4fe1982ea00bba81236c352146c307159d3f9e380635ea32e"
LK_HASH = "431e0551382e21f4edfb8ff3ca05cd67b177d40b1a51f9e863965eea58f8b94a"
BASE = 0x48800000
ENTRY = 0x308AC


def audit_range_handler(atf):
    """Run real handler code against zeroed synthetic BSS and fake MMIO only."""
    assert struct.unpack_from("<4I", atf, 0x4771C) == (8, 10, 11, 19)
    mmio, stack, done = 0x10351000, 0x70000000, BASE + 0x308D8
    results = []

    def fixture():
        machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        machine.mem_map(BASE, 0x100000)
        machine.mem_write(BASE, atf)
        machine.mem_map(stack, 0x10000)
        machine.mem_map(mmio, 4096)
        writes, stops = [], []

        def write(uc, access, address, size, value, context):
            if mmio <= address < mmio + 4096:
                writes.append((address, size, value))

        def stop(uc, address, size, context):
            if address == done:
                stops.append(address)
                uc.emu_stop()

        machine.hook_add(UC_HOOK_MEM_WRITE, write)
        machine.hook_add(UC_HOOK_CODE, stop)
        return machine, writes, stops

    def invoke(machine, writes, stops, start, end, slot, operation=0):
        writes.clear()
        stops.clear()
        for register, value in zip((UC_ARM64_REG_X0, UC_ARM64_REG_X1,
                                    UC_ARM64_REG_X2, UC_ARM64_REG_X3),
                                   (operation, start, end, slot)):
            machine.reg_write(register, value)
        machine.reg_write(UC_ARM64_REG_SP, stack + 0x8000)
        machine.reg_write(UC_ARM64_REG_LR, done)
        machine.emu_start(BASE + ENTRY, BASE + len(atf), count=512)
        assert stops == [done], "handler exceeded instruction budget"
        value = machine.reg_read(UC_ARM64_REG_X0)
        return value - (1 << 64) if value & (1 << 63) else value

    for slot in range(32, 44):
        machine, writes, stops = fixture()
        assert invoke(machine, writes, stops, 0x80000, 0x81000, slot) == 0
        enable = mmio + 0x2A4 + ((slot - 1) // 32) * 4
        expected = [(mmio + (slot - 1) * 8, 4, 0x40000),
                    (mmio + (slot - 1) * 8 + 4, 4, 0x80041000),
                    (enable, 4, 1 << ((slot - 1) % 32))]
        assert writes == expected, (slot, writes)
        assert invoke(machine, writes, stops, 0x80000, 0x81000, slot) == -4
        assert not writes, "second call touched MMIO"
        assert invoke(machine, writes, stops, slot, 0, 0, operation=1) == -1
        assert not writes, "modem-slot disable touched MMIO"
        results.append({"slot": slot, "first": 0, "second": -4,
                        "disable": -1,
                        "first_mmio_writes": expected})
    for start, end, slot in ((0x3FFFF, 0x81000, 32),
                              (0x81000, 0x80000, 32),
                              (0x80000, 0x81000, 0),
                              (0x80000, 0x81000, 64)):
        machine, writes, stops = fixture()
        assert invoke(machine, writes, stops, start, end, slot) == -3
        assert not writes
    # The firmware silently masks high page bits; the loader must reject them.
    machine, writes, stops = fixture()
    assert invoke(machine, writes, stops, 0x1080000, 0x1081000, 32) == 0
    assert writes[0][2] == 0x40000 and writes[1][2] == 0x80041000
    return results


def payload(path, expected):
    data = Path(path).read_bytes()
    if data[:4] == bytes.fromhex("88168858"):
        size = struct.unpack_from("<I", data, 4)[0]
        if len(data) < 512 + size:
            raise ValueError("truncated MediaTek container")
        data = data[512:512 + size]
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("unsupported payload hash; do not reuse audited offsets")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--atf", required=True)
    parser.add_argument("--lk", required=True)
    args = parser.parse_args()
    atf, lk = payload(args.atf, ATF_HASH), payload(args.lk, LK_HASH)
    interfaces = []
    for offset, expected_id, expected_name, expected_handler in (
        (0x58BA8, 0x82000505, b"MTK_SIP_KERNEL_CCCI_CONTROL", 0xBE28),
        (0x58BC8, 0x8200040B, b"MTK_SIP_LK_CCCI_CONTROL", 0x20D90),
        (0x58E68, 0x8200050B, b"MTK_SIP_EMIDBG_CONTROL", 0x2F750),
        (0x58E88, 0x82000415, b"MTK_SIP_BL_EMIMPU_CONTROL", 0x308AC),
    ):
        low, high, label, _, entry = struct.unpack_from("<IIQQQ", atf, offset)
        assert low == expected_id and high == expected_id | 0x40000000
        assert entry == BASE + expected_handler
        assert atf[label - BASE:].split(b"\0", 1)[0] == expected_name
        interfaces.append({"smc32": hex(low), "smc64": hex(high),
                           "name": expected_name.decode(), "handler": hex(expected_handler)})
    smc32, smc64, name, _, handler = struct.unpack_from("<IIQQQ", atf, 0x58E88)
    assert (smc32, smc64, handler) == (0x82000415, 0xC2000415, BASE + ENTRY)
    assert atf[name - BASE:].split(b"\0", 1)[0] == b"MTK_SIP_BL_EMIMPU_CONTROL"

    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    instructions = {i.address: (i.mnemonic, i.op_str)
                    for start, end in ((0x7EE38, 0x7EEB8), (0x81804, 0x818E4))
                    for i in decoder.disasm(lk[start:end], start)}
    assert instructions[0x7EE58] == ("mov", "w1, #6")
    assert instructions[0x818C0] == ("bl", "#0x7ee38")
    assert instructions[0x818C4] == ("b", "#0x8185c")
    assert instructions[0x8185C] == ("add", "x0, sp, #8")
    assert instructions[0x81860] == ("bl", "#0x7ef84")

    results = []
    for operation in (*range(16), (1 << 64) - 1):
        machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        machine.mem_map(BASE, (len(atf) + 4095) & ~4095)
        machine.mem_write(BASE, atf)
        registers = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3)
        # Synthetic arguments. Stop BEFORE any helper can touch hardware.
        for register, value in zip(registers, (operation, 0x80000, 0x81000, 40)):
            machine.reg_write(register, value)
        done = BASE + 0x308D8
        machine.reg_write(UC_ARM64_REG_LR, done)
        stopped = []

        def stop(uc, address, size, context):
            if address in (done, BASE + 0x2F618, BASE + 0x2ED24):
                stopped.append(address - BASE)
                uc.emu_stop()

        machine.hook_add(UC_HOOK_CODE, stop)
        machine.emu_start(BASE + ENTRY, BASE + len(atf), count=32)
        values = [machine.reg_read(register) for register in registers]
        if operation == 0:
            assert stopped == [0x2F618] and values[:3] == [0x80000, 0x81000, 40]
        elif operation == 1:
            assert stopped == [0x2ED24] and values[0] == 0x80000
        else:
            assert stopped == [0x308D8] and values[0] == (1 << 64) - 2
        results.append({"operation": operation, "stop_offset": hex(stopped[0]),
                        "x0": hex(values[0])})
    ranges = audit_range_handler(atf)
    print(json.dumps({"atf_sha256": ATF_HASH, "lk_sha256": LK_HASH,
                      "result": "PASS", "dispatch": results,
                      "interfaces": interfaces,
                      "range_handler": ranges,
                      "scope": "offline dispatch and range handler; synthetic BSS/MMIO only",
                      "lk_operation_6_return": "ignored before operation 0"}, indent=2))


if __name__ == "__main__":
    main()
