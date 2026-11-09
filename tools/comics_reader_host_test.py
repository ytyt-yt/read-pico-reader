#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：运行真实漫画跨文件、返回与目录上下文，模拟书源及NVS边界。
English: Execute actual comic file boundaries, returns and TOC context with source/NVS boundary doubles.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / "main/apps/app_book.c"
unit = r'''
#include "book_file_path.h"


#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int leaf;int64_t now_ms;int request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,TOC,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS,MODE_GL16,BOOK_KIND_TXT=0,BOOK_KIND_CBZ=99};
#define UI_NAV_COMICS 5
#define BOOK_TOC_ROWS 7
#define E0470_TURN_RTL 1
#define E0470_TURN_LTR -1
#define BOOK_STORE_PATH_MAX 288
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define TAG "test"
static void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
typedef struct {uint16_t chapter;uint32_t byte_off;} book_progress_t;
typedef struct {char name[256],path[288];} comic_entry_t;
static comic_entry_t entries[]={ {"1","/sdcard/comics/Series/1.cbz"}, {"2","/sdcard/comics/Series/2.cbz"}, {"10","/sdcard/comics/Series/10.cbz"} };
static struct {comic_entry_t *entries;size_t count;} s_comic_catalog={entries,3};
static bool s_comic_active=true,s_reader_return_home,s_requested_fullscreen=true;
static char s_comic_directory[288]="/sdcard/comics/Series",s_message[128],s_requested_open[288];
static char s_reader_notice[64],returned_directory[288],returned_message[128];
static int64_t s_reader_notice_until;
static size_t s_comic_index,s_chapter,s_page,s_jump_offset,s_selected_toc;
static int s_comic_return_page=2,s_comic_open_position,s_requested_comic_page;
static bool s_requested_comic,s_requested_open_home,s_requested_resume_mode;
static bool default_fullscreen;
static bool app_settings_reader_default_fullscreen(void){return default_fullscreen;}
static void reader_zoom_reset(void){}
static size_t reader_page_offset(size_t p){return p;}
static size_t book_navigation_position(size_t ch,size_t p){(void)ch;(void)p;return SIZE_MAX;}
static int book_toc_open(size_t p){return (int)p/BOOK_TOC_ROWS;}
static const char *s_text="comic";
static bool s_clear_confirm,s_reader_text_frame,s_reader_body_frame=true,s_reader_turn_pending;
static bool s_reader_image_frame __attribute__((unused)),s_reader_image_exit_pending __attribute__((unused));
static int s_turn_direction __attribute__((unused));
static bool s_water_turn_pending,s_reader_cleanup,s_save_failed;
static bool s_reader_fullscreen=true;
#define s_toolbar (s_reader_panel!=READER_PANEL_NONE)
static int s_reader_panel=READER_PANEL_NONE,s_view=READING,s_comic_direction,s_water_turn_dir;
static unsigned s_stats_pending_turns,s_session_turns,s_turns,s_unsaved;
static int64_t s_last_turn_ms,s_stats_activity_ms;
static unsigned saves,pauses,closes,opens;
static int source_kind=BOOK_KIND_CBZ,returned_page,bad_index=-1;
static bool paused;
static size_t image_counts[]={2,4,3},saved_chapters[]={0,2,1};
static void copy_text(char *d,size_t cap,const char *s){snprintf(d,cap,"%s",s);}
static void book_comic_cache_pause(void){paused=true;++pauses;}
static void save_progress(void){assert(paused||source_kind!=BOOK_KIND_CBZ);++saves;saved_chapters[s_comic_index]=s_chapter;}
static void free_book(void){assert(paused);++closes;s_text=NULL;}
static void app_comics_request_chapters(const char *dir,int page,const char *message){snprintf(returned_directory,sizeof(returned_directory),"%s",dir);snprintf(returned_message,sizeof(returned_message),"%s",message);returned_page=page;}
static void ui_nav_request(app_ctx_t *ctx,int page){ctx->request_app=page;}
static bool book_comics_context(const char *path,void *dir){(void)dir;return !strncmp(path,"/sdcard/comics/Series/",21);}
static void return_to_cached_shelf(app_ctx_t *ctx){ctx->leaf=0;}
static int book_kind(void){return source_kind;}
static size_t book_layout_page_count(void){return 1;}
static size_t book_chapter_count(void){return image_counts[s_comic_index];}
static size_t book_navigation_count(void){return book_chapter_count();}
static size_t book_navigation_chapter(size_t i){return i;}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,size_t offset,bool last){(void)ctx;(void)offset;(void)last;assert(chapter<book_chapter_count());s_chapter=chapter;s_page=0;return true;}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){(void)ctx;(void)dir;}
static bool reader_turn_chrome(app_ctx_t *ctx){(void)ctx;s_reader_panel=READER_PANEL_NONE;return false;}
static void set_reader_view(int view){s_view=view;}
static uint8_t app_settings_reader_full_pages(void){return 0;}
static unsigned percent(size_t page){(void)page;return (s_chapter+1)*100/book_chapter_count();}
static const char *app_settings_book_background_path(void){return "";}
static int book_layout_page_image_count(size_t page){(void)page;return 1;}
static int app_settings_reader_turn_effect(void){return 1;}
static app_redraw_t paint_reading(app_ctx_t *ctx,int mode){(void)ctx;(void)mode;return APP_REDRAW_AREA;}
'''
unit += function("reader_open_chapter_index", BOOK) + "\n"
unit += r'''
static bool open_book(app_ctx_t *ctx,const char *path){
 assert(paused);++opens;size_t index=0;while(index<3&&strcmp(entries[index].path,path))++index;assert(index<3);
 if((int)index==bad_index){strcpy(s_message,"章节不可读");s_text=NULL;return false;}
 book_progress_t progress={.chapter=(uint16_t)saved_chapters[index]};
 size_t destination=reader_open_chapter_index(true,&progress,image_counts[index]);
 s_chapter=destination;s_page=0;s_text="comic";s_reader_fullscreen=s_requested_resume_mode?s_requested_fullscreen:default_fullscreen;s_requested_resume_mode=false;s_view=READING;s_reader_panel=READER_PANEL_NONE;ctx->leaf=0;
 return true;
}
'''
for name in ("comic_return", "comic_open_chapter", "current_toc_position", "comic_toc_title",
             "reader_return", "turn_page", "app_book_request_open", "app_book_request_comic"):
    unit += function(name, BOOK) + "\n"
