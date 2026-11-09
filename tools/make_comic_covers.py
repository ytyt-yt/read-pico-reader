#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：为comics两层目录批量制作严格灰阶封面，默认保留已有文件。
English: Prepare strict grayscale covers for a two-level comics tree, preserving existing files by default.
冻结：不改CBZ或原图；176×240、8位灰阶、无透明、非交错PNG。
Frozen: Never change CBZ/source images; output 176x240 opaque, noninterlaced 8-bit gray PNG.
"""
import argparse
import io
import os
from pathlib import Path
import re
import tempfile
import zipfile

from PIL import Image, ImageOps

SIZE = (176, 240)


def natural(name):
    return tuple((1, int(piece), len(piece)) if piece.isdigit() else (0, piece.casefold())
                 for piece in re.split(r"(\d+)", name)) + ((0, name),)


def first_image(path):
    with zipfile.ZipFile(path) as archive:
        entries = [entry for entry in archive.infolist() if not entry.is_dir()
                   and all(part and not part.startswith(".") for part in entry.filename.split("/"))
                   and Path(entry.filename).suffix.lower() in (".webp", ".jpg", ".jpeg", ".png")]
        if len(archive.infolist()) > 8192:
            raise ValueError("archive exceeds the device's entry limit")
        for entry in sorted(entries, key=lambda item: natural(item.filename)):
            if entry.file_size > 8 * 1024 * 1024 or entry.flag_bits & 1:
                continue
            try:
                with Image.open(io.BytesIO(archive.read(entry))) as image:
                    if getattr(image, "is_animated", False) or max(image.size) > 8192 or image.width * image.height > 16_000_000:
                        continue
                    image.load()
                    return image.copy()
            except (OSError, ValueError, zipfile.BadZipFile, RuntimeError):
                continue
    raise ValueError("no usable static image")


def cover_image(image):
    image = ImageOps.exif_transpose(image)
    if image.mode in ("RGBA", "LA") or "transparency" in image.info:
        rgba = image.convert("RGBA")
        background = Image.new("RGBA", image.size, "white")
        image = Image.alpha_composite(background, rgba).convert("RGB")
    return ImageOps.fit(image.convert("L"), SIZE, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))


def save_cover(image, target, overwrite=False):
    if target.exists() and not overwrite:
        return False
    fd, temporary = tempfile.mkstemp(prefix=".cover-", suffix=".png", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            cover_image(image).save(output, format="PNG", interlace=0)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return True


def prepare(root, overwrite=False):
    errors = 0
    for series in sorted(root.iterdir(), key=lambda path: natural(path.name)):
        if not series.is_dir() or series.name.startswith("."):
            continue
        chapters = sorted((path for path in series.iterdir() if path.is_file()
                           and not path.name.startswith(".") and path.suffix.lower() == ".cbz"),
                          key=lambda path: natural(path.name))
        for chapter in chapters:
            target = chapter.with_suffix(".cover.png")
            need_series = chapter == chapters[0] and (overwrite or not (series / "cover.png").exists())
            if target.exists() and not overwrite and not need_series:
                continue
            try:
                # 默认首章已有封面也可供漫画封面使用，避免重复解包。
                # Reuse an existing first-chapter cover for the series without repeated extraction.
                if target.exists() and not overwrite:
                    with Image.open(target) as existing:
                        image = existing.copy()
                else:
                    image = first_image(chapter)
                if save_cover(image, target, overwrite):
                    print(target)
                if need_series and save_cover(image, series / "cover.png", overwrite):
                    print(series / "cover.png")
                image.close()
            except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
                errors += 1
                print(f"{chapter}: {error}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("comics", type=Path, help="the comics directory copied to the TF card")
    parser.add_argument("--overwrite", action="store_true", help="replace existing covers")
    args = parser.parse_args()
    if not args.comics.is_dir():
        parser.error("comics must be a directory")
    return 1 if prepare(args.comics, args.overwrite) else 0


if __name__ == "__main__":
    raise SystemExit(main())
