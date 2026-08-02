#!/usr/bin/env python3
"""Build a flashable Nothing CMF Phone 1 LK image from a U-Boot payload."""

from pathlib import Path
import argparse
import struct

LK_HEADER_SIZE = 0x200
LK_MAGIC = 0x58881688


def parse_byte(value: str) -> int:
    byte = int(value, 0)
    if not 0 <= byte <= 0xff:
        raise argparse.ArgumentTypeError("byte value must be in range 0..255")
    return byte


def trim_trailing(data: bytes, fill: int, align: int) -> bytes:
    end = len(data)
    while end and data[end - 1] == fill:
        end -= 1

    if align:
        end = (end + align - 1) // align * align

    return data[:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--payload", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--trim-fill", type=parse_byte)
    parser.add_argument("--payload-align", type=int, default=16)
    parser.add_argument("--align", type=int, default=512)
    args = parser.parse_args()

    template = args.template.read_bytes()
    payload = args.payload.read_bytes()

    if args.payload_align <= 0:
        raise SystemExit("payload alignment must be positive")
    padding = (-len(payload)) % args.payload_align
    if padding:
        payload += bytes(padding)

    if len(template) < LK_HEADER_SIZE:
        raise SystemExit("template is shorter than the LK header")

    magic, old_size = struct.unpack_from("<II", template, 0)
    if magic != LK_MAGIC:
        raise SystemExit(f"template is not an LK image: magic={magic:#x}")
    if len(template) < LK_HEADER_SIZE + old_size:
        raise SystemExit("template is shorter than its LK payload size")

    header = bytearray(template[:LK_HEADER_SIZE])
    struct.pack_into("<I", header, 4, len(payload))

    tail = template[LK_HEADER_SIZE + old_size :]
    image = bytes(header) + payload + tail
    if args.trim_fill is not None:
        image = trim_trailing(image, args.trim_fill, args.align)

    args.output.write_bytes(image)

    print(f"template_size={len(template)}")
    print(f"old_payload_size={old_size}")
    print(f"new_payload_size={len(payload)}")
    print(f"payload_padding={padding}")
    print(f"tail_size={len(tail)}")
    print(f"output_size={len(image)}")


if __name__ == "__main__":
    main()
