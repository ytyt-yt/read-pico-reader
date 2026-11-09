#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 中文：从仓库根运行漫画目录、封面、导航和阅读边界测试。
# English: Run comic catalog, covers, navigation and reader-boundary tests from the repo root.
set -euo pipefail
cd "$(dirname "$0")/.."
# 板级覆盖和共享源码共同参与测试，和产品构建选择保持一致。
# Exercise board overrides together with shared sources, matching the product build selection.
if [[ ! -f .kiiko-source-view.json ]]; then
    comic_test_view="build/comics-tests/pico-source"
    python3 tools/board_source_view.py --board pico --output "$comic_test_view" --tests
    exec bash "$comic_test_view/tools/run_comics_host_tests.sh"
fi
mkdir -p build/comics-tests
python3 tools/test_comic_covers.py
python3 tools/test_comics_catalog.py
python3 tools/comics_navigation_host_test.py
python3 tools/comics_reader_host_test.py
python3 tools/comics_ui_host_test.py
python3 tools/cover_jobs_host_test.py
python3 tools/reader_chapter_jump_host_test.py
python3 tools/reader_fullscreen_host_test.py
python3 tools/book_ui_host_test.py
python3 tools/test_book_toc.py
