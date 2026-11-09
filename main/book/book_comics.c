/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：扫描固定漫画目录与直接章节，读取逐章进度并选择封面路径。
 * English: Scan the fixed comic root and direct chapters, load per-chapter progress and select cover paths.
 * 冻结：不递归、不改文件或进度；阅读顺序始终自然排序。
 * Frozen: Never recurse or mutate files/progress; reading order always uses natural sorting.
 */
#include "book_comics.h"
#include "book_progress.h"
#include "esp_heap_caps.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static bool visible(const char *name) { return name && name[0] && name[0] != '.'; }
static bool chapter_name(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    return visible(name) && ext && ext != name && !strcasecmp(ext, ".cbz");
}
static bool series_path(const char *path) {
    size_t n = strlen(BOOK_COMICS_ROOT);
    return path && !strncmp(path, BOOK_COMICS_ROOT, n) && path[n] == '/' &&
           visible(path + n + 1) && !strchr(path + n + 1, '/');
}
bool book_comics_context(const char *path, char directory[BOOK_STORE_PATH_MAX]) {
    if (!path || strlen(path) >= BOOK_STORE_PATH_MAX) return false;
    const char *slash = strrchr(path, '/');
    if (!slash || !chapter_name(slash + 1)) return false;
    char parent[BOOK_STORE_PATH_MAX];
    size_t n = (size_t)(slash - path);
    memcpy(parent, path, n); parent[n] = 0;
    if (!series_path(parent)) return false;
    if (directory) memcpy(directory, parent, n + 1);
    return true;
}
int book_comics_natural_compare(const char *a, const char *b) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (*x && *y) {
        if (isdigit(*x) && isdigit(*y)) {
            const unsigned char *xs = x, *ys = y;
            while (*x == '0') ++x;
            while (*y == '0') ++y;
            const unsigned char *xe = x, *ye = y;
            while (isdigit(*xe)) ++xe;
            while (isdigit(*ye)) ++ye;
            size_t nx = (size_t)(xe - x), ny = (size_t)(ye - y);
            if (nx != ny) return nx < ny ? -1 : 1;
            int cmp = memcmp(x, y, nx);
            if (cmp) return cmp;
            size_t zx = (size_t)(x - xs), zy = (size_t)(y - ys);
            if (zx != zy) return zx < zy ? -1 : 1;
            x = xe; y = ye;
        } else {
            int cx = tolower(*x), cy = tolower(*y);
            if (cx != cy) return cx < cy ? -1 : 1;
            ++x; ++y;
        }
    }
    if (*x != *y) return *x ? 1 : -1;
    return strcmp(a, b);
}
static int entry_compare(const void *a, const void *b) {
    return book_comics_natural_compare(((const book_comic_entry_t *)a)->name,
                                       ((const book_comic_entry_t *)b)->name);
}
void book_comics_dispose(book_comic_catalog_t *catalog) {
    if (!catalog) return;
    free(catalog->entries); memset(catalog, 0, sizeof(*catalog));
}
static void chapter_progress(book_comic_entry_t *entry) {
    book_progress_t progress;
    if (book_progress_load(entry->path, entry->size, &progress)) {
        entry->has_progress = true; entry->pct = progress.pct;
        entry->recent = progress.last_open_s;
    }
}
esp_err_t book_comics_scan(const char *directory, book_comic_catalog_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (directory && directory[0] && !series_path(directory)) return ESP_ERR_INVALID_ARG;
    bool series = !directory || !directory[0];
    DIR *dir = opendir(series ? BOOK_COMICS_ROOT : directory);
    if (!dir) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    size_t capacity = 0;
    esp_err_t result = ESP_OK;
    for (;;) {
        errno = 0;
        struct dirent *ent = readdir(dir);
        if (!ent) { if (errno) result = ESP_FAIL; break; }
        if (!visible(ent->d_name) || (!series && !chapter_name(ent->d_name))) continue;
        book_comic_entry_t entry = {0};
        int n = snprintf(entry.path, sizeof(entry.path), "%s/%s", series ? BOOK_COMICS_ROOT : directory, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(entry.path) || strlen(ent->d_name) >= sizeof(entry.name)) {
            out->incomplete = true; continue;
        }
        struct stat st;
        if (stat(entry.path, &st)) { out->incomplete = true; continue; }
        if (series ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) continue;
        memcpy(entry.name, ent->d_name, strlen(ent->d_name) + 1);
        if (series) {
            book_comic_catalog_t chapters;
            esp_err_t err = book_comics_scan(entry.path, &chapters);
            if (err != ESP_OK || chapters.incomplete) out->incomplete = true;
            entry.chapters = (uint32_t)chapters.count;
            for (size_t i = 0; i < chapters.count; ++i) {
                book_comic_entry_t *ch = &chapters.entries[i];
                if (ch->has_progress && (!entry.has_progress || ch->recent > entry.recent)) {
                    entry.has_progress = true; entry.recent = ch->recent; entry.pct = ch->pct;
                    memcpy(entry.latest, ch->name, strlen(ch->name) + 1);
                }
            }
            book_comics_dispose(&chapters);
            if (!entry.chapters) continue;
        } else {
            if (st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { out->incomplete = true; continue; }
            *strrchr(entry.name, '.') = 0;
            entry.size = (uint32_t)st.st_size;
            chapter_progress(&entry);
        }
        if (out->count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            if (next > SIZE_MAX / sizeof(entry)) { result = ESP_ERR_NO_MEM; break; }
            void *entries = heap_caps_realloc(out->entries, next * sizeof(entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!entries) { result = ESP_ERR_NO_MEM; break; }
            out->entries = entries; capacity = next;
        }
        out->entries[out->count++] = entry;
    }
    closedir(dir);
    out->incomplete |= result != ESP_OK;
    if (out->count > 1) qsort(out->entries, out->count, sizeof(*out->entries), entry_compare);
    return result;
}
bool book_comics_cover_sources(const char *source, char primary[BOOK_STORE_PATH_MAX],
                               char secondary[BOOK_STORE_PATH_MAX], char cbz[BOOK_STORE_PATH_MAX]) {
    primary[0] = secondary[0] = cbz[0] = 0;
    if (!source) return false;
    if (series_path(source)) {
        // 封面查询只找首章，不为每本封面重新分配章节表或读取进度。
        // Cover lookup finds only the first chapter without allocating a catalog or loading progress.
        DIR *dir = opendir(source);
        if (!dir) return false;
        char first[256] = "";
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            if (!chapter_name(ent->d_name) || strlen(ent->d_name) >= sizeof(first)) continue;
            char path[BOOK_STORE_PATH_MAX]; struct stat st;
            if (snprintf(path, sizeof(path), "%s/%s", source, ent->d_name) >= (int)sizeof(path) ||
                stat(path, &st) || !S_ISREG(st.st_mode)) continue;
            if (!first[0] || book_comics_natural_compare(ent->d_name, first) < 0)
                memcpy(first, ent->d_name, strlen(ent->d_name) + 1);
        }
        closedir(dir);
        if (!first[0] || snprintf(cbz, BOOK_STORE_PATH_MAX, "%s/%s", source, first) >= BOOK_STORE_PATH_MAX) return false;
        if (snprintf(primary, BOOK_STORE_PATH_MAX, "%s/cover.png", source) >= BOOK_STORE_PATH_MAX) primary[0] = 0;
        size_t n = strlen(cbz) - 4;
        if (n + sizeof(".cover.png") <= BOOK_STORE_PATH_MAX) {
            memcpy(secondary, cbz, n); memcpy(secondary + n, ".cover.png", sizeof(".cover.png"));
        }
        return true;
    }
    const char *ext = strrchr(source, '.');
    if (!ext || strcasecmp(ext, ".cbz") || strlen(source) >= BOOK_STORE_PATH_MAX) return false;
    memcpy(cbz, source, strlen(source) + 1);
    size_t n = (size_t)(ext - source);
    if (n + sizeof(".cover.png") <= BOOK_STORE_PATH_MAX) {
        memcpy(primary, source, n); memcpy(primary + n, ".cover.png", sizeof(".cover.png"));
    }
    return true;
}
