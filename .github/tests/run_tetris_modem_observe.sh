#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/tetris-modem-observe.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
cd "$root"
"${HOSTCC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iscripts/dtc/libfdt .github/tests/tetris_modem_observe.c \
    scripts/dtc/libfdt/fdt.c scripts/dtc/libfdt/fdt_addresses.c \
    scripts/dtc/libfdt/fdt_empty_tree.c scripts/dtc/libfdt/fdt_ro.c \
    scripts/dtc/libfdt/fdt_rw.c scripts/dtc/libfdt/fdt_strerror.c \
    scripts/dtc/libfdt/fdt_sw.c scripts/dtc/libfdt/fdt_wip.c -o "$binary"
"$binary"
