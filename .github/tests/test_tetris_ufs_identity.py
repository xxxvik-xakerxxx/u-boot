#!/usr/bin/env python3
"""Actual UFS producer fixture; C compilation is GitHub CI-only."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def body():
    source = (ROOT / 'drivers/ufs/ufs-uclass.c').read_text()
    matches = re.findall(r'^int ufs_read_lun_boot_identity\([^;]+?\n\{\n.*?^\}',
                         source, re.M | re.S)
    if len(matches) != 1:
        raise ValueError('expected one actual UFS identity producer')
    return matches[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native-ci', action='store_true')
    args = parser.parse_args()
    function = body()
    header = (ROOT / 'drivers/ufs/ufs.h').read_text()
    assert re.search(r'^#define UFS_MAX_LUNS\s+0x7F$', header, re.M)
    for name, value in (('UPIU_QUERY_OPCODE_READ_ATTR', 3),
                        ('UPIU_QUERY_OPCODE_READ_DESC', 1),
                        ('QUERY_ATTR_IDN_BOOT_LU_EN', 0),
                        ('QUERY_DESC_IDN_UNIT', 2)):
        match = re.search(r'\b' + name + r'\s*=\s*(0x[0-9a-fA-F]+|\d+)', header)
        assert match and int(match[1], 0) == value, name
    assert '(unit[3] != 1 && unit[3] != 2)' in function
    assert 'unit[2] != lun' in function and 'unit[4] > 2' in function
    assert 'UPIU_QUERY_OPCODE_READ_ATTR' in function
    assert 'UPIU_QUERY_OPCODE_READ_DESC' in function
    if not args.native_ci:
        print('actual UFS identity source checks PASS; no C build')
        return
    if os.environ.get('CI') != 'true' or os.environ.get('GITHUB_ACTIONS') != 'true':
        parser.error('native compilation is GitHub CI-only')
    with tempfile.TemporaryDirectory(prefix='tetris-ufs-identity-') as temporary:
        work = Path(temporary)
        (work / 'producer.inc').write_text(function + '\n')
        target = work / 'test'
        subprocess.run([os.environ.get('HOSTCC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                        '-fno-omit-frame-pointer', '-I', str(work),
                        str(ROOT / '.github/tests/tetris_ufs_identity.c'), '-o', str(target)],
                       check=True, timeout=60)
        subprocess.run([str(target)], check=True, timeout=20)


if __name__ == '__main__':
    main()
