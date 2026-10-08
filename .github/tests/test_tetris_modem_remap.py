#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""CI-only native execution; all secure calls are recorded synthetic callbacks."""
import ctypes as c
import sys
import unittest


class Transaction(c.Structure):
    _fields_ = [("state", c.c_uint), ("operation", c.c_uint),
                ("reply", (c.c_ulonglong * 4) * 2)]


Callback = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint, c.c_uint,
                      c.c_uint, c.c_uint, c.POINTER(c.c_ulonglong))


class Ops(c.Structure):
    _fields_ = [("smc", Callback), ("context", c.c_void_p)]


library = c.CDLL(sys.argv.pop(1))
program = library.tetris_modem_program_remap
program.argtypes = [c.c_ulonglong] * 4 + [c.POINTER(Ops), c.POINTER(Transaction)]
program.restype = c.c_int


class RemapTransactionTest(unittest.TestCase):
    def run_case(self, base=0x80000000, capacity=0x20000000,
                 dram_base=0x40000000, dram_size=0x800000000,
                 mutate=None, transport=None, missing=None, state=0):
        registers = [0] * 6
        for page in range(16):
            registers[page // 3] |= ((base >> 25) + page) << (10 * (page % 3))
        calls = []

        def smc(context, function, operation, low, high, output):
            calls.append((context, function, operation, low, high))
            values = ([0, registers[0], registers[1],
                       (registers[2] & 0xfffff) | 0x15500000] if operation == 1
                      else registers[2:])
            if mutate:
                mutate(operation, values)
            for i, value in enumerate(values):
                if missing != (operation, i):
                    output[i] = value
            return transport[1] if transport and transport[0] == operation else 0

        callback = Callback(smc)
        ops = Ops(callback, 123)
        transaction = Transaction(state=state)
        result = program(base, capacity, dram_base, dram_size,
                         c.byref(ops), c.byref(transaction))
        if transaction.state:
            count = len(calls)
            self.assertNotEqual(program(base, capacity, dram_base, dram_size,
                                        c.byref(ops), c.byref(transaction)), 0)
            self.assertEqual(len(calls), count, "a consumed transaction was retried")
        for context, function, operation, low, high in calls:
            self.assertEqual(context, 123)
            self.assertEqual(function, 0xc200040b)
            self.assertIn(operation, (1, 2))
            self.assertEqual(low | high << 32, base)
        return result, transaction, calls

    def test_success_and_nonzero_second_x0(self):
        for base in (0x40000000, 0x80000000, 0x120000000, 0x7e0000000):
            ret, transaction, calls = self.run_case(base=base)
            self.assertEqual(ret, 0)
            self.assertEqual(transaction.state, 2)
            self.assertEqual([call[2] for call in calls], [1, 2])
            self.assertNotEqual(transaction.reply[1][0], 0)

    def test_each_owned_readback_bit_is_checked(self):
        for operation, fields in ((1, ((1, 30), (2, 30), (3, 20))),
                                  (2, ((0, 30), (1, 30), (2, 30), (3, 10)))):
            for field, bits in fields:
                for bit in range(bits):
                    def corrupt(op, values):
                        if op == operation:
                            values[field] ^= 1 << bit
                    with self.subTest(operation=operation, field=field, bit=bit):
                        ret, transaction, calls = self.run_case(mutate=corrupt)
                        self.assertNotEqual(ret, 0)
                        self.assertEqual(transaction.state, 3)
                        self.assertEqual(transaction.operation, operation)
                        self.assertEqual(len(calls), operation)

    def test_unowned_register_bits_do_not_fail(self):
        def modify(operation, values):
            if operation == 1:
                values[1] |= 0xc0000000
                values[2] |= 0xc0000000
                values[3] |= 0xfff00000
            else:
                for i in range(3):
                    values[i] |= 0xc0000000
                values[3] |= 0xfffffc00
        self.assertEqual(self.run_case(mutate=modify)[0], 0)

    def test_secure_errors_and_unknown_success_status(self):
        for operation in (1, 2):
            for error in (-1, -7, -15):
                def reject(op, values):
                    if op == operation:
                        values[0] = error
                ret, transaction, calls = self.run_case(mutate=reject)
                self.assertNotEqual(ret, 0)
                self.assertEqual(transaction.state, 3)
                self.assertEqual(len(calls), operation)
        def unexpected(op, values):
            if op == 1:
                values[0] = 1
        self.assertNotEqual(self.run_case(mutate=unexpected)[0], 0)

    def test_transport_failure_consumes_transaction(self):
        for operation in (1, 2):
            for error in (-5, 1):
                ret, transaction, calls = self.run_case(transport=(operation, error))
                self.assertLess(ret, 0)
                self.assertEqual(transaction.state, 3)
                self.assertEqual(len(calls), operation)

    def test_missing_outputs_and_wide_registers_fail(self):
        for operation in (1, 2):
            for field in range(4):
                self.assertNotEqual(self.run_case(missing=(operation, field))[0], 0)
                def wide(op, values):
                    if op == operation:
                        values[field] |= 1 << 32
                self.assertNotEqual(self.run_case(mutate=wide)[0], 0)

    def test_preflight_failure_never_calls_transport(self):
        for args in (dict(base=0), dict(base=0x80000001), dict(capacity=1),
                     dict(dram_size=1), dict(base=0x800000000),
                     dict(dram_base=0xffffffffffffffff)):
            ret, transaction, calls = self.run_case(**args)
            self.assertNotEqual(ret, 0)
            self.assertEqual(bytes(transaction), bytes(Transaction()))
            self.assertEqual(calls, [])

    def test_consumed_state_and_null_arguments(self):
        for state in (1, 2, 3, 99):
            ret, transaction, calls = self.run_case(state=state)
            self.assertNotEqual(ret, 0)
            self.assertEqual(transaction.state, state)
            self.assertEqual(calls, [])
        transaction = Transaction()
        empty = Ops()
        self.assertNotEqual(program(0, 0, 0, 0, None, c.byref(transaction)), 0)
        self.assertNotEqual(program(0, 0, 0, 0, c.byref(empty), c.byref(transaction)), 0)
        self.assertNotEqual(program(0, 0, 0, 0, c.byref(empty), None), 0)


if __name__ == "__main__":
    unittest.main()
