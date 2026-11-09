#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：执行真实漫画浏览页：分页、三样式、搜索、整组封面、返回、睡眠和拔卡。
English: Execute the comic browser across pagination, three styles, search, grouped covers, returns, sleep and media loss.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/apps/app_comics.c").read_text()
keyboard_top = next(line for line in (ROOT / "main/ui/ui_keyboard.h").read_text().splitlines()
                    if line.startswith("#define UI_SYSTEM_KEYBOARD_TOP "))
unit = keyboard_top + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_locale_format.h"
#include "book_comics.h"
#include "book_cover.h"
#include "book_cover_jobs.h"
#include "ui_nav_layout.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;} touch_t;
typedef struct {void *hl;uint8_t *fb;int request_app;int64_t now_ms;touch_t *touch;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
typedef enum {UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_LONG_PRESS,UI_GESTURE_TAP,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D,UI_GESTURE_CANCEL} ui_gesture_type_t;
typedef struct {ui_gesture_type_t type;uint16_t x0,y0,x,y;} ui_gesture_event_t;
typedef struct {char *text;size_t cap;} ui_text_edit_t;
typedef enum {UI_KEYBOARD_NONE,UI_KEYBOARD_CHANGED,UI_KEYBOARD_DONE} ui_keyboard_result_t;
typedef struct {EpdRect area;bool layout;} ui_keyboard_update_t;
enum EpdDrawMode {MODE_DU,MODE_GL16,MODE_GC16};
enum {UI_KEY_1=1,UI_KEY_2=2,UI_KEY_3=3,EPD_DRAW_ALIGN_CENTER=0,EPD_DRAW_ALIGN_LEFT=1};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_MARGIN 36
#define UI_GAP 20
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 224
#define UI_PX_BODY 28
#define UI_SHELF_FAST_STRIDE 21
#define UI_SHELF_FAST_BYTES 4494
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_ERR_NOT_FINISHED 20
typedef struct {bool present,mounted;} read_pico_sd_info_t;
static bool mounted=true,probe,oom,fast,invalid_directory;
static unsigned revision=1,cover_draws,list_draws,cover_loads,gray_pixels,commits;
static unsigned header_labels,pager_labels,pager_icons,old_rules,area_pushes,pressed_headers;
static EpdRect pushed_area;
static char drawn_title[256],drawn_detail[256],drawn_progress[288],drawn_name[256],drawn_pager[64],drawn_info[288];
static unsigned keyboard_draws,keyboard_updates,keyboard_presses,keyboard_taps;
static uint8_t setting_style=2;
static uint64_t fingerprint=1;
static char opened_path[288],saved_directory[288];
static int opened_page;static uint32_t saved_page,saved_root_page;
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){(void)mode;assert(!strcmp(ns,"comic_nav"));*h=1;return ESP_OK;}
static void nvs_close(nvs_handle_t h){assert(h==1);}
static esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size){(void)h;assert(!strcmp(key,"directory")&&*size>strlen(saved_directory));strcpy(out,saved_directory);return ESP_OK;}
static esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *out){(void)h;*out=!strcmp(key,"page")?saved_page:saved_root_page;return ESP_OK;}
static esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *in){(void)h;assert(!strcmp(key,"directory"));strcpy(saved_directory,in);return ESP_OK;}
static esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t value){(void)h;if(!strcmp(key,"page"))saved_page=value;else saved_root_page=value;return ESP_OK;}
static esp_err_t nvs_commit(nvs_handle_t h){(void)h;++commits;return ESP_OK;}
unsigned book_store_revision(void){return revision;}
static esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *sd){*sd=(read_pico_sd_info_t){mounted,mounted};return probe?ESP_ERR_NOT_FINISHED:ESP_OK;}
static uint8_t app_settings_shelf_style(void){return setting_style;}
static bool app_settings_main_fast_refresh(void){return fast;}
static unsigned app_settings_system_contrast(void){return 100;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return 8u*1024u*1024u;}
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return oom?NULL:malloc(n);}
static void *heap_caps_calloc(size_t n,size_t size,int caps){(void)caps;return oom?NULL:calloc(n,size);}
static bool read_pico_search_match(const char *name,const char *query){return strstr(name,query)!=NULL;}
static bool app_book_request_comic(const char *path,int page){strcpy(opened_path,path);opened_page=page;return true;}
static void ui_nav_request(app_ctx_t *ctx,int id){ctx->request_app=id;}
static int ui_content_width(void){return 612;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static int ui_text_fixed_width_px(int px,const char *text){return strlen(text)*(unsigned)px/2;}
static int ui_text_effective_px(int px){return px;}
static void ui_text_set_system_scale(bool value){(void)value;}
static void ui_clear_page(uint8_t *fb){(void)fb;}
static void epd_fill_rect(EpdRect r,int gray,uint8_t *fb){(void)gray;(void)fb;if(r.y==195&&r.width==612&&r.height==2)++old_rules;}
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){epd_fill_rect(r,gray,fb);}
static void ui_nav_status(uint8_t *fb){(void)fb;}
static void ui_nav_back(uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;}
static void ui_nav_draw(uint8_t *fb,int id){(void)fb;assert(id==5);}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *s,int align,bool inverse){
 (void)fb;(void)s;(void)align;(void)inverse;
 if((x==36||x==112)&&y==90)strcpy(drawn_title,s);
 if(x<220&&y==449)strcpy(drawn_info,s);
 if((x==490||x==599)&&(y==121||y==123)){assert(px==20);++header_labels;}
 if(x==342&&y==1031){assert(px==21);++pager_labels;strcpy(drawn_pager,s);}
}
#define ui_text_vc ui_text
static void ui_text_box(uint8_t *fb,EpdRect r,int px,const char *text,int align,bool wrap){ui_text(fb,r.x+r.width/2,r.y+r.height/2,px,text,align,wrap);}
static void ui_draw_button(uint8_t *fb,EpdRect r,const char *text,bool selected){(void)fb;(void)r;(void)text;(void)selected;}
static void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,int gray){(void)fb;(void)r;(void)radius;(void)gray;}
#define ui_fill_round_rect ui_draw_round_rect
#define ui_draw_control_frame ui_draw_round_rect
static void ui_shelf_cover_pixels(uint8_t *fb,EpdRect image,const uint8_t *gray,unsigned w,unsigned h,const uint8_t *bits,bool is_fast){(void)fb;(void)image;(void)w;(void)h;(void)is_fast;assert(gray||bits);++cover_draws;}
static void ui_shelf_list_details(uint8_t *fb,int row,const char *name,const char *detail,unsigned pct,const char *label){(void)fb;(void)pct;++list_draws;if(!row){strcpy(drawn_name,name);strcpy(drawn_detail,detail);strcpy(drawn_progress,label);}}
static void ui_shelf_furniture(uint8_t *fb,int style,bool is_fast){(void)fb;(void)style;(void)is_fast;}
typedef int ui_icon_t;
enum {UI_ICON_PAGER_CHEVRON_FIRST,UI_ICON_CHEVRON_LEFT,UI_ICON_CHEVRON_RIGHT,UI_ICON_PAGER_CHEVRON_LAST};
enum {UI_PAGER_NONE=-1,UI_PAGER_FIRST,UI_PAGER_PREVIOUS,UI_PAGER_NEXT,UI_PAGER_LAST};
static void ui_draw_icon(uint8_t *fb,int x,int y,int size,int icon,int gray){(void)fb;(void)gray;(void)icon;assert(y>=1029&&y<=1033&&(size==36||size==42)&&x>=36&&x<=648);++pager_icons;}
static void ui_draw_pressed_round_rect(uint8_t *fb,EpdRect r,int radius){(void)fb;assert(r.width==97&&r.height==54&&radius==18);++pressed_headers;}


