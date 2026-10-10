#!/usr/bin/env python3
"""Source-only API guards; native regression executes in CI."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LinuxPolicyTests(unittest.TestCase):
    def test_iterator_returns_block_child(self):
        upstream = (ROOT / 'drivers/block/blk-uclass.c').read_text()
        start = upstream.index('int blk_first_device(')
        self.assertIn('uclass_find_first_device(UCLASS_BLK, devp)',
                      upstream[start:upstream.index('int blk_next_device(', start)])
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_linux_policy.c').read_text()
        selection = source[source.index('static int selected_boot('):
                           source.index('static int preloader_digest(')]
        self.assertNotIn('blk_get_by_device(', selection)
        self.assertIn('candidate = dev_get_uclass_plat(device)', selection)
        self.assertIn('candidate->bdev != device', selection)
        self.assertIn('device->parent == user->bdev->parent', selection)
        self.assertIn('candidate->target == user->target', selection)
        self.assertIn('if (seen != 1)', selection)

    def test_production_source_and_ci_gate(self):
        fixture = (ROOT / '.github/tests/tetris_modem_linux_policy.c').read_text()
        self.assertIn('#include "../../board/mediatek/mt6878/tetris_modem_linux_policy.c"', fixture)
        self.assertIn('assert(selected_boot(&descriptors[2], &output) == 0)', fixture)
        runner = (ROOT / '.github/tests/run_tetris_modem_linux_policy.sh').read_text()
        self.assertLess(runner.index('if [ "${CI:-}" != true ]'),
                        runner.index('"${HOSTCC:-cc}"'))
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('sh .github/tests/run_tetris_modem_linux_policy.sh', workflow)


if __name__ == '__main__':
    unittest.main()
