/*
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：有界壁纸洗牌队列，成功推屏后提交TF进度，唤醒复用实际图片。
 * English: Bounded wallpaper shuffle bags, committing SD progress after presentation and retaining actual wake artwork.
 * 冻结：只扫描单层目录；恢复不推进，不改变面板/供电/PIN规则。
 * Frozen: Scan one directory level; restoration never advances or changes panel/power/PIN rules.
 */
#include "wallpaper_rotation.h"
#include "settings.h"
#include "ui_wallpaper.h"
#include "book_cover.h"
#include "read_pico_sd.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#ifdef ESP_PLATFORM
#include "book_cover_jobs.h"
#endif
#include <dirent.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifndef WALLPAPER_ROTATION_ROOT
#define WALLPAPER_ROTATION_ROOT "/sdcard/.kiikoread"
#endif
#define STATE_PATH WALLPAPER_ROTATION_ROOT "/locks/wallpaper-rotation.bin"
#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
typedef struct { char name[256]; uint64_t key; bool failed; } wallpaper_item_t;
typedef struct {
    uint32_t magic, count, cursor;
    char current[288];
    uint64_t order[WALLPAPER_ROTATION_MAX];
    uint32_t checksum;
} rotation_record_t;
static wallpaper_item_t *s_items;
static rotation_record_t *s_saved, *s_trial;
static unsigned s_count, s_warm;
static bool s_loaded, s_scanned, s_overflow, s_new_lock, s_prepared;
static bool s_restore_selected;
static char s_restore_path[288];

