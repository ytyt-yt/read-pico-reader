#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：测试批量封面像素、自然首图顺序及保留/替换语义。
English: Test cover pixels, natural first-image order and preservation/replacement.
"""
import io
from pathlib import Path
import struct
import tempfile
import zipfile
from PIL import Image
from make_comic_covers import cover_image, prepare

with tempfile.TemporaryDirectory(prefix="comic-covers-") as temp:
    root = Path(temp)
    series = root / "漫画名称"
    series.mkdir()
    for chapter in ("10", "2", "1"):
        with zipfile.ZipFile(series / f"{chapter}.cbz", "w") as archive:
            for name, color in (("10.png", 180), ("2.png", 70), (".hidden/1.png", 0)):
                data = io.BytesIO()
                Image.new("L", (200, 300), color).save(data, "PNG")
                archive.writestr(name, data.getvalue())
    nested = series / "nested"
    nested.mkdir()
    (nested / "1.cbz").write_bytes(b"ignore")
    assert prepare(root) == 0
    for name in ("cover.png", "1.cover.png", "2.cover.png", "10.cover.png"):
        data = (series / name).read_bytes()
        assert struct.unpack_from(">IIBBBBB", data, 16) == (176, 240, 8, 0, 0, 0, 0)
        with Image.open(series / name) as image:
            assert image.mode == "L" and image.size == (176, 240) and image.getextrema() == (70, 70)
    saved = (series / "cover.png").read_bytes()
    (series / "1.cover.png").write_bytes(b"custom cover is preserved")
    assert prepare(root) == 0 and (series / "cover.png").read_bytes() == saved
    assert (series / "1.cover.png").read_bytes() == b"custom cover is preserved"
    assert prepare(root, overwrite=True) == 0
    with Image.open(series / "1.cover.png") as image:
        assert image.getextrema() == (70, 70)
    assert not (nested / "1.cover.png").exists()
    assert cover_image(Image.new("RGBA", (176, 240), (0, 0, 0, 0))).getextrema() == (255, 255)
    # 非同比例的源图两侧裁掉，中心内容不拉伸。/ Crop excess sides and retain the center without stretching.
    image = Image.new("L", (600, 240), 0)
    image.paste(255, (212, 0, 388, 240))
    result = cover_image(image)
    assert result.getpixel((88, 120)) == 255 and result.getpixel((0, 120)) > 200
print("comic covers: strict PNG, natural order, crop, alpha, preservation and replacement passed")
