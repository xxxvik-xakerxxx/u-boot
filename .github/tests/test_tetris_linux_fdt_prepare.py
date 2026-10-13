#!/usr/bin/env python3
"""Source checks only; the real libfdt fixture is compiled in CI."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LateFdtTests(unittest.TestCase):
    def test_owned_copy_precedes_scp_and_pointer_is_refreshed(self):
        source = (ROOT / "board/mediatek/mt6878/mt6878_tetris.c").read_text()
        hook = source[source.index("void board_prep_linux("):]
        prepare = hook.index("tetris_linux_fdt_prepare(images)")
        self.assertLess(prepare, hook.index("tetris_observe_scp_region_info(fdt)"))
        self.assertLess(prepare, hook.index("tetris_scp_prepare_diagnostic(images, fdt)"))
        self.assertIn("fdt = images->ft_addr;", hook[prepare:hook.index("if (IS_ENABLED")])
        self.assertIn("panic(", hook[prepare:hook.index("if (IS_ENABLED")])

    def test_bounded_clone_is_committed_after_validation(self):
        source = (ROOT / "board/mediatek/mt6878/tetris_linux_fdt_prepare.h").read_text()
        self.assertLess(source.index("tetris_linux_fdt_sync(images)"),
                        source.index("images->ft_len >"))
        self.assertLess(source.index("fdt_add_mem_rsv(final, address, capacity)"),
                        source.index("images->ft_addr = final"))
        self.assertLess(source.index("fdt_check_full(final, capacity)"),
                        source.index("images->ft_addr = final"))
        self.assertIn("LMB_NOOVERWRITE | LMB_NONOTIFY", source)
        allocation = source[source.index("ret = lmb_alloc_mem"):source.index("if (ret)", source.index("ret = lmb_alloc_mem"))]
        self.assertNotIn("LMB_NOMAP", allocation)
        self.assertNotIn("writel", source)
        self.assertNotIn("arm_smccc", source)
        self.assertLess(source.index("if (!owned || !dram)"),
                        source.index("final = map_sysmem"))
        self.assertIn("address >= 0x80000000ULL", source)

    def test_ci_executes_real_libfdt_regression(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("sh .github/tests/run_tetris_linux_fdt_prepare.sh", workflow)
        runner = (ROOT / ".github/tests/run_tetris_linux_fdt_prepare.sh").read_text()
        self.assertLess(runner.index('if [ "${CI:-}" != true ]'),
                        runner.index("cc=${HOSTCC:-cc}"))
        fixture = (ROOT / ".github/tests/tetris_linux_fdt_prepare.c").read_text()
        self.assertIn("-FDT_ERR_NOSPACE", fixture)
        self.assertIn("nothing,scp-prepare-stage", fixture)
        self.assertIn("nothing,scp-secure-operation", fixture)


if __name__ == "__main__":
    unittest.main()
