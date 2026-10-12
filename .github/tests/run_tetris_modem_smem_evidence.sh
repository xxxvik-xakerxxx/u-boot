#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
if [ "${CI:-}" != true ] || [ "${GITHUB_ACTIONS:-}" != true ]; then
    printf '%s\n' 'Refusing native compilation outside GitHub CI.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-smem-evidence.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$root"
"${HOSTCC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -fno-omit-frame-pointer -DTETRIS_MODEM_LAYOUT_HOST_TEST \
    board/mediatek/mt6878/tetris_modem_layout.c \
    board/mediatek/mt6878/tetris_modem_emi.c \
    board/mediatek/mt6878/tetris_modem_smem_evidence.c \
    .github/tests/tetris_modem_smem_evidence.c -o "$work/test"
"$work/test"
