#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Source/extraction checks only. Do not compile or execute the C fixture here."""
import ast
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import extract_tetris_gpueb_initial as extractor

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


class InitialSource(unittest.TestCase):
    def test_single_object_ci_goals_do_not_race_shared_dependency_files(self):
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('make O=build-initramfs-only -j1', workflow)
        objects = workflow.split('make O=build-initramfs-only -j1', 1)[1].split('- name:', 1)[0]
        self.assertIn('arch/arm/lib/save_prev_bl_data.o', objects)
        self.assertIn('board/mediatek/mt6878/mt6878_tetris.o', objects)
        self.assertIn('make O=build-initramfs-only -j"$(nproc)" \\\n'
                      '            CROSS_COMPILE=aarch64-linux-gnu- tools', workflow)

    @classmethod
    def setUpClass(cls):
        cls.source = extractor.SOURCE.read_text()
        cls.body = extractor.extract(cls.source)
        cls.fixture = (HERE / 'tetris_gpueb_initial.c').read_text()

    def test_extracts_actual_guarded_body_verbatim(self):
        start = self.source.index(self.body)
        self.assertTrue(self.body.startswith(extractor.GUARD))
        self.assertTrue(self.body.endswith('#endif\n'))
        self.assertEqual(self.body, self.source[start:start + len(self.body)])
        self.assertRegex(self.source[start + len(self.body):], r'^\s*int board_late_init\(void\)')

    def test_fixture_has_no_copied_publication_or_capture_model(self):
        self.assertIn('#include "tetris_gpueb_initial.inc"', self.fixture)
        for name in extractor.FUNCTIONS:
            self.assertNotRegex(self.fixture, r'static\s+(?:void|int)\s+' + name + r'\s*\(')
        self.assertNotRegex(self.fixture, r'static\s+int\s+tetris_linux_fdt_(?:prepare|sync)\s*\(')

    def test_actual_clone_header_precedes_extracted_helper(self):
        include = '#include "../../board/mediatek/mt6878/tetris_linux_fdt_prepare.h"'
        self.assertLess(self.fixture.index(include), self.fixture.index('#include "tetris_gpueb_initial.inc"'))
        native_include = '#include "tetris_linux_fdt_prepare.h"'
        self.assertLess(self.source.index(native_include), self.source.index(self.body))

    def test_extractor_rejects_missing_native_header(self):
        changed = self.source.replace('#include "tetris_linux_fdt_prepare.h"', '')
        with self.assertRaisesRegex(ValueError, 'declarations must precede'):
            extractor.extract(changed)

    def test_extractor_rejects_duplicate_state(self):
        with self.assertRaisesRegex(ValueError, 'one actual'):
            extractor.extract(self.source + '\nstatic u32 gpueb_initial_raw;\n')

    def test_extractor_rejects_missing_definition_and_boundary(self):
        for name in extractor.FUNCTIONS:
            changed = self.source.replace('static int ' + name + '(', 'static int altered(', 1)
            changed = changed.replace('static void ' + name + '(', 'static void altered(', 1)
            with self.assertRaisesRegex(ValueError, 'definition missing'):
                extractor.extract(changed)
        with self.assertRaisesRegex(ValueError, 'immediately before'):
            extractor.extract(self.source.replace('int board_late_init(void)', 'int altered(void)', 1))

    def test_extractor_rejects_missing_outer_guard(self):
        start = self.source.index(self.body)
        changed = self.source[:start] + self.source[start:].replace(extractor.GUARD, '', 1)
        with self.assertRaisesRegex(ValueError, 'diagnostic guard missing'):
            extractor.extract(changed)

    def test_both_ci_guards_precede_output_and_source_access(self):
        with tempfile.TemporaryDirectory(prefix='gpueb-initial-guard-') as tmp:
            output = Path(tmp) / 'tetris_gpueb_initial.inc'
            for ci, github in (('', ''), ('true', ''), ('', 'true'), ('1', 'true'),
                               ('true', '1'), ('false', 'true'), ('true', 'false')):
                with patch.dict(os.environ, CI=ci, GITHUB_ACTIONS=github), \
                        patch.object(Path, 'read_text', side_effect=AssertionError('read before guards')):
                    with self.assertRaisesRegex(ValueError, 'CI-only'):
                        extractor.emit(Path('/missing'), output)
                self.assertFalse(output.exists())

    def test_guarded_emit_is_verbatim_and_never_overwrites(self):
        with tempfile.TemporaryDirectory(prefix='gpueb-initial-emit-') as tmp:
            output = Path(tmp) / 'tetris_gpueb_initial.inc'
            with patch.dict(os.environ, CI='true', GITHUB_ACTIONS='true'):
                body = extractor.emit(extractor.SOURCE, output)
                self.assertEqual(output.read_text(), self.body)
                self.assertEqual(body, self.body)
                with self.assertRaises(FileExistsError):
                    extractor.emit(extractor.SOURCE, output)

    def test_in_checkout_output_is_rejected(self):
        with patch.dict(os.environ, CI='true', GITHUB_ACTIONS='true'):
            with self.assertRaisesRegex(ValueError, 'outside the checkout'):
                extractor.emit(extractor.SOURCE, HERE / 'tetris_gpueb_initial.inc')

    def test_source_check_cli_never_compiles(self):
        result = subprocess.run([sys.executable, '-B', str(HERE / 'extract_tetris_gpueb_initial.py'), '--check'],
                                env={**os.environ, 'CI': '', 'GITHUB_ACTIONS': ''},
                                capture_output=True, text=True, check=True)
        self.assertIn(hashlib.sha256(self.body.encode()).hexdigest(), result.stdout)
        code = (HERE / 'extract_tetris_gpueb_initial.py').read_text()
        imports = [node for node in ast.walk(ast.parse(code)) if isinstance(node, (ast.Import, ast.ImportFrom))]
        names = [node.module if isinstance(node, ast.ImportFrom) else alias.name
                 for node in imports for alias in (node.names if isinstance(node, ast.Import) else [None])]
        self.assertNotIn('subprocess', names)
        self.assertNotIn('os.system(', code)
        self.assertNotIn('os.exec', code)
        self.assertIn('FIRST compiler command', code)

    def test_real_libfdt_no_success_substitutes(self):
        self.assertIn('#include <libfdt.h>', self.fixture)
        self.assertIn('return fdt_open_into(source, target, size)', self.fixture)
        self.assertIn('return fdt_delprop(fdt, node, name)', self.fixture)
        self.assertIn('ret = fdt_setprop_u32(fdt, node, name, value)', self.fixture)
        self.assertIn('assert(!ret && !fdt_pack(packed))', self.fixture)
        runner = (HERE / 'run_tetris_linux_fdt_prepare.sh').read_text()
        for unit in ('fdt', 'fdt_addresses', 'fdt_empty_tree', 'fdt_ro', 'fdt_rw', 'fdt_strerror', 'fdt_sw', 'fdt_wip'):
            self.assertIn(unit, runner)
        self.assertIn('-fsanitize=address,undefined', runner)

    def test_capture_once_zero_and_nonzero_scenarios_are_present(self):
        for call in ('test_capture_and_commit(0)', 'test_capture_and_commit(0xc0002000U)',
                     'test_capture_and_commit(0x00001000U)'):
            self.assertIn(call, self.fixture)
        self.assertIn('assert(reads == 1 && gpueb_initial_raw == word)', self.fixture)
        self.assertIn('input_word = ~word', self.fixture)
        self.assertIn('gpueb_initial_power_capture();', self.fixture)

    def test_stale_removal_version_last_missing_sample_assertions(self):
        self.assertIn('if (!(deletes % 7))', self.fixture)
        self.assertIn('assert(last_write && !strcmp(last_write, gpueb_initial_properties[0]))', self.fixture)
        self.assertIn('assert(versions == 1)', self.fixture)
        self.assertIn('assert_report(images.ft_addr, 0, -ENODATA)', self.fixture)
        self.assertIn('assert(len == -FDT_ERR_NOTFOUND)', self.fixture)

    def test_allocator_errno_fallback_does_not_publish_raw(self):
        self.assertIn('return -ENOMEM', self.fixture)
        self.assertIn('gpueb_initial_report_publish(&images) == -ENOMEM', self.fixture)
        self.assertIn('assert_report(images.ft_addr, 0, -ENOMEM)', self.fixture)
        self.assertIn('assert(!maps && !unmaps && !frees && flushes == 1)', self.fixture)

    def test_valid_small_clone_real_nospace_release_and_first_error(self):
        self.assertIn('small = fdt_totalsize(packed) + 2 * sizeof(uint64_t)', self.fixture)
        self.assertIn('ret = fdt_open_into(packed, target, small)', self.fixture)
        self.assertIn('gpueb_initial_report_publish(&images) == -FDT_ERR_NOSPACE', self.fixture)
        self.assertIn('assert_report(images.ft_addr, 0, -FDT_ERR_NOSPACE)', self.fixture)
        self.assertIn('maps == 1 && unmaps == 1 && frees == 1 && flushes == 2', self.fixture)

    def test_success_pointer_commit_and_original_preservation(self):
        self.assertIn('images.ft_addr == allocation && images.ft_addr != original', self.fixture)
        self.assertIn('assert(!memcmp(original, before, sizeof(original)))', self.fixture)
        self.assertIn('assert(active_images->ft_addr == original_address)', self.fixture)
        self.assertIn('assert_unrelated(images.ft_addr)', self.fixture)

    def test_secondary_real_badmagic_does_not_replace_nospace(self):
        self.assertIn('fdt_set_magic(original_address, 0)', self.fixture)
        fault = self.fixture.split('static void test_secondary_error_preserves_first(void)', 1)[1].split('int main(void)', 1)[0]
        self.assertIn('gpueb_initial_report_publish(&images) == -FDT_ERR_NOSPACE', fault)
        self.assertIn('fdt_check_full(original, sizeof(original)) == -FDT_ERR_BADMAGIC', fault)
        self.assertIn('assert(deletes == 7 && !writes && !versions)', fault)

    def test_native_failure_ret_and_erase_paths_are_not_replaced(self):
        publish = self.body[self.body.index('static int gpueb_initial_report_publish('):]
        self.assertLess(publish.index('fdt_check_full(final, capacity)'), publish.index('*images = candidate'))
        self.assertIn('lmb_free(address, capacity, LMB_NOOVERWRITE | LMB_NONOTIFY)', publish)
        self.assertIn('node, 0, ret)', publish)
        self.assertIn('return ret;', publish)
        self.assertNotIn('writel(', self.body)
        self.assertNotIn('arm_smccc', self.body)

    def test_fixture_rejects_non_ci_execution_as_extra_defense(self):
        self.assertIn('getenv("CI")', self.fixture)
        self.assertIn('getenv("GITHUB_ACTIONS")', self.fixture)
        main = self.fixture[self.fixture.index('int main(void)'):]
        self.assertLess(main.index('strcmp(ci, "true")'), main.index('test_capture_and_commit(0)'))
        self.assertIn('not hardware validation', self.fixture)


if __name__ == '__main__':
    unittest.main()
