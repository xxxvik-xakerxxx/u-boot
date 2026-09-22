#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/tetris-scp-tcm.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${HOSTCC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined "$root/.github/tests/tetris_scp_tcm.c" -o "$binary"
"$binary"
