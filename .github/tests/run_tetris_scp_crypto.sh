#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/tetris-scp-crypto.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${HOSTCC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/.github/tests/tetris_scp_crypto.c" -o "$binary"
"$binary"
