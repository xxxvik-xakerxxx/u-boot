#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Offline, hash-pinned LK/ATF EMI dispatch audit. No device access."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1
from unicorn.arm64_const import UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_LR

ATF_HASH = "05a247cb02696ce4fe1982ea00bba81236c352146c307159d3f9e380635ea32e"
LK_HASH = "431e0551382e21f4edfb8ff3ca05cd67b177d40b1a51f9e863965eea58f8b94a"
BASE = 0x48800000
ENTRY = 0x308AC


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
    print(json.dumps({"atf_sha256": ATF_HASH, "lk_sha256": LK_HASH,
                      "result": "PASS", "dispatch": results,
                      "scope": "dispatch only; helpers and hardware not executed",
                      "lk_operation_6_return": "ignored before operation 0"}, indent=2))


if __name__ == "__main__":
    main()