static void ui_text_edit_init(ui_text_edit_t *edit,char *text,size_t cap){edit->text=text;edit->cap=cap;}
static void ui_keyboard_begin(ui_text_edit_t *edit,bool ascii){(void)edit;(void)ascii;}
static void ui_keyboard_end(void){}
static bool ui_keyboard_release(void){return true;}
static bool ui_keyboard_pending(void){return false;}
static void ui_keyboard_draw(uint8_t *fb,int top){(void)fb;assert(top==UI_SYSTEM_KEYBOARD_TOP);++keyboard_draws;}
static bool ui_keyboard_press(int x,int y,int top,int64_t ms){(void)x;(void)y;(void)ms;assert(top==UI_SYSTEM_KEYBOARD_TOP);++keyboard_presses;return true;}
static ui_keyboard_result_t ui_keyboard_tap(int x,int y,int top,int64_t ms){(void)ms;assert(top==UI_SYSTEM_KEYBOARD_TOP);++keyboard_taps;return x>=534&&y>=top+430&&y<top+502?UI_KEYBOARD_DONE:UI_KEYBOARD_CHANGED;}
static ui_keyboard_result_t ui_keyboard_idle_tick(bool touch,int64_t ms){(void)touch;(void)ms;return UI_KEYBOARD_NONE;}
static ui_keyboard_update_t ui_keyboard_update(uint8_t *fb,int top,EpdRect field,int px,bool masked,void *suffix,bool force){(void)fb;(void)px;(void)masked;(void)suffix;(void)force;assert(top==UI_SYSTEM_KEYBOARD_TOP);++keyboard_updates;return (ui_keyboard_update_t){.area=field};}
static void ui_text_input_draw(uint8_t *fb,ui_text_edit_t *edit,EpdRect r,int px,bool masked,void *suffix){(void)fb;(void)edit;(void)r;(void)px;(void)masked;(void)suffix;}
static bool ui_text_input_tap(ui_text_edit_t *edit,EpdRect r,int px,bool masked,void *suffix,int x,int y){(void)edit;(void)px;(void)masked;(void)suffix;return ui_rect_hit(r,x,y);}
static int E0470_FOLLOW_WAVEFORM,E0470_WAVEFORM;
static int update_display_area_diff_with(void *hl,void *wave,int mode,EpdRect area){(void)hl;(void)wave;(void)mode;assert(area.width>0);pushed_area=area;++area_pushes;return 0;}
static int update_display_mode(void *hl,int mode){(void)hl;(void)mode;return 0;}
static int update_display_fast_page(void *hl){(void)hl;return 0;}
static void guard_draw_result(void *hl,int error){(void)hl;assert(!error);}
static void display_main_transition_shelf_page(void){}
void book_comics_dispose(book_comic_catalog_t *catalog){free(catalog->entries);memset(catalog,0,sizeof(*catalog));}
esp_err_t book_comics_scan(const char *directory,book_comic_catalog_t *out){
 memset(out,0,sizeof(*out));if(invalid_directory&&directory&&directory[0])return ESP_ERR_NOT_FOUND;
 out->count=directory&&directory[0]?24:12;out->entries=calloc(out->count,sizeof(*out->entries));assert(out->entries);
 for(size_t i=0;i<out->count;++i){book_comic_entry_t *e=&out->entries[i];
  snprintf(e->name,sizeof(e->name),directory&&directory[0]?"%zu":"Series%zu",i+1);
  if(directory&&directory[0])snprintf(e->path,sizeof(e->path),"%s/%zu.cbz",directory,i+1);
  else snprintf(e->path,sizeof(e->path),"/sdcard/comics/Series%zu",i+1);
  e->chapters=24;
 }return ESP_OK;
}
uint64_t book_cover_source_fingerprint(const char *path){(void)path;assert(!"UI fingerprint IO");return 0;}
bool book_cover_load_gray(const char *path,const char *title,const char *author,uint8_t out[BOOK_COVER_W * BOOK_COVER_H],bool allow,bool *pending){(void)path;(void)title;(void)author;*pending=!allow;if(!allow)return false;++cover_loads;memset(out,55,176*240);return true;}
bool book_cover_fast_cache_read(const char *p,const char *t,const char *a,book_fast_cover_key_t key,uint8_t *out,bool *edge){(void)p;(void)t;(void)a;(void)key;(void)out;(void)edge;return false;}
void book_cover_fast_cache_write(const char *p,const char *t,const char *a,book_fast_cover_key_t key,const uint8_t *out,bool edge){(void)p;(void)t;(void)a;(void)key;(void)out;(void)edge;}
'''
unit += r"""
static book_cover_job_t job_requests[9];
static int completed_jobs;
static unsigned job_generation,check_requests;
static bool checks_pending[9],checks_complete;
unsigned book_cover_jobs_begin(void){completed_jobs=0;memset(checks_pending,0,sizeof(checks_pending));return ++job_generation;}
void book_cover_jobs_cancel(unsigned generation){if(generation==job_generation){++job_generation;completed_jobs=0;}memset(checks_pending,0,sizeof(checks_pending));}
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *request){assert(slot<9);if(generation!=job_generation)return false;job_requests[slot]=*request;return true;}
int book_cover_jobs_recheck(unsigned generation,unsigned slot,uint64_t *out){
 assert(slot<9);if(generation!=job_generation)return -1;
 if(!checks_pending[slot]){checks_pending[slot]=true;++check_requests;return 0;}
 if(!checks_complete)return 0;
 checks_pending[slot]=false;*out=fingerprint;return 1;
}
int book_cover_jobs_take(unsigned generation,unsigned slot,book_cover_result_t *result){
 assert(generation==job_generation&&slot<9);if(oom){result->source_fingerprint=fingerprint;return -1;}if(slot>=(unsigned)completed_jobs)return 0;
 book_cover_job_t *q=&job_requests[slot];size_t bytes=q->fast?q->stride*q->height:176u*240u;
 uint8_t *pixels=malloc(bytes);assert(pixels);++cover_loads;memset(pixels,55,bytes);
 *result=(book_cover_result_t){.pixels=pixels,.fast=q->fast,.source_fingerprint=fingerprint};return 1;
}
"""
for name in ("ui_pager_area_at","ui_pager_rect_at","ui_pager_hit_at","ui_pager_area","ui_pager_rect","ui_pager_hit","ui_pager_target","ui_pager_draw_at","ui_pager_draw"):
    unit += function(name,ROOT/"main/ui/ui_kit.c") + "\n"
for name in ("book_cover_crop",):
    unit += function(name, ROOT / "main/book/book_cover.c") + "\n"
for name in ("ui_shelf_rows", "ui_shelf_row", "ui_shelf_image", "ui_shelf_lift", "ui_shelf_feedback", "ui_shelf_list_frame", "ui_shelf_header_rect", "ui_shelf_header_button", "ui_shelf_page_arrow", "ui_shelf_pager_state", "ui_shelf_pager"):
    unit += function(name, ROOT / "main/ui/ui_shelf.c") + "\n"
unit += function("ui_nav_hit", ROOT / "main/ui/ui_nav.c") + "\n"
unit += SOURCE[SOURCE.index("static book_comic_catalog_t s_catalog;"):SOURCE.index("const app_desc_t app_comics =")]
unit += r'''
// 测试仅把固件存储视为只读，用户目录、标题和输入缓冲均保留RAM身份。
// Keep firmware storage readonly in the fixture while preserving RAM identity for user folders, titles and input.
static char mutable_name[64]="漫画";
bool ui_locale_test_readonly(const void *text){
 uintptr_t at=(uintptr_t)text;
#define USER_BUFFER(value) if(at>=(uintptr_t)(value)&&at-(uintptr_t)(value)<sizeof(value))return false
 USER_BUFFER(mutable_name);USER_BUFFER(s_directory);USER_BUFFER(s_requested_directory);
 USER_BUFFER(s_message);USER_BUFFER(s_requested_message);USER_BUFFER(s_query);USER_BUFFER(s_root_query);USER_BUFFER(s_draft);
 USER_BUFFER(job_requests);
#undef USER_BUFFER
 if(s_catalog.entries&&at>=(uintptr_t)s_catalog.entries&&at-(uintptr_t)s_catalog.entries<s_catalog.count*sizeof(*s_catalog.entries))return false;
 return true;
}
static app_redraw_t tap(app_ctx_t *ctx,int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y,.x=x,.y=y};return gesture(ctx,&e);}
int main(void){uint8_t frame[1];app_ctx_t ctx={.fb=frame,.now_ms=1000};
 for(int style=1;style<=6;++style){if(style!=1&&style!=2&&style!=5&&style!=6)continue;setting_style=style;
  for(int mode=0;mode<2;++mode){fast=mode;s_directory[0]=s_query[0]=0;s_page=0;on_enter(&ctx);
   assert(count()==12&&pages()==(style==5?3:2));
   header_labels=pager_labels=pager_icons=old_rules=0;render(&ctx,frame);assert(main_visible(&ctx));
   assert(header_labels==2&&pager_labels==1&&pager_icons==4&&!old_rules);
   EpdRect first=ui_shelf_row(style,0);assert(first.y==(style==5?184:192));
   for(int direction=-1;direction<=1;direction+=2){EpdRect arrow=ui_shelf_page_arrow(direction);
    assert(arrow.y==1006&&arrow.width==88&&arrow.height==50);
    for(int y=arrow.y;y<arrow.y+arrow.height;++y)for(int x=arrow.x;x<arrow.x+arrow.width;++x)assert(hit(x,y)==(direction<0?24:25));
   }
   EpdRect header=ui_shelf_header_rect(1);assert(hit(header.x,header.y)==22&&hit(header.x+96,header.y+53)==22);
   unsigned before_pushes=area_pushes,before_decodes=cover_loads;
   ui_gesture_event_t press={.type=UI_GESTURE_PRESS,.x0=header.x+8,.y0=header.y+8,.x=header.x+8,.y=header.y+8};
   assert(gesture(&ctx,&press)==APP_REDRAW_AREA&&s_pressed==22&&s_feedback_mode==MODE_DU);
   assert(present(&ctx,APP_REDRAW_AREA)&&area_pushes==before_pushes+1&&pushed_area.x==header.x&&pushed_area.y==header.y&&pushed_area.width==97&&pushed_area.height==54);
   press.type=UI_GESTURE_CANCEL;assert(gesture(&ctx,&press)==APP_REDRAW_AREA&&s_pressed==-1);
   assert(present(&ctx,APP_REDRAW_AREA)&&area_pushes==before_pushes+2&&cover_loads==before_decodes);
   cover_draws=list_draws=0;assert(!prepare(false));render(&ctx,frame);assert(!cover_draws);
   unsigned loaded=cover_loads;for(int row=0;row<ui_shelf_rows(style);++row){completed_jobs=row+1;bool revealed=prepare(true);assert(revealed==(row+1==ui_shelf_rows(style)));}
   assert(cover_loads-loaded==(unsigned)ui_shelf_rows(style)&&ready());render(&ctx,frame);assert(cover_draws==(unsigned)ui_shelf_rows(style));
   if(style==5)assert(list_draws==8);
   assert(turn(1)==APP_REDRAW_PAGE&&s_page==1);render(&ctx,frame);
   EpdRect row=ui_shelf_row(style,0);assert(tap(&ctx,row.x+8,row.y+8)==APP_REDRAW_PAGE&&s_directory[0]);
   assert(s_root_page==1&&s_page==0&&count()==24);
   assert(turn(1)==APP_REDRAW_PAGE);int original=s_page;
   assert(tap(&ctx,row.x+8,row.y+8)==APP_REDRAW_NONE&&ctx.request_app==1&&opened_page==original&&strstr(opened_path,".cbz"));
   char directory[288];strcpy(directory,s_directory);comics_on_exit(&ctx);
   app_comics_request_chapters(directory,original,"");on_enter(&ctx);assert(s_page==original&&!strcmp(directory,s_directory));
   before_lock(&ctx);assert(saved_page==(unsigned)original&&saved_root_page==1);
   s_directory[0]=0;s_page=0;s_root_page=0;assert(app_comics_request_resume(directory));on_enter(&ctx);
   assert(s_page==original&&s_root_page==1&&!strcmp(directory,s_directory));
   assert(back(&ctx)==APP_REDRAW_PAGE&&!s_directory[0]&&s_page==1);
   comics_on_exit(&ctx);
  }
 }
 setting_style=5;fast=false;app_comics_request_chapters("/sdcard/comics/Series1",2,"");on_enter(&ctx);
 assert(tap(&ctx,490,120)==APP_REDRAW_PAGE&&s_search&&!main_visible(&ctx));strcpy(s_draft,"2");finish_search();assert(count()==7&&s_page==0);
 strcpy(s_draft,"");finish_search();assert(count()==24);
 assert(key(&ctx,UI_KEY_2)==APP_REDRAW_NONE&&ctx.request_app==0);
 before_lock(&ctx);assert(commits>0);invalid_directory=true;scan();assert(!s_directory[0]&&s_message[0]);invalid_directory=false;
 oom=true;assert(prepare(true));assert(ready());drop_covers();oom=false;
 media_lost(&ctx);assert(!s_directory[0]&&!s_catalog.entries&&!s_catalog.count&&s_message[0]);
 mounted=false;scan();assert(!count()&&s_message[0]);mounted=true;media_ready(&ctx);ctx.now_ms+=3000;assert(tick(&ctx)==APP_REDRAW_PAGE&&count()==12);
 // 加载中锁屏后模拟队列睡眠失效，不重新进入页面也应完成封面。/ Lock mid-load, invalidate for sleep, then finish without re-entering.
 assert(!prepare(false));completed_jobs=1;assert(!prepare(true)&&s_covers[0].ready);
 unsigned sleeping_generation=s_cover_generation;before_lock(&ctx);book_cover_jobs_cancel(job_generation);
 assert(!s_cover_generation&&!s_cover_checks&&!s_covers[0].ready);
 assert(tick(&ctx)==APP_REDRAW_NONE&&s_cover_generation!=sleeping_generation);
 completed_jobs=9;assert(tick(&ctx)==APP_REDRAW_PAGE&&ready());
 unsigned before_checks=check_requests,before_loads=cover_loads;
 ctx.now_ms+=2100;assert(tick(&ctx)==APP_REDRAW_NONE&&s_cover_checks);
 assert(check_requests==before_checks+4&&cover_loads==before_loads);
 for(int poll=0;poll<100;++poll)assert(tick(&ctx)==APP_REDRAW_NONE&&cover_loads==before_loads);
 checks_complete=true;assert(tick(&ctx)==APP_REDRAW_NONE&&!s_cover_checks&&ready());
 ++fingerprint;ctx.now_ms+=2100;assert(tick(&ctx)==APP_REDRAW_NONE&&s_cover_checks);
 assert(tick(&ctx)==APP_REDRAW_PAGE&&!s_cover_checks&&!s_cover_generation&&!ready());
 assert(tick(&ctx)==APP_REDRAW_NONE);completed_jobs=9;assert(tick(&ctx)==APP_REDRAW_PAGE&&ready());
 ctx.now_ms+=2100;checks_complete=false;assert(tick(&ctx)==APP_REDRAW_NONE&&s_cover_checks);
 assert(turn(1)==APP_REDRAW_PAGE&&!s_cover_checks);assert(tick(&ctx)==APP_REDRAW_NONE&&!ready());
 app_comics_cover_mode_changed();assert(!s_covers[0].gray&&!s_covers[0].bits);comics_on_exit(&ctx);
 app_comics_request_chapters("",0,"");on_enter(&ctx);app_redraw_t redraw;
 unsigned loaded=cover_loads;
 assert(main_key(&ctx,UI_KEY_2,&redraw)&&s_key_mode&&redraw==APP_REDRAW_AREA);
 assert(main_key(&ctx,UI_KEY_3,&redraw)&&s_key_delta==1&&cover_loads==loaded);
 ctx.now_ms+=40;assert(tick(&ctx)==APP_REDRAW_PAGE&&s_page==1&&!s_key_pressed);
 assert(main_key(&ctx,UI_KEY_3,&redraw));ctx.now_ms+=40;assert(tick(&ctx)==APP_REDRAW_PAGE&&s_page==2);
 assert(main_key(&ctx,UI_KEY_3,&redraw));ctx.now_ms+=40;assert(tick(&ctx)==APP_REDRAW_AREA&&s_page==2);
 assert(main_key(&ctx,UI_KEY_1,&redraw));before_lock(&ctx);assert(!s_key_delta&&!s_key_pressed);
 assert(main_key(&ctx,UI_KEY_2,&redraw)&&!s_key_mode);
 s_search=true;assert(!main_key(&ctx,UI_KEY_2,&redraw)&&edge_back(&ctx));
 ui_gesture_event_t swipe={.y0=700};assert(!back_swipe_area(&ctx,&swipe));swipe.y0=400;assert(back_swipe_area(&ctx,&swipe));
 swipe.y0=UI_SYSTEM_KEYBOARD_TOP;assert(!back_swipe_area(&ctx,&swipe));--swipe.y0;assert(back_swipe_area(&ctx,&swipe));
 render(&ctx,frame);assert(keyboard_draws);
 // 底排按下和抬起交给键盘，不能命中常驻导航；四条键盘调用路径共用同一位置。
 // Bottom-row presses/releases belong to the keyboard, not persistent navigation; all four call paths share placement.
 unsigned before_updates=keyboard_updates;
 const int bottom_x[]={300,480,600};
 for(unsigned i=0;i<3;++i){int x=bottom_x[i],y=UI_SYSTEM_KEYBOARD_TOP+501;
  assert(ui_nav_hit(x,y)==-1);
  ui_gesture_event_t press={.type=UI_GESTURE_PRESS,.x0=x,.y0=y,.x=x,.y=y};
  assert(gesture(&ctx,&press)==APP_REDRAW_AREA&&s_search);
  app_redraw_t result=tap(&ctx,x,y);assert(result==(i==2?APP_REDRAW_PAGE:APP_REDRAW_AREA));
 }
 assert(!s_search&&keyboard_presses==3&&keyboard_taps==3&&keyboard_updates>before_updates);
 comics_on_exit(&ctx);
 // 真实翻译器先处理固件格式和复制；与文案同名的用户目录、文件名和输入不能被翻译。
 // Translate firmware formats/copies with the real locale while preserving matching user folder/file names and input.
 for(int english=0;english<2;++english){
  ui_locale_set_english(english);setting_style=5;app_comics_request_chapters("",0,"");on_enter(&ctx);
  strcpy(s_catalog.entries[0].name,"漫画");strcpy(s_catalog.entries[0].latest,"搜索");
  render(&ctx,frame);
  assert(!strcmp(drawn_title,english?"Comics":"漫画")&&!strcmp(drawn_name,"漫画"));
  assert(!strcmp(drawn_detail,english?"24 chapters":"24 章"));
  assert(!strcmp(drawn_progress,english?"Latest: 搜索":"最近：搜索"));
  assert(strstr(drawn_pager,english?"12 comics":"12部漫画"));
  s_style=2;render(&ctx,frame);assert(!strcmp(drawn_info,english?"24 chapters · 搜索":"24 章 · 搜索"));
  s_style=5;app_comics_request_chapters("/sdcard/comics/漫画",0,"");on_enter(&ctx);
  strcpy(s_catalog.entries[0].name,"刷新");s_catalog.entries[0].has_progress=true;s_catalog.entries[0].pct=37;
  render(&ctx,frame);assert(!strcmp(drawn_title,"漫画")&&!strcmp(drawn_name,"刷新"));
  assert(!strcmp(drawn_detail,english?"Chapter progress":"章节进度"));
  assert(!strcmp(drawn_progress,english?"Read 37%":"已读 37%"));
  assert(strstr(drawn_pager,english?"24 chapter":"24章"));
  s_catalog.entries[0].has_progress=false;render(&ctx,frame);
  assert(!strcmp(drawn_progress,english?"Not started · 37%":"未开始 · 37%"));
  invalid_directory=true;scan();invalid_directory=false;
  assert(!strcmp(s_message,english?"Comic folder unavailable; returned to Comics":"漫画目录已失效，已返回漫画书架"));
  mounted=false;scan();mounted=true;
  assert(!strcmp(s_message,english?"TF card unavailable; remount it":"TF 卡不可用，请重新挂载"));
  copy(s_draft,sizeof(s_draft),mutable_name);assert(!strcmp(s_draft,"漫画"));
  app_comics_request_chapters("",0,mutable_name);on_enter(&ctx);assert(!strcmp(s_message,"漫画"));
  comics_on_exit(&ctx);
 }
 ui_locale_set_english(false);
 puts("PASS: actual comic browser, keyboard/nav separation, bilingual labels/counts/progress/notices with user data preserved, shared header/pager, four gray/BW styles, grouped covers, search, return pages, sleep and media cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix="comics-ui-", dir=ROOT / "build") as folder:
    work = Path(folder)
    (work / "test.c").write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
                    "-fsanitize=address,undefined", "-DKIIKO_LOCALE_TEST=1",
                    "-I" + str(ROOT / "components/kiiko_locale/include"), "-I" + str(ROOT / "tools/book_epub_stubs"),
                    "-I" + str(ROOT / "main/book"), "-I" + str(ROOT / "main/ui"),
                    str(work / "test.c"), str(ROOT / "components/kiiko_locale/ui_locale.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