unit += r'''
static void reset(app_ctx_t *ctx,size_t index,size_t chapter){
 s_comic_active=true;s_comic_index=index;s_chapter=chapter;s_page=0;s_text="comic";ctx->request_app=0;s_unsaved=0;paused=true;
 s_reader_body_frame=true;s_reader_return_home=false;s_reader_fullscreen=true;s_reader_panel=READER_PANEL_NONE;s_reader_notice[0]=0;s_comic_open_position=0;
}
int main(void){app_ctx_t ctx={.now_ms=1000};
 assert(app_book_request_comic("/sdcard/comics/Series/2.cbz",3)&&s_requested_comic&&s_requested_comic_page==3);
 assert(!app_book_request_comic("/sdcard/books/old.cbz",0));
 assert(app_book_request_open("/sdcard/books/old.cbz")&&!s_requested_comic);
 reset(&ctx,0,1);assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&s_comic_index==1&&s_chapter==0);
 assert(saved_chapters[0]==1&&opens==1&&saves>=1&&pauses>=1&&s_water_turn_pending&&s_water_turn_dir==E0470_TURN_RTL);
 assert(turn_page(&ctx,-1)==APP_REDRAW_AREA&&s_comic_index==0&&s_chapter==1&&s_water_turn_dir==E0470_TURN_LTR);
 assert(saved_chapters[1]==0);
 saved_chapters[1]=2;assert(comic_open_chapter(&ctx,1,0)&&s_comic_index==1&&s_chapter==2);
 assert(current_toc_position()==1);char title[64];assert(comic_toc_title(2,title,sizeof(title),NULL)&&!strcmp(title,"10"));
 assert(!comic_toc_title(3,title,sizeof(title),NULL));
 assert(reader_return(&ctx)==APP_REDRAW_NONE&&ctx.request_app==5&&returned_page==2&&!strcmp(returned_directory,s_comic_directory)&&!s_text);
 reset(&ctx,1,3);s_comic_active=false;s_reader_return_home=true;assert(reader_return(&ctx)==APP_REDRAW_NONE&&ctx.request_app==0);
 reset(&ctx,0,1);s_reader_panel=READER_PANEL_TOOLS;
 assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_water_turn_pending&&s_comic_index==1);
 reset(&ctx,0,1);s_reader_fullscreen=false;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&s_water_turn_pending&&!s_reader_fullscreen&&s_comic_index==1);
 for(int full=0;full<2;++full)for(int preference=0;preference<2;++preference){
  default_fullscreen=preference;reset(&ctx,0,1);s_reader_fullscreen=full;
  assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&s_comic_index==1&&s_reader_fullscreen==full&&!s_requested_resume_mode);
  assert(turn_page(&ctx,-1)==APP_REDRAW_AREA&&s_comic_index==0&&s_reader_fullscreen==full&&!s_requested_resume_mode);
 }
 reset(&ctx,2,2);unsigned before=opens;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&opens==before&&s_chapter==2&&!strcmp(s_reader_notice,"整部漫画已读完"));
 assert(s_reader_notice_until==4000);
 reset(&ctx,0,1);bad_index=1;assert(turn_page(&ctx,1)==APP_REDRAW_NONE&&ctx.request_app==5&&returned_message[0]&&returned_page==2);
 bad_index=-1;reset(&ctx,0,0);s_comic_active=false;
 assert(turn_page(&ctx,-1)==APP_REDRAW_NONE&&s_comic_index==0&&ctx.request_app==0);
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&s_chapter==1&&s_comic_index==0);
 assert(turn_page(&ctx,1)==APP_REDRAW_NONE&&s_comic_index==0);
 puts("PASS: real bidirectional CBZ boundaries with mode preservation across both fullscreen defaults, manual resume, natural series TOC/current highlight, chapter-page Back, explicit Home, bad/missing chapter return, completion and legacy single-volume behavior");
}
'''
with tempfile.TemporaryDirectory(prefix="comics-reader-", dir=ROOT / "build") as folder:
    work = Path(folder)
    (work / "test.c").write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I"+str(ROOT/"main/book"), str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
