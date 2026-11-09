#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：构建时无损压缩原始开机位图，不修改源资源。
English: Losslessly compress the raw boot bitmap at build time without changing its source.
冻结：仅字节游程编码，保留全部4bpp像素。/ Frozen: Byte runs only; retain every 4bpp pixel.
"""
import argparse
from pathlib import Path
import struct
import zlib


def compress_image(raw: bytes) -> bytes:
    if not raw or len(raw) > 0xFFFFFFFF:
        raise ValueError("Boot image must contain 1..UINT32_MAX bytes")
    encoded = bytearray(struct.pack("<4sII", b"KBP1", len(raw), zlib.crc32(raw)))
    at = 0
    while at < len(raw):
        run = 1
        while run < 128 and at + run < len(raw) and raw[at + run] == raw[at]:
            run += 1
        if run >= 3:
            encoded.extend((0x80 | (run - 1), raw[at]))
            at += run
            continue
        start = at
        at += run
        while at < len(raw) and at - start < 128:
            run = 1
            while run < 3 and at + run < len(raw) and raw[at + run] == raw[at]:
                run += 1
            if run >= 3:
                break
            at += min(run, 128 - (at - start))
        encoded.append(at - start - 1)
        encoded.extend(raw[start:at])
    return bytes(encoded)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Output must differ from the raw source")
    packed = compress_image(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(packed)


if __name__ == "__main__":
    main()
