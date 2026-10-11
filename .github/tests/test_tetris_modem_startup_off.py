#!/usr/bin/env python3
"""Actual opt-in startup helpers; C compilation/execution is GitHub CI-only."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from test_tetris_modem_power_ack import function

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'board/mediatek/mt6878'


def sources():
    source = (BOARD / 'tetris_modem_loaded_boot.c').read_text()
    header = (BOARD / 'tetris_modem_loaded_boot.h').read_text()
    bootstrap = (BOARD / 'tetris_modem_bootstrap.h').read_text()
    config = (ROOT / 'arch/arm/mach-mediatek/Kconfig').read_text()
    policy = config.split('config TETRIS_MODEM_STARTUP_OFF_DIAGNOSTIC\n', 1)[1].split('\nconfig ', 1)[0]
    assert 'depends on TETRIS_MODEM_BROM_ONLY' in policy and 'default n' in policy
    call = source.index('ret = startup_off();')
    assert source.index('owner.attempted = 1;') < source.index('tetris_scp_check_atf_profile(dev)') < call
    assert call < source.index('ret = cold_off();') < source.index('tetris_modem_reserve_diagnostic_window(')
    assert source.count('ret = startup_off();') == 1
    assert '#if IS_ENABLED(CONFIG_TETRIS_MODEM_STARTUP_OFF_DIAGNOSTIC)' in source
    stages = re.findall(r'enum tetris_modem_loaded_stage \{.*?\n\};', header, re.S)
    assert len(stages) == 1
    assert stages[0].index('TETRIS_MD_LOAD_COMPLETE') < stages[0].index('TETRIS_MD_LOAD_STARTUP_STATE')
    ack = re.findall(r'^#define TETRIS_MD_POWER_ACK \(1U << 30\)$', bootstrap, re.M)
    assert len(ack) == 1
    helpers = [function(source, name) for name in ('fail', 'startup_read', 'startup_wait', 'startup_off')]
    assert all('arm_smccc' not in h and 'md_boot_cleanup' not in h for h in helpers)
    return '\n'.join([stages[0], *ack, *helpers])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-ci', action='store_true')
    args = parser.parse_args()
    code = sources()
    if not args.native_ci:
        print('actual startup-OFF admission/order source checks PASS; no C build')
        return
    if os.environ.get('CI') != 'true' or os.environ.get('GITHUB_ACTIONS') != 'true':
        parser.error('native compilation/execution is GitHub CI-only')
    with tempfile.TemporaryDirectory(prefix='tetris-md-startup-off-') as temporary:
        work = Path(temporary)
        (work / 'producer.inc').write_text(code)
        target = work / 'test'
        subprocess.run([os.environ.get('HOSTCC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                        '-fno-omit-frame-pointer', '-I', str(work),
                        str(ROOT / '.github/tests/tetris_modem_startup_off.c'), '-o', str(target)],
                       check=True, timeout=60)
        subprocess.run([str(target)], check=True, timeout=20)


if __name__ == '__main__':
    main()
