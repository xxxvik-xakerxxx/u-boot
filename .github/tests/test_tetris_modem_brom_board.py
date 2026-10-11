#!/usr/bin/env python3
"""Source-only guards; native fault fixtures execute in CI, never here."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BoardReportTests(unittest.TestCase):
    def test_report_precedes_hardware(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_brom_board.c').read_text()
        self.assertLess(source.index('ret = publish(final, recorded, stage, 0, 0, 0, NULL)'),
                        source.index('ret = tetris_modem_linux_b41_once('))
        self.assertLess(source.index('images->ft_addr = final;'),
                        source.index('ret = tetris_modem_linux_b41_once('))
        disabled = source[source.index('static int disabled('):source.index('static int publish(')]
        self.assertIn('if (node == -FDT_ERR_NOTFOUND)\n\t\t\treturn 0;', disabled)
        self.assertNotIn('-ENODEV', disabled)
        self.assertIn('unsigned char recorded[80]', source)
        self.assertIn('put32(recorded, 1)', source)

    def test_loaded_observation_preserves_v1_hardware_fields(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_brom_board.c').read_text()
        for field in ('loaded-observation-valid', 'loaded-value', 'loaded-address'):
            self.assertIn('nothing,modem-brom-' + field, source)
        self.assertIn('report_ret ? NULL : &report', source)
        self.assertIn('cpu_to_fdt64(loaded ? loaded->address : 0)', source)
        self.assertIn('put32(recorded + 36, report.hardware.value)', source)
        self.assertIn('put64(recorded + 40, report.hardware.address)', source)
        self.assertNotIn('readl(', source)

    def test_error_order_and_owned_lifetime(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_brom_board.c').read_text()
        self.assertLess(source.index('latch(ret);'), source.index('latch(report_ret);'))
        cleanup = source[source.index('if (!committed) {'):]
        self.assertIn('unmap_sysmem(final)', cleanup)
        self.assertIn('lmb_free(address, capacity', cleanup)
        self.assertNotIn('map_sysmem(images->ft_addr', source)

    def test_emi_snapshot_is_observation_only_and_bounded(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_brom_board.c').read_text()
        publish = source[source.index('static int publish('):
                         source.index('int tetris_modem_brom_only_board(')]
        self.assertIn('unsigned char emi_record[64] = { 0 }', publish)
        self.assertIn('nothing,modem-brom-emi-report', publish)
        self.assertIn('tx->slot >= 32 && tx->slot <= 43', publish)
        self.assertIn('range->step < TETRIS_MODEM_EMI_STEPS', publish)
        self.assertIn('put32(emi_record + 4, loaded != NULL)', publish)
        self.assertNotIn('arm_smccc', publish)
        self.assertNotIn('ops->smc', publish)
        self.assertNotIn('readl(', publish)

    def test_bootm_uses_replaced_pointer(self):
        boot = (ROOT / 'arch/arm/lib/bootm.c').read_text()
        board = (ROOT / 'board/mediatek/mt6878/mt6878_tetris.c').read_text()
        self.assertIn('board_prep_linux(images);', boot)
        self.assertIn('(u64)images->ft_addr', boot)
        hook = board[board.index('ret = tetris_modem_brom_only_board(images);'):]
        self.assertIn('fdt = images->ft_addr;', hook)

    def test_current_fdt_size_is_owned(self):
        helper = (ROOT / 'board/mediatek/mt6878/tetris_linux_fdt_bounds.h').read_text()
        self.assertLess(helper.index('owned < sizeof(struct fdt_header)'),
                        helper.index('total = fdt_totalsize('))
        self.assertLess(helper.index('total > owned || total > dram'),
                        helper.index('fdt_check_full('))
        source = (ROOT / 'board/mediatek/mt6878/mt6878_tetris.c').read_text()
        hook = source[source.index('void board_prep_linux('):]
        self.assertLess(hook.index('tetris_linux_fdt_sync(images)'),
                        hook.index('tetris_scp_prepare_diagnostic(images, fdt)'))
        fixture = (ROOT / '.github/tests/tetris_modem_brom_board.c').read_text()
        self.assertIn('original, 50056', fixture)
        self.assertIn('images.ft_len == 53248', fixture)
        runner = (ROOT / '.github/tests/run_tetris_modem_brom_board.sh').read_text()
        self.assertLess(runner.index('if [ "${CI:-}" != true ]'), runner.index('cc=${HOSTCC:-cc}'))


if __name__ == '__main__':
    unittest.main()
