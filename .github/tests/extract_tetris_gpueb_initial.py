#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Extract the actual board helper for the CI-only bundled-libfdt fixture.

Parent runner must invoke guarded --output before its FIRST compiler command,
including compilation of libfdt objects. Add its scratch directory to -I when
compiling tetris_gpueb_initial.c. This script never invokes a compiler or cpp.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'board/mediatek/mt6878/mt6878_tetris.c'
GUARD = '#if CONFIG_IS_ENABLED(TETRIS_GPUEB_FLAT_RETENTION_DIAGNOSTIC) || \\\n\tCONFIG_IS_ENABLED(TETRIS_GPUEB_TRANSFORM_DIAGNOSTIC)\n'
FUNCTIONS = ('gpueb_initial_power_capture', 'gpueb_initial_clear',
             'gpueb_initial_write', 'gpueb_initial_report_publish')


def require(value, message):
    if not value:
        raise ValueError(message)


def extract(source):
    marker = 'static u32 gpueb_initial_raw;'
    require(source.count(marker) == 1, 'expected one actual initial-report state')
    raw = source.index(marker)
    start = source.rfind(GUARD, 0, raw)
    require(start >= 0, 'actual helper diagnostic guard missing')
    require('#include "tetris_linux_fdt_prepare.h"' in source[:start],
            'native bounded-clone declarations must precede the helper')
    require(not re.search(r'^#\s*(if|ifdef|ifndef|endif)\b', source[start + len(GUARD):raw], re.M),
            'state is outside the selected diagnostic guard')
    depth, end = 0, None
    for match in re.finditer(r'^#\s*(if|ifdef|ifndef|endif)\b[^\n]*(?:\n|$)', source[start:], re.M):
        depth += -1 if match.group(1) == 'endif' else 1
        if depth == 0:
            end = start + match.end()
            break
    require(end is not None, 'unterminated initial-report diagnostic guard')
    require(re.match(r'\s*int board_late_init\(void\)\n\{', source[end:]),
            'helper does not end immediately before board_late_init')
    body = source[start:end]
    for name in FUNCTIONS:
        require(len(re.findall(r'^static (?:void|int) ' + name + r'\(', body, re.M)) == 1,
                'actual helper definition missing/duplicated: ' + name)
    require('tetris_linux_fdt_prepare(&candidate)' in body and
            'tetris_linux_fdt_sync(images)' in body, 'real bounded clone/sync path missing')
    require('board_late_init' not in body, 'caller accidentally included')
    return body


def ci_guard():
    require(os.environ.get('CI') == 'true' and os.environ.get('GITHUB_ACTIONS') == 'true',
            'CI-only extraction: CI=true and GITHUB_ACTIONS=true required before compilation')


def check_cache_scope(source):
    body = extract(source)
    calls = re.findall(r'\bflush_dcache_range\s*\((.*?)\);', body, re.S)
    calls = [re.sub(r'\s+', '', call) for call in calls]
    require(calls == [
        '(unsignedlong)final,(unsignedlong)final+capacity',
        '(unsignedlong)images->ft_addr,(unsignedlong)images->ft_addr+images->ft_len',
    ], 'initial report cache operation is not an owned DT range')
    require('flush_dcache_range' not in source.replace(body, '', 1),
            'unvalidated cache maintenance outside the DT report is enabled')


def emit(source, output):
    ci_guard()
    body = extract(source.read_text())
    resolved = output.resolve()
    require(resolved != ROOT and ROOT not in resolved.parents, 'generated include must be outside the checkout')
    require(output.name == 'tetris_gpueb_initial.inc', 'unexpected scratch include name')
    with output.open('x') as stream:
        stream.write(body)
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--check', action='store_true')
    action.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        body = extract(args.source.read_text()) if args.check else emit(args.source, args.output)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + '\n')
    print('Actual GPUEB initial helper SHA256=' + hashlib.sha256(body.encode()).hexdigest())


if __name__ == '__main__':
    main()
