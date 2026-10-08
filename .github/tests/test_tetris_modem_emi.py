#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""CI-only native EMI range transaction tests; never issue an actual SMC."""
import ctypes as c
import errno
import sys
import unittest


class Transaction(c.Structure):
    _fields_ = [("state", c.c_uint), ("step", c.c_uint), ("slot", c.c_uint),
                ("reply", c.c_ulonglong * 21)]


Callback = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint, c.c_uint,
                      c.c_ulonglong, c.c_ulonglong, c.c_ulonglong,
                      c.POINTER(c.c_ulonglong))


class Ops(c.Structure):
    _fields_ = [("smc", Callback), ("context", c.c_void_p)]


library = c.CDLL(sys.argv.pop(1))
program = library.tetris_modem_program_emi_range
program.argtypes = [c.c_ulonglong, c.c_ulonglong, c.c_uint,
                    c.c_ulonglong, c.c_ulonglong, c.POINTER(c.c_ulonglong), c.POINTER(Ops),
                    c.POINTER(Transaction)]
program.restype = c.c_int


class EmiTransactionTest(unittest.TestCase):
    def run_case(self, start=0x80000000, size=0x200000, slot=32,
                 base=0x80000000, capacity=0x20000000, state=0,
                 corrupt=None, transport=None, missing=None, policy=None):
        calls = []
        policy = list(policy) if policy is not None else [0x123456789abcdef0 ^ (i << 40) for i in range(8)]
        policy_array = (c.c_ulonglong * 8)(*policy)
        values = [0] + policy + [0, start, (start + size) | (1 << 43), 1] + policy

        def smc(context, fid, operation, a, b, d, output):
            step = len(calls)
            calls.append((context, fid, operation, a, b, d))
            if step != missing:
                output[0] = corrupt[1] if corrupt and corrupt[0] == step else values[step]
            return transport[1] if transport and transport[0] == step else 0

        cb = Callback(smc)
        ops = Ops(cb, 123)
        tx = Transaction(state=state)
        ret = program(start, size, slot, base, capacity, policy_array, c.byref(ops), c.byref(tx))
        queries = [(123, 0xc2000415, 2, 4, slot, group) for group in range(8)]
        expected = [(123, 0xc2000415, 2, 3, slot, 0)] + queries + [
                    (123, 0xc2000415, 0, start >> 12, (start + size) >> 12, slot),
                    (123, 0xc2000415, 2, 0, slot, 0),
                    (123, 0xc2000415, 2, 1, slot, 0),
                    (123, 0xc2000415, 2, 3, slot, 0)] + queries
        self.assertEqual(calls, expected[:len(calls)])
        if tx.state:
            before, count = bytes(tx), len(calls)
            self.assertEqual(program(start, size, slot, base, capacity,
                                     policy_array, c.byref(ops), c.byref(tx)), -errno.EALREADY)
            self.assertEqual(bytes(tx), before)
            self.assertEqual(len(calls), count)
        return ret, tx, calls

    def test_all_slots_and_wide_addresses(self):
        for slot in range(32, 44):
            for start in (0x40000000, 0x80000000, 0x120000000, 0x83fffe000):
                with self.subTest(slot=slot, start=start):
                    ret, tx, calls = self.run_case(start=start, base=start,
                                                    size=4096, capacity=4096, slot=slot)
                    self.assertEqual(ret, 0)
                    self.assertEqual((tx.state, tx.step, tx.slot), (2, 20, slot))
                    self.assertEqual(len(calls), 21)

    def test_every_result_is_checked(self):
        policy = [0x123456789abcdef0 ^ (i << 40) for i in range(8)]
        values = [0] + policy + [0, 0x80000000, 0x80200000 | (1 << 43), 1] + policy
        for step, value in enumerate(values):
            for bit in range(64):
                with self.subTest(step=step, bit=bit):
                    ret, tx, calls = self.run_case(corrupt=(step, value ^ (1 << bit)))
                    self.assertLess(ret, 0)
                    self.assertEqual((tx.state, tx.step), (3, step))
                    self.assertEqual(len(calls), step + 1)

    def test_busy_slot_is_not_written(self):
        ret, tx, calls = self.run_case(corrupt=(0, 1))
        self.assertEqual(ret, -errno.EBUSY)
        self.assertEqual(len(calls), 1)

    def test_missing_and_transport_failures(self):
        for step in range(21):
            for args in (dict(missing=step), dict(transport=(step, -errno.ETIMEDOUT)),
                         dict(transport=(step, 1)), dict(corrupt=(step, 2**64 - 4))):
                with self.subTest(step=step, args=args):
                    ret, tx, calls = self.run_case(**args)
                    self.assertLess(ret, 0)
                    self.assertEqual((tx.state, tx.step), (3, step))
                    self.assertEqual(len(calls), step + 1)
                    if args.get("transport", (0, 0))[1] < 0:
                        self.assertEqual(ret, -errno.ETIMEDOUT)

    def test_preflight_is_atomic_and_no_calls(self):
        for args in (dict(start=0), dict(start=0x80000001), dict(size=0),
                     dict(size=4097), dict(slot=31), dict(slot=44),
                     dict(base=0), dict(capacity=0), dict(capacity=4096),
                     dict(base=0x80001000), dict(start=0xa0000000),
                     dict(base=2**64 - 1), dict(capacity=2**64 - 1),
                     dict(start=0x83ffff000, base=0x83ffff000, size=4096)):
            with self.subTest(args=args):
                ret, tx, calls = self.run_case(**args)
                self.assertLess(ret, 0)
                self.assertEqual(bytes(tx), bytes(Transaction()))
                self.assertEqual(calls, [])

    def test_consumed_and_null_arguments(self):
        for state in (1, 2, 3, 99):
            ret, tx, calls = self.run_case(state=state)
            self.assertEqual(ret, -errno.EALREADY)
            self.assertEqual(tx.state, state)
            self.assertEqual(calls, [])
        tx, empty = Transaction(), Ops()
        policy = (c.c_ulonglong * 8)()
        for ops, transaction in ((None, c.byref(tx)), (c.byref(empty), c.byref(tx)),
                                 (c.byref(empty), None)):
            self.assertEqual(program(0, 0, 0, 0, 0, policy, ops, transaction), -errno.EINVAL)

    def test_uniform_policies_and_missing_outputs(self):
        for value in (0, 2**64 - 1):
            self.assertEqual(self.run_case(policy=[value] * 8)[0], 0)
            for step in (*range(1, 9), *range(13, 21)):
                ret, tx, calls = self.run_case(policy=[value] * 8, missing=step)
                self.assertLess(ret, 0)
                self.assertEqual(len(calls), step + 1)

    def test_policy_required_before_any_call(self):
        calls = []
        cb = Callback(lambda *args: calls.append(args) or 0)
        ops, tx = Ops(cb, None), Transaction()
        self.assertEqual(program(0x80000000, 4096, 32, 0x80000000, 4096,
                                 None, c.byref(ops), c.byref(tx)), -errno.EINVAL)
        self.assertEqual(calls, [])
        self.assertEqual(bytes(tx), bytes(Transaction()))


if __name__ == "__main__":
    unittest.main()
