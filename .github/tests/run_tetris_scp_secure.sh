#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/tetris-scp-secure.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${HOSTCC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined "$root/.github/tests/tetris_scp_secure.c" -o "$binary"
"$binary"