static uint64_t hash_bytes(uint64_t h, const void *data, size_t bytes) {
    const uint8_t *p = data;
    while (bytes--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}
static uint32_t record_checksum(const rotation_record_t *r) {
    return (uint32_t)hash_bytes(UINT64_C(14695981039346656037), r,
        offsetof(rotation_record_t, checksum));
}
static bool allocate_state(void) {
    if (!s_saved) s_saved = heap_caps_calloc(1, sizeof(*s_saved), CAPS);
    if (!s_trial) s_trial = heap_caps_calloc(1, sizeof(*s_trial), CAPS);
    return s_saved && s_trial;
}
static bool valid_path(const char *path) {
    if (!path[0]) return true;
    if (strncmp(path, "/sdcard/", 8) || !path[8]) return false;
    for (const char *p = path + 8; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
        for (size_t i = 0; i < n; ++i)
            if ((unsigned char)p[i] < 32 || p[i] == '\\' || p[i] == ':') return false;
        if (!end) break;
        p = end + 1;
    }
    return true;
}
static bool load_state(void) {
    if (!allocate_state() || !read_pico_sd_io_ready()) return false;
    if (s_loaded) return true;
    const char *paths[] = {STATE_PATH, STATE_PATH ".bak"};
    for (unsigned i = 0; i < 2; ++i) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        bool ok = fread(s_saved, 1, sizeof(*s_saved), f) == sizeof(*s_saved) &&
            fgetc(f) == EOF && !ferror(f) && s_saved->magic == UINT32_C(0x57525031) &&
            s_saved->count <= WALLPAPER_ROTATION_MAX && s_saved->cursor <= s_saved->count &&
            memchr(s_saved->current, 0, sizeof(s_saved->current)) && valid_path(s_saved->current) &&
            s_saved->checksum == record_checksum(s_saved);
        fclose(f);
        if (ok) { s_loaded = true; return true; }
    }
    memset(s_saved, 0, sizeof(*s_saved));
    s_saved->magic = UINT32_C(0x57525031);
    s_loaded = true;
    return true;
}
static int item_compare(const void *a, const void *b) {
    return strcmp(((const wallpaper_item_t *)a)->name, ((const wallpaper_item_t *)b)->name);
}
static int item_index(uint64_t key) {
    for (unsigned i = 0; i < s_count; ++i) if (s_items[i].key == key) return (int)i;
    return -1;
}
static void item_path(unsigned i, char path[288]) {
    snprintf(path, 288, "%s/%s", WALLPAPER_ROTATION_DIR, s_items[i].name);
}
static void shuffle(rotation_record_t *r) {
    for (unsigned i = r->count; i > 1; --i) {
        // 拒绝取模偏差，避免长期偏向部分图片。/ Reject modulo bias across long-running rotations.
        uint32_t value, limit = UINT32_MAX - UINT32_MAX % i;
        do { value = esp_random(); } while (value >= limit);
        unsigned j = value % i;
        uint64_t t = r->order[i - 1]; r->order[i - 1] = r->order[j]; r->order[j] = t;
    }
    if (r->count > 1) {
        int first = item_index(r->order[0]); char path[288];
        if (first >= 0) {
            item_path((unsigned)first, path);
            if (!strcmp(path, r->current)) {
                uint64_t t = r->order[0]; r->order[0] = r->order[1]; r->order[1] = t;
            }
        }
    }
    r->cursor = 0;
}
unsigned wallpaper_rotation_refresh(void) {
    if (!load_state()) return 0;
    if (!s_items) s_items = heap_caps_calloc(WALLPAPER_ROTATION_MAX, sizeof(*s_items), CAPS);
    if (!s_items) return 0;
    unsigned warmed = s_warm;
    s_count = s_warm = 0; s_overflow = false; s_scanned = true;
    DIR *dir = opendir(WALLPAPER_ROTATION_DIR);
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))) continue;
        char path[288];
        if (snprintf(path, sizeof(path), "%s/%s", WALLPAPER_ROTATION_DIR, entry->d_name) >= (int)sizeof(path) || !valid_path(path)) continue;
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) continue;
        if (s_count == WALLPAPER_ROTATION_MAX) { s_overflow = true; continue; }
        wallpaper_item_t *item = &s_items[s_count++];
        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
        int64_t info[] = {st.st_size, st.st_mtime};
        item->key = hash_bytes(hash_bytes(UINT64_C(14695981039346656037), path, strlen(path) + 1), info, sizeof(info));
        item->failed = false;
    }
    if (dir) closedir(dir);
    qsort(s_items, s_count, sizeof(*s_items), item_compare);
    bool same = s_saved->count == s_count;
    for (unsigned i = 0; same && i < s_count; ++i) {
        same = item_index(s_saved->order[i]) >= 0;
        for (unsigned j = 0; same && j < i; ++j) same = s_saved->order[i] != s_saved->order[j];
    }
    if (same) s_warm = warmed < s_count ? warmed : s_count;
    if (!same) {
        s_saved->count = s_count;
        for (unsigned i = 0; i < s_count; ++i) s_saved->order[i] = s_items[i].key;
        shuffle(s_saved);
    }
    return s_count;
}
bool wallpaper_rotation_overflow(void) { return s_overflow; }
void wallpaper_rotation_begin(void) {
    s_new_lock = s_prepared = s_restore_selected = false;
    if (!app_settings_wallpaper_random()) return;
    (void)wallpaper_rotation_refresh();
    if (!load_state()) return;
    if (s_saved->count && s_saved->cursor == s_saved->count) shuffle(s_saved);
    *s_trial = *s_saved; s_new_lock = true;
}
const char *wallpaper_rotation_path(void) {
    if (app_settings_wallpaper_random() && load_state()) {
        if (s_restore_selected) return s_restore_path;
        const char *path = s_prepared ? s_trial->current : s_saved->current;
        if (path[0]) return path;
    }
    return app_settings_wallpaper_path();
}
bool wallpaper_rotation_prepare(uint8_t *frame) {
    if (!frame) return false;
    if (s_new_lock && app_settings_wallpaper_random()) {
        unsigned tries = s_count < 16 ? s_count : 16;
        for (unsigned i = 0; i < tries; ++i) {
            if (s_trial->cursor == s_trial->count) shuffle(s_trial);
            int index = item_index(s_trial->order[s_trial->cursor++]);
            if (index >= 0 && !s_items[index].failed) {
                char candidate[288];item_path((unsigned)index, candidate);
                if (ui_wallpaper_prepare_lock(frame, candidate)) {
                    memcpy(s_trial->current, candidate, strlen(candidate) + 1);
                    s_prepared = true;return true;
                }
                s_items[index].failed = true;
            }
            // 坏图跳过进度独立于显示提交；失败批次后下次继续，推屏失败仍重试有效候选。
            // Keep failed skips independently of presentation; resume failed batches but retry unshown valid candidates.
            s_saved->cursor = s_trial->cursor;
            memcpy(s_saved->order, s_trial->order, sizeof(s_saved->order));
        }
        snprintf(s_trial->current, sizeof(s_trial->current), "%s", app_settings_wallpaper_path());
        s_prepared = ui_wallpaper_prepare_lock(frame, s_trial->current);
        return s_prepared;
    }
    const char *path = wallpaper_rotation_path();
    if (ui_wallpaper_prepare_lock(frame, path)) return true;
    // 恢复时不得抽下一张；保留固定壁纸兜底。/ Restoration never draws another random candidate; retain fixed fallback.
    if (strcmp(path, app_settings_wallpaper_path()) && ui_wallpaper_prepare_lock(frame, app_settings_wallpaper_path())) {
        snprintf(s_restore_path, sizeof(s_restore_path), "%s", app_settings_wallpaper_path());
        s_restore_selected = true;return true;
    }
    return false;
}
void wallpaper_rotation_finish(bool displayed) {
    if (s_new_lock && s_prepared && displayed) {
        *s_saved = *s_trial; s_saved->checksum = record_checksum(s_saved);
        if ((!mkdir(WALLPAPER_ROTATION_ROOT, 0775) || errno == EEXIST) &&
            (!mkdir(WALLPAPER_ROTATION_ROOT "/locks", 0775) || errno == EEXIST)) {
            FILE *f = fopen(STATE_PATH ".tmp", "wb");
            if (f) {
                bool ok = fwrite(s_saved, 1, sizeof(*s_saved), f) == sizeof(*s_saved) && !fflush(f);
                if (fclose(f)) ok = false;
                if (ok) {
                    (void)remove(STATE_PATH ".bak");
                    bool old = rename(STATE_PATH, STATE_PATH ".bak") == 0;
                    if (rename(STATE_PATH ".tmp", STATE_PATH)) {
                        if (old) (void)rename(STATE_PATH ".bak", STATE_PATH);
                    } else (void)remove(STATE_PATH ".bak");
                }
                (void)remove(STATE_PATH ".tmp");
            }
        }
    }
    s_new_lock = s_prepared = false;
}
void wallpaper_rotation_invalidate(void) {
    free(s_items); free(s_saved); free(s_trial);
    s_items = NULL; s_saved = s_trial = NULL;
    s_count = s_warm = 0;
    s_loaded = s_scanned = s_new_lock = s_prepared = s_overflow = s_restore_selected = false;
}
void wallpaper_rotation_idle(void) {
#ifdef ESP_PLATFORM
    if (!app_settings_wallpaper_random() || app_settings_lock_style() != 1 || !read_pico_sd_io_ready()) return;
    if (!s_scanned) (void)wallpaper_rotation_refresh();
    if (s_warm < s_count) {
        char path[288]; item_path(s_warm, path);
        if (book_cover_jobs_prefetch_wallpaper(path)) ++s_warm;
    }
#endif
}
