#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
# CI-only native compilation; no handset access or hardware execution.
set -eu
if [ "${CI:-}" != true ]; then
    printf '%s\n' 'Refusing native compilation outside CI (CI=true required).' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-brom-board.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$root"
cc=${HOSTCC:-cc}
flags='-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'
# Existing upstream libfdt parameter warnings are isolated from our TU.
for source in fdt fdt_addresses fdt_empty_tree fdt_ro fdt_rw fdt_strerror fdt_sw fdt_wip; do
    "$cc" $flags -Wno-unused-parameter -Iscripts/dtc/libfdt \
        -c "scripts/dtc/libfdt/$source.c" -o "$work/$source.o"
done
"$cc" $flags -Iscripts/dtc/libfdt -c .github/tests/tetris_modem_brom_board.c -o "$work/test.o"
"$cc" $flags "$work"/*.o -o "$work/test"
"$work/test"
