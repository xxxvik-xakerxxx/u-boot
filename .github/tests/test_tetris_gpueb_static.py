#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Cheap source/fixture gates; no compiler or hardware access."""
import ast
import hashlib
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "board/mediatek/mt6878"
SECURITY_BASELINE_SHA256 = "ecc93a78efbcecfedcf8c8cc2b24aa74017f3dec0dfc39cc6cee41b84035a684"


def body(source, name):
    start = source.index("int " + name + "(")
    first = source.index("{", start)
    depth = 1
    pos = first + 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


class Static(unittest.TestCase):
    def test_existing_security_behaviour_unchanged(self):
        path = "board/mediatek/mt6878/tetris_scp_security.c"
        current = (ROOT / path).read_text()
        # Remove only the new entry point; pin every pre-existing security byte
        # independently of checkout history and the current commit.
        added = body(current, "tetris_gpueb_authenticate") + "\n\n"
        self.assertEqual(current.count(added), 1)
        baseline = current.replace(added, "", 1)
        self.assertEqual(hashlib.sha256(baseline.encode()).hexdigest(),
                         SECURITY_BASELINE_SHA256)

    def test_no_registration_or_hardware_boot_and_one_transform(self):
        source = (BOARD / "tetris_gpueb_prepare.c").read_text()
        self.assertEqual(source.count("tetris_scp_crypto_decrypt("), 1)
        self.assertNotIn("tetris_scp_crypto_init(", source)
        for forbidden in ("writel(", "readl(", "arm_smccc_smc(", "printf(", "gunzip("):
            self.assertNotIn(forbidden, source)
        self.assertLess(source.index("tetris_gpueb_authenticate("), source.index("memcpy(staging"))
        self.assertIn("wipe(staging, capacity);", source)
        self.assertIn("TETRIS_SCP_CRYPTO_READY", source)

    def test_strict_signed_profile_and_header(self):
        source = body((BOARD / "tetris_scp_security.c").read_text(), "tetris_gpueb_authenticate")
        self.assertIn("{ 2, 3, 1, 0, 0 }", source)
        self.assertIn("verify_chain(", source)
        self.assertIn("get_digest(&leaf, 2, 4", source)
        self.assertIn("metadata_field(&leaf, 2, 9)", source)
        self.assertIn("nonzero_wrapped", source)

    def test_fixtures_parse_and_runner_refuses_local_build(self):
        ast.parse(Path(__file__).with_name("test_tetris_gpueb_c.py").read_text())
        runner = Path(__file__).with_name("run_tetris_gpueb.sh")
        result = subprocess.run(["sh", str(runner)], env={"CI": "false", "PATH": "/usr/bin:/bin"},
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("CI-only", result.stderr)


if __name__ == "__main__":
    unittest.main()
