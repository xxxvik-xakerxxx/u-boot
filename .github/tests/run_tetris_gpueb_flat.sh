#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
if [ "${CI:-}" != true ]; then
    printf '%s\n' 'Native GPUEB retained metadata tests are CI-only.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-gpueb-flat.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc=${HOSTCC:-cc}
flags='-std=gnu11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
for source in fdt fdt_addresses fdt_empty_tree fdt_ro fdt_rw fdt_strerror fdt_sw fdt_wip; do
    "$cc" $flags -Wno-unused-parameter -I"$root/scripts/dtc/libfdt" \
        -c "$root/scripts/dtc/libfdt/$source.c" -o "$work/$source.o"
done
"$cc" $flags -I"$root/scripts/dtc/libfdt" \
    -c "$root/.github/tests/tetris_gpueb_flat_test.c" -o "$work/test.o"
"$cc" $flags -DTETRIS_GPUEB_HOST_TEST \
    -c "$root/board/mediatek/mt6878/tetris_gpueb_layout.c" -o "$work/layout.o"
"$cc" $flags "$work/"*.o -lcrypto -o "$work/flat"
"$work/flat"
