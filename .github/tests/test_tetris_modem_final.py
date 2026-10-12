#!/usr/bin/env python3
"""Source-only final publication guards; C transactions run in GitHub CI."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FinalMetadataTests(unittest.TestCase):
    def test_success_only_hook(self):
        board = (ROOT / 'board/mediatek/mt6878/mt6878_tetris.c').read_text()
        start = board.index('ret = tetris_modem_brom_only_board(images);')
        hook = board[start:board.index('if (IS_ENABLED(CONFIG_TETRIS_MODEM_LOAD_DIAGNOSTIC))', start)]
        self.assertIn('if (!ret && IS_ENABLED(CONFIG_TETRIS_MODEM_METADATA_HANDOFF))', hook)
        self.assertEqual(hook.count('tetris_modem_publish_final(images)'), 1)
        self.assertIn('fdt = images->ft_addr;', hook[hook.index('tetris_modem_publish_final(images)'):])
        kconfig = (ROOT / 'arch/arm/mach-mediatek/Kconfig').read_text()
        option = kconfig.split('config TETRIS_MODEM_METADATA_HANDOFF\n')[1].split('\nconfig ')[0]
        self.assertIn('depends on TETRIS_MODEM_BROM_ONLY', option)
        self.assertIn('default n', option)

    def test_disabled_consumer_and_private_capsule(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_final.c').read_text()
        self.assertIn('memcmp(property, "disabled", 9)', source)
        self.assertIn('if (!IS_ENABLED(CONFIG_TETRIS_MODEM_METADATA_HANDOFF))', source)
        self.assertIn('LMB_MEM_ALLOC_MAX, TAG_CAPACITY, &tag_address', source)
        self.assertIn('tetris_modem_loaded_encode_tags(tags, TAG_CAPACITY)', source)
        self.assertIn('tetris_modem_loaded_encode_smem_evidence(evidence, sizeof(evidence))', source)
        self.assertIn('"nothing,modem-smem-boot-evidence"', source)
        self.assertNotIn('"okay"', source)
        for forbidden in ('readl(', 'writel(', 'arm_smccc', 'ccci_tetris_register', 'pm_runtime'):
            self.assertNotIn(forbidden, source)
        loader = (ROOT / 'board/mediatek/mt6878/tetris_modem_loaded_boot.c').read_text()
        encode = loader[loader.index('int tetris_modem_loaded_encode_tags('):]
        self.assertIn('owner.report.stage != TETRIS_MD_LOAD_COMPLETE', encode)
        self.assertIn('if (owner.report.error)', encode)

    def test_atomic_cache_cleaned_publication(self):
        source = (ROOT / 'board/mediatek/mt6878/tetris_modem_final.c').read_text()
        commit = source.index('images->ft_addr = final;')
        self.assertLess(source.index('no_map_reservation(final, tag_address)'), commit)
        self.assertLess(source.index('flush_dcache_range((unsigned long)tags'), commit)
        self.assertLess(source.index('flush_dcache_range((unsigned long)final'), commit)
        cleanup = source[source.index('fail:'):]
        self.assertIn('if (!first_error)', cleanup)
        self.assertNotIn('images->ft_addr =', cleanup)
        self.assertNotIn('tetris_modem_loaded_encode_tags', cleanup)
        runner = (ROOT / '.github/tests/run_tetris_modem_final.sh').read_text()
        self.assertLess(runner.index('"${GITHUB_ACTIONS:-}"'), runner.index('cc=${HOSTCC:-cc}'))


if __name__ == '__main__':
    unittest.main()
