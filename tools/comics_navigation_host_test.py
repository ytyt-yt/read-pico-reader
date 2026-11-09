#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：运行真实五栏绘制、全宽命中、反馈及稳定页面编号。
English: Exercise real five-tab drawing, full-width hits, feedback and stable page IDs.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
NAV = ROOT / "main/ui/ui_nav.c"
REGISTRY = ROOT / "main/app/app_registry.c"
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_nav_layout.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 224
#define UI_CLICK_NAV 2
#define UI_NAV_TAB_ICON_PX 44
#define APP_MAIN_REFRESH_NORMAL 0
#define EPD_DRAW_ALIGN_CENTER 0
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int id;} app_desc_t;
typedef struct {const app_desc_t *request_app;} app_ctx_t;
typedef enum {UI_ICON_HOUSE,UI_ICON_LIBRARY_BIG,UI_ICON_BOOK_OPEN,UI_ICON_FOLDER_OPEN,UI_ICON_SLIDERS_HORIZONTAL} ui_icon_t;
static const app_desc_t app_dashboard={0},app_book={1},app_files={2},app_device_settings={3},app_image={4},app_comics={5};
static const char *const labels[]={"首页","书架","漫画","文件管理","设置"};
static unsigned drawn,registered,texts,markers;
static int mode,active,marker_center;
static int app_settings_main_refresh_mode(void){return mode;}
static void epd_fill_rect(EpdRect r,int gray,uint8_t *fb){(void)fb;assert(r.x>=0&&r.x+r.width<=684);if(gray==0&&r.y==UI_NAV_TOP+1){++markers;marker_center=r.x+r.width/2;}}
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){epd_fill_rect(r,gray,fb);}
static void ui_hairline(uint8_t *fb,int y,int x,int width,int gray){(void)fb;(void)y;(void)x;(void)width;(void)gray;}
static void ui_click_feedback_register(uint8_t *fb,EpdRect r,int kind,int icon){(void)fb;(void)icon;assert(kind==UI_CLICK_NAV&&r.x>=0&&r.x+r.width<684);++registered;}
static void ui_draw_icon(uint8_t *fb,int cx,int y,int size,int icon,int gray){(void)fb;(void)y;(void)icon;(void)gray;assert(size==44&&cx==(int)(drawn*2+1)*684/10);++drawn;}
static void ui_text(uint8_t *fb,int x,int y,int size,const char *text,int align,bool inverse){(void)fb;(void)x;(void)y;(void)size;(void)align;(void)inverse;assert(!strcmp(text,labels[texts++]));}
'''
registry = REGISTRY.read_text()
unit += registry[registry.index("static const app_desc_t* const s_apps[]"):registry.index("int app_count(")]
for name in ("app_at", "app_index_of"):
    unit += function(name, REGISTRY) + "\n"
for name in ("icon", "ui_nav_draw", "ui_nav_hit", "ui_nav_tab_rect", "ui_nav_request"):
    unit += function(name, NAV) + "\n"
unit += r'''
int main(void){uint8_t frame[1]={0};int expected[]={0,1,5,2,3};
 for(int slot=0;slot<5;++slot){
  EpdRect r=ui_nav_tab_rect(expected[slot]);assert(r.x==slot*684/5&&r.width==(slot+1)*684/5-r.x);
  for(int x=r.x;x<r.x+r.width;++x)for(int y=UI_NAV_TOUCH_TOP;y<1216;++y)assert(ui_nav_hit(x,y)==expected[slot]);
  app_ctx_t ctx={0};ui_nav_request(&ctx,expected[slot]);assert(ctx.request_app->id==expected[slot]);
  assert(app_index_of(ctx.request_app)==expected[slot]);
 }
 assert(ui_nav_hit(684,1100)==-1&&ui_nav_hit(0,UI_NAV_TOUCH_TOP-1)==-1&&ui_nav_hit(0,1216)==-1);
 assert(ui_nav_tab_rect(4).width==0);
 for(mode=0;mode<3;++mode)for(int slot=0;slot<5;++slot){
  drawn=registered=texts=markers=0;active=expected[slot];ui_nav_draw(frame,active);
  assert(drawn==5&&registered==5&&texts==5&&markers==1&&marker_center==(slot*2+1)*684/10);
 }
 assert(app_at(0)==&app_dashboard&&app_at(1)==&app_book&&app_at(2)==&app_files&&app_at(3)==&app_device_settings&&app_at(4)==&app_image&&app_at(5)==&app_comics);
 puts("PASS: five drawn tabs, every hit pixel, local feedback, selected marker, three refresh modes and unchanged persistent IDs 0..4");
}
'''
with tempfile.TemporaryDirectory(prefix="comics-nav-", dir=ROOT / "build") as folder:
    work = Path(folder)
    (work / "test.c").write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "main/ui"), str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)

feedback = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui_nav_layout.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_TOUCH_SLOP_PX 18
#define UI_GRAY_BLACK 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(n,c) malloc(n)
#define heap_caps_free free
typedef struct {int x,y,width,height;} EpdRect;
typedef int ui_icon_t;
typedef enum {UI_CLICK_BACK,UI_CLICK_BACK_SMALL,UI_CLICK_NAV} ui_click_kind_t;
enum {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
enum {UI_ICON_HOUSE,UI_ICON_LIBRARY_BIG,UI_ICON_BOOK_OPEN,UI_ICON_FOLDER_OPEN,UI_ICON_SLIDERS_HORIZONTAL};
static uint8_t frame[684u*1216u/2],baseline[sizeof(frame)];
static int selected_icon;
static int epd_width(void){return 684;}
static int epd_height(void){return 1216;}
static int epd_rotated_display_width(void){return 684;}
static int epd_rotated_display_height(void){return 1216;}
static int epd_get_rotation(void){return EPD_ROT_LANDSCAPE;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){
 assert(w==684&&h==1216&&x>=0&&x<w&&y>=0&&y<h);
 return (uint8_t)((fb[y*342+x/2]>>((x&1)*4)&15)<<4);
}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 assert(x>=0&&x<684&&y>=0&&y<1216);unsigned shift=(x&1)*4;
 uint8_t *p=fb+y*342+x/2;*p=(*p&~(15u<<shift))|((gray>>4)<<shift);
}
static void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *fb){
 for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)epd_draw_pixel(x,y,gray,fb);
}
static void ui_draw_icon(uint8_t *fb,int x,int y,int size,ui_icon_t icon,uint8_t gray){
 selected_icon=icon;epd_fill_rect((EpdRect){x-1,y-1,size/8,size/8},gray,fb);
}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)radius;epd_fill_rect(r,gray,fb);}
static bool app_settings_main_fast_refresh(void){return false;}
static void ui_image_bw_rect(uint8_t *fb,EpdRect r){(void)fb;(void)r;}
'''
feedback += '\n'.join(line for line in (ROOT/'main/ui/ui_click_feedback.c').read_text().splitlines()
                      if not line.startswith('#include'))
