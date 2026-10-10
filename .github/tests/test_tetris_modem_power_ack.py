#!/usr/bin/env python3
"""Extract actual MD observation/poll helpers; native compilation is CI-only."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'board/mediatek/mt6878'


def function(source, name):
    matches = re.findall(r'^static (?:unsigned int|int) ' + re.escape(name)
                         + r'\([^;]+?\n\{\n.*?^\}', source, re.M | re.S)
    if len(matches) != 1:
        raise ValueError('expected one production helper: ' + name)
    return matches[0] + '\n'


def sources():
    header = (BOARD / 'tetris_modem_bootstrap.h').read_text()
    bootstrap = (BOARD / 'tetris_modem_bootstrap.c').read_text()
    loaded = (BOARD / 'tetris_modem_loaded_boot.c').read_text()
    ack = re.findall(r'^#define TETRIS_MD_POWER_ACK \(1U << 30\)$', header, re.M)
    assert len(ack) == 1
    assert '#define MD_ACK TETRIS_MD_POWER_ACK' in bootstrap
    assert '4U | TETRIS_MD_POWER_ACK' in function(loaded, 'cold_off')
    assert 'md_boot_wait(MD_POWER, MD_ON | MD_ACK, MD_ON | MD_ACK)' in bootstrap
    assert 'md_cleanup_wait(MD_POWER, MD_ON | MD_ACK, 0)' in bootstrap
    assert 'MD_ON | MD_ACK, 3, 0x200, 0x800, 0xc0' in function(bootstrap, 'md_boot_cold_off')
    # The inherited ON gate still precedes every firmware reservation/write.
    assert loaded.index('ret = cold_off();') < loaded.index('tetris_modem_reserve_diagnostic_window(')
    helpers = [function(bootstrap, name) for name in
               ('md_cleanup_read', 'md_cleanup_wait', 'md_boot_read', 'md_boot_wait', 'md_boot_cold_off')]
    helpers.append(function(loaded, 'cold_off'))
    assert all('writel(' not in item for item in helpers)
    macros = re.findall(r'^#define MD_\w+[^\n]+$', bootstrap, re.M)
    return '\n'.join(ack + macros + helpers)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-ci', action='store_true')
    args = parser.parse_args()
    source = sources()
    if not args.native_ci:
        print('actual MD ACK30 source checks PASS; no C build')
        return
    if os.environ.get('CI') != 'true' or os.environ.get('GITHUB_ACTIONS') != 'true':
        parser.error('native compilation is GitHub CI-only')
    with tempfile.TemporaryDirectory(prefix='tetris-md-power-ack-') as temporary:
        work = Path(temporary)
        (work / 'producer.inc').write_text(source)
        target = work / 'test'
        subprocess.run([os.environ.get('HOSTCC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                        '-fno-omit-frame-pointer', '-I', str(work),
                        str(ROOT / '.github/tests/tetris_modem_power_ack.c'), '-o', str(target)],
                       check=True, timeout=60)
        subprocess.run([str(target)], check=True, timeout=20)


if __name__ == '__main__':
    main()
