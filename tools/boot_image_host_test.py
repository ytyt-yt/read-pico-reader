#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：原开机图逐像素、四旋转及损坏压缩流回归。
English: Compare original splash pixels in four rotations and reject damaged compressed streams.
冻结：实际编码器/解码器，绘图硬件使用主机参考。/ Frozen: Real codec with a host drawing reference.
"""
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

from compress_boot_image import compress_image

ROOT = Path(__file__).resolve().parents[1]
STUB = """#pragma once
#include <stdint.h>
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *framebuffer);
"""
HARNESS = r'''
#include "ui_boot_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int width, height, rotation;
static size_t pixels_drawn;
int epd_rotated_display_width(void) { return rotation & 1 ? height : width; }
int epd_rotated_display_height(void) { return rotation & 1 ? width : height; }
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    assert(x >= 0 && x < epd_rotated_display_width());
    assert(y >= 0 && y < epd_rotated_display_height());
    int px = x, py = y;
    switch (rotation) {
        case 1: px = width - y - 1; py = x; break;
        case 2: px = width - x - 1; py = height - y - 1; break;
        case 3: px = y; py = height - x - 1; break;
    }
    uint8_t *out = fb + (size_t)py * (width / 2) + px / 2;
    if (px & 1) *out = (*out & 0x0f) | (color & 0xf0);
    else *out = (*out & 0xf0) | (color >> 4);
    ++pixels_drawn;
}
static uint8_t *read_file(const char *name, size_t *size) {
    FILE *file = fopen(name, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long length = ftell(file); assert(length >= 0);
    *size = (size_t)length; rewind(file);
    uint8_t *data = malloc(*size + 16); assert(data);
    assert(fread(data, 1, *size, file) == *size); assert(!fclose(file));
    memset(data + *size, 0xa5, 16); return data;
}
static void reject(uint8_t *frame, const uint8_t *encoded, size_t encoded_size, size_t bytes) {
    memset(frame, 0xa5, bytes + 16); pixels_drawn = 0;
    assert(!ui_boot_image_draw(frame, encoded, encoded_size)); assert(!pixels_drawn);
    for (size_t i = 0; i < bytes + 16; ++i) assert(frame[i] == 0xa5);
}
int main(int argc, char **argv) {
    assert(argc == 5); width = atoi(argv[3]); height = atoi(argv[4]);
    size_t raw_size, encoded_size;
    uint8_t *raw = read_file(argv[1], &raw_size), *encoded = read_file(argv[2], &encoded_size);
    assert(raw_size == (size_t)width * height / 2);
    assert(ui_boot_image_raw_size(encoded, encoded_size) == raw_size);
    uint8_t *frame = malloc(raw_size + 16), *reference = malloc(raw_size + 16);
    assert(frame && reference);
    clock_t started = clock();
    for (rotation = 0; rotation < 4; ++rotation) {
        memset(reference, 0xa5, raw_size + 16);
        int logical_width = epd_rotated_display_width();
        for (size_t i = 0; i < raw_size; ++i) {
            size_t pixel = i * 2;
            epd_draw_pixel(pixel % logical_width, pixel / logical_width, raw[i] << 4, reference);
            epd_draw_pixel(pixel % logical_width + 1, pixel / logical_width, raw[i] & 0xf0, reference);
        }
        memset(frame, 0xa5, raw_size + 16); pixels_drawn = 0;
        assert(ui_boot_image_draw(frame, encoded, encoded_size));
        assert(pixels_drawn == raw_size * 2 && !memcmp(frame, reference, raw_size + 16));
        reject(frame, NULL, encoded_size, raw_size);
        reject(frame, encoded, 0, raw_size);
        reject(frame, encoded, 11, raw_size);
        reject(frame, encoded, 12, raw_size);
        reject(frame, encoded, encoded_size - 1, raw_size);
        uint8_t saved = encoded[0]; encoded[0] ^= 1;
        assert(!ui_boot_image_raw_size(encoded, encoded_size));
        reject(frame, encoded, encoded_size, raw_size); encoded[0] = saved;
        saved = encoded[4]; encoded[4] ^= 1;
        reject(frame, encoded, encoded_size, raw_size); encoded[4] = saved;
        saved = encoded[8]; encoded[8] ^= 1;
        reject(frame, encoded, encoded_size, raw_size); encoded[8] = saved;
        saved = encoded[encoded_size - 1]; encoded[encoded_size - 1] ^= 1;
        reject(frame, encoded, encoded_size, raw_size); encoded[encoded_size - 1] = saved;
        encoded[encoded_size] = 0x80; encoded[encoded_size + 1] = 0;
        reject(frame, encoded, encoded_size + 2, raw_size);
        assert(!ui_boot_image_draw(NULL, encoded, encoded_size));
    }
    rotation = 0;
    int saved_width = width; width = 0;
    reject(frame, encoded, encoded_size, raw_size); width = saved_width - 1;
    reject(frame, encoded, encoded_size, raw_size); width = saved_width;
    int saved_height = height; height = -1;
    reject(frame, encoded, encoded_size, raw_size); height = saved_height;
    printf("PASS: %zu raw bytes, exact pixels in four rotations, unchanged guards/failure frame (%.3fs host)\n",
           raw_size, (double)(clock() - started) / CLOCKS_PER_SEC);
    free(frame); free(reference); free(raw); free(encoded);
}
'''


def unpack_image(encoded: bytes) -> bytes:
    magic, count, checksum = struct.unpack("<4sII", encoded[:12])
    assert magic == b"KBP1"
    output = bytearray()
    at = 12
    while at < len(encoded):
        control = encoded[at]
        at += 1
        length = (control & 0x7F) + 1
        if control & 0x80:
            output.extend(encoded[at:at + 1] * length)
            at += 1
        else:
            output.extend(encoded[at:at + length])
            at += length
    assert len(output) == count and zlib.crc32(output) == checksum
    return bytes(output)


def main() -> None:
    rng = random.Random(83021)
    for size in (1, 2, 3, 127, 128, 129, 255, 256, 257, 4096):
        for raw in (bytes([255]) * size, bytes(i % 256 for i in range(size)), rng.randbytes(size)):
            assert unpack_image(compress_image(raw)) == raw
    try:
        compress_image(b"")
        raise AssertionError("Empty source accepted")
    except ValueError:
        pass
    real = ROOT / "main/assets/loading_4bpp.bin"
    original = real.read_bytes()
    with tempfile.TemporaryDirectory(prefix="boot-image-") as directory:
        work = Path(directory)
        (work / "epdiy.h").write_text(STUB)
        (work / "test.c").write_text(HARNESS)
        binary = work / "test"
        subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I" + str(work),
                        "-I" + str(ROOT / "main/ui"), str(work / "test.c"),
                        str(ROOT / "main/ui/ui_boot_image.c"), "-lz", "-o", str(binary)], check=True)
        fixtures = [("real", original, 1216, 684),
                    ("white", bytes([255]) * 4800, 120, 80),
                    ("literal", rng.randbytes(4800), 120, 80),
                    ("mixed", (bytes(range(256)) + b"\x55" * 100) * 16, 178, 64),
                    ("tiny", b"\xaf\xbf", 2, 2)]
        for name, raw, width, height in fixtures:
            source = work / (name + ".raw")
            packed = work / (name + ".rle")
            source.write_bytes(raw)
            packed.write_bytes(compress_image(raw))
            assert unpack_image(packed.read_bytes()) == raw
            subprocess.run([str(binary), str(source), str(packed), str(width), str(height)], check=True)
        cli_output = work / "cli.rle"
        subprocess.run(["python3", str(ROOT / "tools/compress_boot_image.py"),
                        str(real), str(cli_output)], check=True)
        assert cli_output.read_bytes() == compress_image(original)
        refused = subprocess.run(["python3", str(ROOT / "tools/compress_boot_image.py"),
                                  str(work / "tiny.raw"), str(work / "tiny.raw")], capture_output=True)
        assert refused.returncode and (work / "tiny.raw").read_bytes() == b"\xaf\xbf"
        assert real.read_bytes() == original
    print(f"PASS: deterministic lossless build encoding, source preservation and codec boundaries; "
          f"splash {len(original)} -> {len(compress_image(original))} bytes")


if __name__ == "__main__":
    main()
