#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Offline modem signature checks, not rollback policy or permission to boot."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import tetris_scp_security as security

MAX_CONTAINER = 256 * 1024 * 1024
MAX_PAYLOAD = 64 * 1024 * 1024
NAMES = ("md1rom", "md1drdi", "md1dsp")


def verify(cert1, cert2, header, payload, root_pin):
    require = security.require
    require(len(header) == 512 and 0 < len(payload) <= MAX_PAYLOAD and
            len(payload) % 16 == 0, "invalid input extent")
    root, root_fields = security.certificate(cert1)
    leaf, fields = security.certificate(cert2)
    require(hashlib.sha256(security.encoder.encode(root[0][6])).digest() ==
            root_pin, "root pin mismatch")
    key = root_fields[security.PREFIX + "1.2"]
    require(security.encoder.encode(key) == security.encoder.encode(leaf[0][6]),
            "delegated key mismatch")
    security.verify_signature(root, security.public_key(root[0][6]))
    security.verify_signature(leaf, security.public_key(key))
    for suffix in ("2.6", "2.8", "4.2"):
        require(security.field_bits(fields, suffix, 1) == b"\0",
                "unsupported transform profile")
    require(security.field_bits(fields, "2.4", 32) ==
            hashlib.sha256(header).digest(), "header digest mismatch")
    require(security.field_bits(fields, "2.1", 32) ==
            hashlib.sha256(payload).digest(), "payload digest mismatch")


def components(data):
    """Keep each image with its adjacent certificates from one snapshot."""
    require = security.require
    require(len(data) <= MAX_CONTAINER, "oversized container")
    offset = 0
    pending = []
    found = {}
    for _ in range(128):
        if len(found) == len(NAMES):
            return found
        require(offset + 512 <= len(data), "truncated header")
        header = data[offset:offset + 512]
        magic, size = struct.unpack_from("<II", header)
        require(magic == 0x58881688 and
                struct.unpack_from("<II", header, 48) == (0x58891689, 512) and
                struct.unpack_from("<I", header, 68)[0] == 16,
                "unsupported member header")
        require(b"\0" in header[8:40], "unterminated member name")
        name = header[8:40].split(b"\0", 1)[0].decode("ascii")
        require(0 < size <= len(data) - offset - 512, "truncated payload")
        end = offset + 512 + size
        payload = data[offset + 512:end]
        offset = (end + 15) & ~15
        require(offset <= len(data), "truncated padding")
        if pending:
            expected = ("cert1md" if pending[0] == "md1rom" else "cert1") \
                if len(pending) == 3 else "cert2"
            require(name == expected and size <= security.MAX_CERT,
                    "missing adjacent certificate")
            pending.append(payload)
            if len(pending) == 5:
                found[pending[0]] = (pending[3], pending[4], pending[1], pending[2])
                pending = []
        elif name in NAMES:
            require(name not in found and size <= MAX_PAYLOAD, "invalid image member")
            pending = [name, header, payload]
    raise ValueError("header budget exhausted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--root-sha256", required=True,
                        help="independent root pin; image-derived pin proves only consistency")
    args = parser.parse_args()
    try:
        pin = bytes.fromhex(args.root_sha256)
        security.require(len(pin) == 32, "invalid root pin")
        with args.image.open("rb") as stream:
            data = stream.read(MAX_CONTAINER + 1)
        parts = components(data)
        for part in parts.values():
            verify(*part, pin)
    except Exception as error:
        parser.exit(1, f"Modem signature check failed ({type(error).__name__})\n")
    print(json.dumps({"container_sha256": hashlib.sha256(data).hexdigest(),
                      "components": list(parts), "signatures": "verified",
                      "rollback_and_device_policy": "not-checked",
                      "boot_ready": False}, indent=2))


if __name__ == "__main__":
    main()
