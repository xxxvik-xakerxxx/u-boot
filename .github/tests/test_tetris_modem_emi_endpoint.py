#!/usr/bin/env python3
"""Source-only admission checks; real range transactions execute only in CI."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EndpointContractTests(unittest.TestCase):
    def test_only_end_marker_is_excluded(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_emi.c').read_text()
        header = (ROOT / 'board/mediatek/mt6878/tetris_modem_emi.h').read_text()
        self.assertIn('#define TETRIS_MODEM_EMI_END_MARKER (1ULL << 43)', header)
        self.assertEqual(source.count('compare_mask = ~0ULL;'), 1)
        self.assertEqual(source.count('compare_mask &= ~TETRIS_MODEM_EMI_END_MARKER;'), 1)
        endpoint = source[source.index('case 11:'):source.index('case 12:')]
        self.assertIn('compare_mask &= ~TETRIS_MODEM_EMI_END_MARKER;', endpoint)
        self.assertIn('transaction->reply[step] & compare_mask', source)
        self.assertIn('expected & compare_mask', source)
        self.assertLess(source.index('if (ret)\n\t\t\tgoto failed;'),
                        source.index('transaction->reply[step] & compare_mask'))
        self.assertIn('case 12:\n\t\t\texpected = 1;', source)
        self.assertIn('expected = expected_policy[c];', source)
        self.assertIn('transaction->state != TETRIS_MODEM_EMI_FRESH', source)


if __name__ == '__main__':
    unittest.main()
