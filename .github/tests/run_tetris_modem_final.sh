#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
if [ "${CI:-}" != true ] || [ "${GITHUB_ACTIONS:-}" != true ]; then
    printf '%s\n' 'Refusing native compilation outside GitHub CI.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-modem-final.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$root"
cc=${HOSTCC:-cc}
flags='-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'
for source in fdt fdt_addresses fdt_empty_tree fdt_ro fdt_rw fdt_strerror fdt_sw fdt_wip; do
    "$cc" $flags -Wno-unused-parameter -Iscripts/dtc/libfdt \
        -c "scripts/dtc/libfdt/$source.c" -o "$work/$source.o"
done
"$cc" $flags -Iscripts/dtc/libfdt -c .github/tests/tetris_modem_final.c -o "$work/test.o"
"$cc" $flags "$work"/*.o -o "$work/test"
"$work/test"
