#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：生成原创漫画夹具，测试真实解码器，可选验证用户 CBZ。
English: Test real decoders with original comic fixtures and an optional user CBZ.
冻结：产物仅在 build；不把用户漫画加入仓库。/ Frozen: Outputs stay in build; no user comic is added to the repository.
"""
import argparse
import io
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import zipfile
from PIL import Image, ImageChops, ImageDraw, ImageStat

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def image_bytes(image, kind="WEBP", **options):
    data = io.BytesIO()
    image.save(data, format=kind, **options)
    return data.getvalue()


def compile_test(work):
    # 链接真实显示入口的空闲边界，覆盖主循环每轮调用而非仅测试缓存接口。
    # Exercise the actual present entry on idle loop calls, beyond the standalone cache API.
    app = (ROOT / "main/apps/app_book.c").read_text()
    start = app.index("static bool present(app_ctx_t* ctx, app_redraw_t redraw) {")
    end = app.index("    // 管理页及排序弹层", start)
    gate = app[start:end].replace("static bool present(", "static bool comic_test_present_gate(", 1)
    start = app.index("        if (s_unsaved && ctx->now_ms - s_last_turn_ms >= 2000")
    end = app.index("        EpdRect band = book_layout_image_rect();", start)
    checkpoint = "static void comic_test_idle_checkpoint(app_ctx_t *ctx) {\n" + app[start:end] + "}\n"
    zoom_start = app.index("static app_redraw_t zoom_frame(app_ctx_t* ctx) {")
    zoom_end = app.index("    const reader_image_t *image", zoom_start)
    zoom_gate = "static void comic_test_zoom_gate(void) {\n" + app[app.index("\n", zoom_start) + 1:zoom_end] + "}\n"
    (work / "comic_present_gate.h").write_text(gate + "    return false;\n}\n" + checkpoint + zoom_gate)
    png_flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libpng"], text=True))
    png_libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "libpng"], text=True))
    (work / "sdkconfig.h").write_text("\n".join([
        "#define CONFIG_JD_SZBUF 512", "#define CONFIG_JD_FORMAT 0", "#define CONFIG_JD_USE_SCALE 1",
        "#define CONFIG_JD_TBLCLIP 1", "#define CONFIG_JD_FASTDECODE 0"]) + "\n")
    common = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
              "-ffunction-sections", "-fdata-sections", "-include", str(ROOT / "tools/cbz_host_stubs/track.h")]
    if sys.platform.startswith("linux"):
        common += ["-D__LINUX__"]
    includes = ["tools/cbz_host_stubs", "main/book", "components/read_pico_webp/libwebp", "components/read_pico_webp/include", "tools/book_epub_stubs", "tools/zip_host_stubs", "components/jpegdec/src",
                "managed_components/espressif__esp_jpeg/include", "managed_components/espressif__esp_jpeg/tjpgd"]
    common += ["-I" + str(ROOT / path) for path in includes] + ["-I" + str(work), *png_flags]
    book_sources = ["book_source", "book_cbz", "book_webp", "book_epub", "book_txt", "gbk",
                    "book_index_cache", "book_image_header", "book_cover", "book_comics", "zip_reader", "html_text", "book_layout", "book_comic_cache"]
    sources = ["tools/book_cbz_host_test.c", "tools/cbz_host_freertos.c"] + [f"main/book/{name}.c" for name in book_sources]
    sources += ["managed_components/espressif__esp_jpeg/tjpgd/tjpgd.c",
                "managed_components/espressif__esp_jpeg/jpeg_decoder.c",
                "components/jpegdec/src/JPEGDEC.cpp", "components/jpegdec/src/jpegdec_shim.cpp"]
    vendor = ROOT / "components/read_pico_webp"
    vendor_sources = list(dict.fromkeys(re.findall(r'"(libwebp/src/[^"\s]+\.c)"', (vendor / "CMakeLists.txt").read_text())))
    sources += [str((vendor / source).relative_to(ROOT)) for source in vendor_sources]
    objects = []
    for index, source in enumerate(sources):
        cpp = source.endswith(".cpp")
        flags = common.copy()
        stub = "tools/book_layout_stubs" if source.endswith(("book_layout.c", "book_cbz_host_test.c")) else "tools/book_epub_stubs"
        flags = ["-I" + str(ROOT / "tools/cbz_host_stubs"), "-I" + str(ROOT / stub), *flags]
        if "read_pico_webp/libwebp/src/" in source:
            flags += ["-U__SSE2__", "-U__SSSE3__", "-U__SSE4_1__", "-U__AVX2__", "-U__ARM_NEON__", "-U__aarch64__"]
            if source.endswith("utils/utils.c"):
                flags += ["-DESP_PLATFORM", "-include", str(vendor / "webp_alloc.h")]
        elif source.startswith(("main/", "tools/")):
            flags += ["-Wall", "-Wextra", "-Werror"]
        if source.endswith("JPEGDEC.cpp"):
            flags += ["-fno-sanitize=alignment,shift"]
        if source.endswith("esp_jpeg/jpeg_decoder.c"):
            # IDF 的 unsigned int 回调在64位主机上与 TJPGD size_t 不同。/ IDF's unsigned-int callback differs from TJPGD size_t on 64-bit hosts.
            flags += ["-Wno-error=incompatible-pointer-types"]
        if source.endswith("book/zip_reader.c"):
            flags += ["-Dzip_extract=cbz_test_extract_full_impl", "-Dzip_extract_prefix=cbz_test_extract_prefix_impl"]
        obj = work / f"part{index}.o"
        run("c++" if cpp else "cc", "-std=c++17" if cpp else "-std=gnu11", *flags,
            "-c", ROOT / source, "-o", obj)
        objects.append(obj)
    exe = work / "cbz-test"
    gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
    run("c++", *common, gc, *objects, *png_libs, "-lz", "-o", exe)
    return exe


def verify(exe, path, work, names):
    output = work / (path.stem + "-pages")
    output.mkdir(exist_ok=True)
    run(exe, "read", path, output, *names)
    with zipfile.ZipFile(path) as archive:
        for page, name in enumerate(names, 1):
            decoded = Image.open(output / f"{page:04d}.pgm").convert("L")
            reference = Image.open(io.BytesIO(archive.read(name))).convert("RGBA")
            white = Image.new("RGBA", reference.size, "white")
            white.alpha_composite(reference)
            reference = white.convert("L").resize(decoded.size, Image.Resampling.BOX)
            error = ImageStat.Stat(ImageChops.difference(decoded, reference)).mean[0]
            assert error < 18, (name, error)
            assert ImageStat.Stat(decoded).stddev[0] > 1, name
    print(f"PASS: {path.name}: all {len(names)} decoded pages agree with reference grayscale")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample", type=Path, help="Additional local CBZ to validate without copying it into the repository")
    args = parser.parse_args()
    work = ROOT / "build/cbz-tests"
    work.mkdir(parents=True, exist_ok=True)
    exe = compile_test(work)
    image = Image.new("RGB", (320, 460), "white")
    draw = ImageDraw.Draw(image)
    draw.rectangle((20, 20, 290, 190), outline="black", width=8)
    draw.line((30, 220, 280, 420), fill="black", width=9)
    draw.ellipse((70, 240, 240, 350), fill=(120, 120, 120))
    alpha = image.convert("RGBA")
    alpha.putalpha(Image.linear_gradient("L").resize(alpha.size))
    pages = {"chapter1/page10.WEBP": image_bytes(image, quality=80),
             "chapter1/page2.webp": image_bytes(alpha, lossless=True),
             "chapter1/page1.webp": image_bytes(image, lossless=True),
             "chapter2/page1.png": image_bytes(image, "PNG"),
             "chapter2/page2.jpg": image_bytes(image, "JPEG"),
             "chapter10/page1.webp": image_bytes(alpha, quality=85)}
    expected = ["chapter1/page1.webp", "chapter1/page2.webp", "chapter1/page10.WEBP",
                "chapter2/page1.png", "chapter2/page2.jpg", "chapter10/page1.webp"]
    for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        path = work / f"comic-{compression}.cbz"
        with zipfile.ZipFile(path, "w", compression=compression) as archive:
            for name, data in pages.items():
                archive.writestr(name, data)
            for name in (".hidden/page0.webp", "chapter1/.page0.webp", "__MACOSX/page0.webp", "chapter1/._page1.webp"):
                archive.writestr(name, pages[expected[0]])
            archive.writestr("ComicInfo.xml", "<ComicInfo><Title>Ignored metadata</Title></ComicInfo>")
        verify(exe, path, work, expected)
    path = work / "bad-image.cbz"
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("1.webp", b"invalid webp")
    run(exe, "bad-image", path)
    animated = work / "animated.cbz"
    with zipfile.ZipFile(animated, "w") as archive:
        archive.writestr("1.webp", image_bytes(image, save_all=True, append_images=[image.rotate(180)], duration=100, loop=0))
    run(exe, "decode-error", animated)
    truncated = work / "truncated.cbz"
    with zipfile.ZipFile(truncated, "w") as archive:
        archive.writestr("1.webp", pages[expected[0]][:100])
    run(exe, "decode-error", truncated)
    corrupt = work / "corrupt.cbz"
    with zipfile.ZipFile(corrupt, "w") as archive:
        archive.writestr("1.webp", pages[expected[0]])
    data = bytearray(corrupt.read_bytes())
    start = 30 + struct.unpack_from("<H", data, 26)[0] + struct.unpack_from("<H", data, 28)[0]
    data[start + 100] ^= 1
    corrupt.write_bytes(data)
    run(exe, "crc", corrupt)
    large = work / "oversized.cbz"
    with zipfile.ZipFile(large, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("1.webp", b"0" * (8 * 1024 * 1024 + 1))
    run(exe, "oversized", large)
    empty = work / "empty.cbz"
    with zipfile.ZipFile(empty, "w") as archive:
        archive.writestr("ComicInfo.xml", "metadata only")
    run(exe, "reject", empty)
    invalid = work / "not-zip.cbz"
    invalid.write_bytes(b"not a zip")
    run(exe, "reject", invalid)
    if args.sample:
        with zipfile.ZipFile(args.sample) as archive:
            def natural(name):
                return [int(part) if part.isdigit() else part.lower() for part in re.split(r"(\d+)", name)]
            names = sorted((name for name in archive.namelist()
                            if Path(name).suffix.lower() in (".webp", ".png", ".jpg", ".jpeg")
                            and not any(part.startswith(".") or part == "__MACOSX" for part in Path(name).parts)), key=natural)
        verify(exe, args.sample.resolve(), work, names)
    print("PASS: CBZ stored/deflate, natural nested order, all image formats, alpha, CRC, animation/oversize rejection and OOM cleanup")


if __name__ == "__main__":
    main()
