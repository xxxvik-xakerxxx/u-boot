#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tetris-scp-security.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/include/linux"
for header in asn1.h asn1_decoder.h asn1_ber_bytecode.h; do
    ln -s "$root/include/linux/$header" "$work/include/linux/$header"
done
board="$root/board/mediatek/mt6878"
cc=${HOSTCC:-cc}
"$cc" -I"$work/include" "$root/tools/asn1_compiler.c" -o "$work/compiler"
"$work/compiler" "$board/tetris_scp_fields.asn1" \
    "$work/tetris_scp_fields.asn1.c" "$work/tetris_scp_fields.asn1.h"
"$cc" -std=gnu11 -Wall -Wextra -Wno-unused-but-set-variable \
    -Wno-unused-variable -include stddef.h -shared -fPIC \
    -DTETRIS_SCP_SECURITY_HOST_TEST -DTETRIS_SCP_CRYPTO_HOST_TEST \
    -I"$work/include" -I"$root/.github/tests/asn1-host" -I"$work" \
    "$root/lib/asn1_decoder.c" "$work/tetris_scp_fields.asn1.c" \
    "$board/tetris_scp_security.c" "$board/tetris_scp_crypto.c" -o "$work/security.so"
"${PYTHON:-python3}" "$root/.github/tests/test_tetris_scp_security_c.py" "$work/security.so"
