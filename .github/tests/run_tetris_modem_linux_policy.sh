#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
if [ "${CI:-}" != true ]; then
    printf '%s\n' 'Refusing native compilation outside CI.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-linux-policy.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$root"
"${HOSTCC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    .github/tests/tetris_modem_linux_policy.c -o "$work/test"
"$work/test"
