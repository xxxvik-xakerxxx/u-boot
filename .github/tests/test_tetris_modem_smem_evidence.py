#!/usr/bin/env python3
"""Source-only checks; execute real native encoder/publication tests in CI."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'board/mediatek/mt6878'


class EvidenceTests(unittest.TestCase):
    def test_private_owner_is_the_only_publisher_input(self):
        loader = (BOARD / 'tetris_modem_loaded_boot.c').read_text()
        wrapper = loader[loader.index('int tetris_modem_loaded_encode_smem_evidence('):]
        self.assertIn('owner.report.stage != TETRIS_MD_LOAD_COMPLETE', wrapper)
        self.assertIn('if (owner.report.error)', wrapper)
        self.assertIn('tetris_modem_build_smem_evidence(&owner.report, buffer, size)', wrapper)

    def test_complete_actual_replies_and_independent_policy(self):
        source = (BOARD / 'tetris_modem_smem_evidence.c').read_text()
        for token in ('TETRIS_MD_BOOT_COMPLETE', 'TETRIS_MD_CLEANUP_NONE',
                      'TETRIS_MODEM_EMI_RANGE_VERIFIED', 'TETRIS_MODEM_EMI_STEPS - 1',
                      'tetris_modem_plan_emi_policy(bootstrap->preloader_sha256',
                      'transaction->reply[1 + group] != policy[group]',
                      'transaction->reply[13 + group] != policy[group]'):
            self.assertIn(token, source)
        self.assertLess(source.index('for (bank = 0; bank < 2; bank++)'),
                        source.index('memcpy(buffer, encoded, sizeof(encoded))'))
        for forbidden in ('readl(', 'writel(', 'smc(', 'ioremap', 'map_sysmem', 'READY'):
            self.assertNotIn(forbidden, source)

    def test_scoped_wire_and_real_fdt_path(self):
        header = (BOARD / 'tetris_modem_smem_evidence.h').read_text()
        self.assertIn('EVIDENCE_SIZE 336U', header)
        self.assertIn('NOT a Linux NS access grant', header)
        source = (BOARD / 'tetris_modem_final.c').read_text()
        self.assertIn('"nothing,modem-smem-boot-evidence"', source)
        self.assertLess(source.index('tetris_modem_loaded_encode_smem_evidence('),
                        source.index('lmb_alloc_mem('))
        self.assertLess(source.rindex('"nothing,modem-smem-boot-evidence"'),
                        source.index('images->ft_addr = final;'))
        runner = (ROOT / '.github/tests/run_tetris_modem_smem_evidence.sh').read_text()
        self.assertLess(runner.index('"${GITHUB_ACTIONS:-}"'), runner.index('"${HOSTCC:-cc}"'))
        self.assertIn('board/mediatek/mt6878/tetris_modem_smem_evidence.c', runner)

    def test_native_runner_refuses_local_compilation(self):
        result = subprocess.run(['sh', str(ROOT / '.github/tests/run_tetris_modem_smem_evidence.sh')],
                                env={'PATH': '/usr/bin:/bin'}, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('Refusing native compilation', result.stderr)


if __name__ == '__main__':
    unittest.main()