feedback += r'''
int main(void){
 EpdRect changed;
 for(int slot=0;slot<UI_NAV_COUNT;++slot){
  int page=ui_nav_page_at(slot),cx=(slot*2+1)*684/(2*UI_NAV_COUNT);
  memset(frame,0xee,sizeof(frame));memcpy(baseline,frame,sizeof(frame));
  ui_click_feedback_begin(frame);
  for(int i=0;i<UI_NAV_COUNT;++i){int center=(i*2+1)*684/(2*UI_NAV_COUNT);
   ui_click_feedback_register(frame,(EpdRect){center-30,1111,60,60},UI_CLICK_NAV,i);}
  int left=slot*684/UI_NAV_COUNT,right=(slot+1)*684/UI_NAV_COUNT;
  for(int x=left;x<right;++x){
   assert(ui_click_feedback_press(frame,x,UI_NAV_TOUCH_TOP,&changed));
   assert(selected_icon==slot&&!ui_click_feedback_cancel_at(x,UI_NAV_TOUCH_TOP));
   if(left)assert(ui_click_feedback_cancel_at(left-1,UI_NAV_TOUCH_TOP));
   if(right<684)assert(ui_click_feedback_cancel_at(right,UI_NAV_TOUCH_TOP));
   assert(ui_click_feedback_release(frame,&changed)&&!memcmp(frame,baseline,sizeof(frame)));
  }
  ui_click_feedback_nav_request(page);
  assert(ui_click_feedback_nav_tick(frame,100,&changed)&&selected_icon==slot);
  assert(changed.x>=cx-30&&changed.x+changed.width<=cx+30);
  assert(ui_click_feedback_nav_tick(frame,165,&changed)&&!memcmp(frame,baseline,sizeof(frame)));
  ui_click_feedback_reset();ui_click_feedback_begin(frame);
  ui_click_feedback_nav_request(page);
  assert(ui_click_feedback_nav_tick(frame,200,&changed)&&selected_icon==slot);
  assert(ui_click_feedback_nav_tick(frame,265,&changed)&&!memcmp(frame,baseline,sizeof(frame)));
  ui_click_feedback_reset();
 }
 ui_click_feedback_nav_request(4);assert(!ui_click_feedback_nav_pending());
 puts("PASS: real five-tab feedback across every uneven slot pixel, comic stable ID, delayed registrations, local pulses and exact pixel restoration");
}
'''
with tempfile.TemporaryDirectory(prefix='comics-nav-feedback-',dir=ROOT/'build') as folder:
    work=Path(folder);(work/'test.c').write_text(feedback)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(ROOT/'main/ui'),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
