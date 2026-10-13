#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
if [ "${CI:-}" != true ]; then
    printf '%s\n' 'Refusing native compilation outside CI (CI=true required).' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-late-fdt.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$root"
cc=${HOSTCC:-cc}
flags='-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'
for source in fdt fdt_addresses fdt_empty_tree fdt_ro fdt_rw fdt_strerror fdt_sw fdt_wip; do
    "$cc" $flags -Wno-unused-parameter -Iscripts/dtc/libfdt \
        -c "scripts/dtc/libfdt/$source.c" -o "$work/$source.o"
done
for fixture in tetris_linux_fdt_prepare tetris_gpueb_final; do
    "$cc" $flags -Iscripts/dtc/libfdt -c ".github/tests/$fixture.c" -o "$work/test.o"
    "$cc" $flags "$work"/*.o -o "$work/test"
    "$work/test"
done
