#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：执行真实两层扫描和预制封面读取/缓存，含同秒同大小替换。
English: Exercise real two-level scans and prepared-cover loading/caches, including same-second same-size replacement.
"""
from pathlib import Path
import shlex
import subprocess
import tempfile
from PIL import Image
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
COVER = ROOT / "main/book/book_cover.c"
source = COVER.read_text()
METALIO = "static metalio_cover_key_t ram_key(" in source
unit = r'''
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <utime.h>
#include <png.h>
#include "book_comics.h"
#include "book_cover.h"
#include "metalio_cover_cache.h"
#include "book_progress.h"
#include "esp_heap_caps.h"
static const char *root;
static bool oom;
static unsigned extractions, rows_allocated;
static void map(const char *p,char out[1024]){assert(!strncmp(p,"/sdcard",7));assert(snprintf(out,1024,"%s%s",root,p+7)<1024);}
static FILE *mapped_open(const char *p,const char *m){char out[1024];map(p,out);return fopen(out,m);}
static DIR *mapped_opendir(const char *p){char out[1024];map(p,out);return opendir(out);}
static int mapped_stat(const char *p,struct stat *s){char out[1024];map(p,out);return stat(out,s);}
static int mapped_mkdir(const char *p,int mode){char out[1024];map(p,out);return mkdir(out,mode);}
static int mapped_rename(const char *p,const char *q){char a[1024],b[1024];map(p,a);map(q,b);return rename(a,b);}
static int mapped_remove(const char *p){char a[1024];map(p,a);return remove(a);}
static void *test_realloc(void *p,size_t n,int caps){(void)caps;return oom?NULL:realloc(p,n);}
#define fopen mapped_open
#define opendir mapped_opendir
#define stat(p,s) mapped_stat(p,s)
#define mkdir mapped_mkdir
#define rename mapped_rename
#define remove mapped_remove
#define heap_caps_realloc test_realloc
bool book_progress_load(const char *path,uint32_t size,book_progress_t *out){
    (void)size; const char *name=strrchr(path,'/')+1;
    if(strcmp(name,"1.cbz")&&strcmp(name,"2.cbz"))return false;
    memset(out,0,sizeof(*out));out->last_open_s=!strcmp(name,"2.cbz")?20:10;out->pct=42;return true;
}
#include "book_comics.c"
static bool book_title_from_path(const char *p,char *out,size_t cap){(void)p;(void)out;(void)cap;return false;}
static bool book_auto_cover_render(const char*p,const char*t,const char*a,unsigned w,unsigned h,uint8_t*out){(void)p;(void)t;(void)a;memset(out,191,w*h);return true;}
bool book_cover_thumbnail(const uint8_t *p,size_t n,bool png,uint8_t out[BOOK_COVER_W * BOOK_COVER_H]){(void)png;assert(p&&n==1);memset(out,77,176*240);return true;}
static esp_err_t book_cbz_cover_bounded(const char *p,uint8_t **out,size_t *size,bool *png,size_t budget){
    assert(strstr(p,".cbz")&&budget);++extractions;*out=malloc(1);**out=1;*size=1;*png=true;return ESP_OK;
}
static esp_err_t book_epub_cover_bounded(const char *p,uint8_t **out,size_t *size,bool *png,size_t budget){return book_cbz_cover_bounded(p,out,size,png,budget);}
#define ZIP_OUTPUT_MAX (8u*1024u*1024u)
#define BOOK_AUTO_COVER_VERSION 1
static void decode_yield(unsigned at){(void)at;}
static void *cover_alloc(size_t n){++rows_allocated;return oom?NULL:malloc(n);}
typedef struct {uint16_t at;uint8_t frac;} scale_step_t;
typedef struct {const uint8_t *data;size_t size,pos;FILE *file;} png_input_t;
'''
for name in ("luminance", "scale_axis", "scale_map", "scale_next", "bilinear4", "book_cover_crop",
             "png_read_mem", "png_gray_rows"):
    unit += function(name, COVER) + "\n"
unit += source[source.index("#define COVER_CACHE_MAGIC"):source.index("static uint64_t cover_hash")]
for name in ("cover_hash", "book_cover_source_fingerprint", "prepared_comic_cover", "cover_cache_key",
             "cover_cache_read", "cover_cache_write", "book_cover_load_gray_impl", "book_cover_load_gray"):
    unit += function(name, COVER) + "\n"
    if METALIO and name == "cover_cache_key":
        unit += function("ram_key", COVER) + "\n"
unit += source[source.index("#define FAST_COVER_REVISION"):source.index("static uint64_t fast_cover_checksum")]
for name in ("fast_cover_checksum", "fast_cover_key", "book_cover_fast_cache_read", "fast_cache_write", "book_cover_fast_cache_write"):
    unit += function(name, COVER) + "\n"
    if METALIO and name == "fast_cover_key":
        unit += function("finished_fast_ram_key", COVER) + "\n"
unit += r'''
static void replace_cover(const char *fixture,const char *destination){
 char p[288];snprintf(p,sizeof(p),"/sdcard/%s",fixture);FILE *in=fopen(p,"rb"),*out=fopen(destination,"wb");assert(in&&out);
 uint8_t b[256];size_t n;while((n=fread(b,1,sizeof(b),in)))assert(fwrite(b,1,n,out)==n);assert(!fclose(in)&&!fclose(out));
 char actual[1024];map(destination,actual);struct utimbuf time={1700000000,1700000000};assert(!utime(actual,&time));
}
int main(int argc,char **argv){
 assert(argc==2);root=argv[1];book_comic_catalog_t catalog;
 assert(book_comics_scan(NULL,&catalog)==ESP_OK&&!catalog.incomplete&&catalog.count==3);
 assert(!strcmp(catalog.entries[0].name,"Series2")&&catalog.entries[0].chapters==3);
 assert(!strcmp(catalog.entries[0].latest,"2")&&catalog.entries[0].recent==20);book_comics_dispose(&catalog);
 assert(book_comics_scan("/sdcard/comics/Series2",&catalog)==ESP_OK&&catalog.count==3);
 assert(!strcmp(catalog.entries[0].name,"1")&&!strcmp(catalog.entries[1].name,"2")&&!strcmp(catalog.entries[2].name,"10"));
 assert(catalog.entries[1].pct==42&&catalog.entries[1].has_progress);book_comics_dispose(&catalog);
 char directory[288];assert(book_comics_context("/sdcard/comics/Series2/2.CBZ",directory));
 assert(!strcmp(directory,"/sdcard/comics/Series2"));
 const char *bad[]={"/sdcard/books/2.cbz","/sdcard/comics/2.cbz","/sdcard/comics/.hidden/2.cbz","/sdcard/comics/Series2/nested/2.cbz","/sdcard/comics/../2.cbz","/sdcard/comics/Series2/.2.cbz"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!book_comics_context(bad[i],directory));
 assert(book_comics_natural_compare("chapter9999999999999999999999999","chapter10000000000000000000000000")<0);
 oom=true;assert(book_comics_scan("/sdcard/comics/Series2",&catalog)==ESP_ERR_NO_MEM&&catalog.incomplete&&!catalog.count);book_comics_dispose(&catalog);oom=false;
 const char *series="/sdcard/comics/Series2",*chapter="/sdcard/comics/Series2/1.cbz",*cover="/sdcard/comics/Series2/cover.png",*side="/sdcard/comics/Series2/1.cover.png";
 uint8_t pixels[176*240];bool pending;char primary[288],secondary[288],cbz[288];
 assert(book_comics_cover_sources(series,primary,secondary,cbz)&&!strcmp(primary,cover)&&!strcmp(secondary,side)&&!strcmp(cbz,chapter));
 replace_cover("valid.png",cover);replace_cover("secondary.png",side);
 assert(book_cover_load_gray(series,"Series2","",pixels,true,&pending)&&pixels[0]==39&&!extractions&&!rows_allocated);
 assert(book_cover_load_gray(chapter,"1","",pixels,true,&pending)&&pixels[0]==91&&!extractions);
 uint64_t before=book_cover_source_fingerprint(series);replace_cover("replaced.png",cover);
 assert(before!=book_cover_source_fingerprint(series));
 assert(book_cover_load_gray(series,"Series2","",pixels,true,&pending)&&pixels[0]==173&&!extractions);
 book_fast_cover_key_t key={164,214,21,100,0};uint8_t bits[4494],out[4494];memset(bits,0x55,sizeof(bits));bool edge;
 book_cover_fast_cache_write(series,"Series2","",key,bits,false);
 assert(book_cover_fast_cache_read(series,"Series2","",key,out,&edge)&&!memcmp(out,bits,sizeof(bits)));
 assert(!remove(cover));assert(!book_cover_fast_cache_read(series,"Series2","",key,out,&edge));
 assert(book_cover_load_gray(series,"Series2","",pixels,true,&pending)&&pixels[0]==91&&!extractions);
 const char *invalid[]={"wrong-size.png","rgb.png","alpha.png","transparent.png","interlaced.png","corrupt.png"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i){replace_cover(invalid[i],cover);assert(book_cover_load_gray(series,"Series2","",pixels,true,&pending)&&pixels[0]==91&&!extractions);}
 assert(!remove(side));assert(book_cover_load_gray(series,"Series2","",pixels,true,&pending)&&pixels[0]==77&&extractions==1);
 replace_cover("valid.png",cover);assert(!book_cover_load_gray(series,"Series2","",pixels,false,&pending)&&pending&&extractions==1);
 assert(book_cover_load_gray(series,"Series2","",pixels,true,&pending)&&pixels[0]==39&&extractions==1);
 puts("PASS: real two-level catalog, natural order, hidden/nested/empty filtering, recent progress, OOM, prepared pixels without archive reads, fallback and gray/BW cache invalidation");
}
'''
# S31测试执行实际RAM缓存；结束时释放缓存，不借用Pico的磁盘专用实现。
# S31 tests exercise and release the real RAM cache rather than borrowing Pico's disk-only implementation.
if METALIO:
    unit = unit.replace(' puts("PASS:', ' metalio_cover_cache_clear();\n puts("PASS:')
with tempfile.TemporaryDirectory(prefix="comics-catalog-", dir=ROOT / "build") as temp:
    work = Path(temp)
    long_name = "漫画" * 35
    for name in ("Series2", "Series10", "empty", ".hidden", long_name):
        (work / "comics" / name).mkdir(parents=True)
    for name in ("1.cbz", "2.cbz", "10.cbz", ".0.cbz"):
        (work / "comics/Series2" / name).write_bytes(b"CBZ")
    (work / "comics/Series2/nested").mkdir()
    (work / "comics/Series2/nested/0.cbz").write_bytes(b"nested")
    (work / "comics/Series10/1.cbz").write_bytes(b"CBZ")
    (work / "comics" / long_name / ("章节" * 8 + ".cbz")).write_bytes(b"long")
    (work / "comics/.hidden/1.cbz").write_bytes(b"hidden")
    (work / "comics/empty/nested").mkdir()
    (work / "comics/empty/nested/1.cbz").write_bytes(b"ignored")
    for name, mode, size, color in (("valid", "L", (176,240),39), ("replaced", "L", (176,240),173),
                                   ("secondary", "L", (176,240),91), ("wrong-size", "L", (175,240),0),
                                   ("rgb", "RGB", (176,240),(0,0,0)), ("alpha", "LA", (176,240),(0,0))):
        Image.new(mode, size, color).save(work / (name + ".png"), compress_level=0)
    Image.new("L", (176,240),0).save(work / "transparent.png", transparency=0)
    # 构造交错标记的无效PNG，严格检查必须在读像素前拒绝。/ Reject an interlace-marked invalid PNG before pixel reading.
    import zlib
    data = bytearray((work / "valid.png").read_bytes())
    data[28] = 1
    data[29:33] = zlib.crc32(data[12:29]).to_bytes(4,"big")
    (work / "interlaced.png").write_bytes(data)
    (work / "corrupt.png").write_bytes((work / "valid.png").read_bytes()[:80])
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "libpng"], text=True))
    (work / "test.c").write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "tools/book_epub_stubs"), "-I" + str(ROOT / "main/book"),
                    str(work / "test.c"), *([str(ROOT / "main/book/metalio_cover_cache.c"), "-pthread"] if METALIO else []), *flags, "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test"), str(work)], check=True)
