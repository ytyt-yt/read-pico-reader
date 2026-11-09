"""Exercise final wallpaper cache integrity and avatar pixel reuse with actual painters.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'main/ui/ui_wallpaper.c'
unit=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui_image_dither.h"
#include "ui_wallpaper_crop.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define BOOK_IMAGE_FILE_MAX (6u*1024u*1024u)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define UI_WALLPAPER_CACHE_PARENT "cache"
#define WALL_SET_CACHE_PARENT "cache"
typedef struct {int x,y,width,height;} EpdRect;
static bool fat_semantics;
static unsigned renames,rename_fail_at;
static int fat_rename(const char *a,const char *b){
 ++renames;if(renames==rename_fail_at){errno=EIO;return -1;}
 struct stat st;if(fat_semantics&&!stat(b,&st)){errno=EEXIST;return -1;}return rename(a,b);
}
#define rename fat_rename

static unsigned source_w=684,source_h=1216,loads,rotation,variant;
static bool alloc_fail,decode_fail,source_mutates;
static unsigned warm_polls,cancel_at,decode_rows;
static size_t heap_caps_get_free_size(int caps){assert(caps==3);return 8u*1024u*1024u;}
static bool warm_active(void){return true;}
static bool warm_cancelled(void){return false;}
static bool warm_mid_cancel(void){return ++warm_polls<cancel_at;}
static char source_file[128]="source.jpg";
static int test_stat(const char *path,struct stat *out){
 assert(!strncmp(path,"/sdcard/",8));
 return stat(source_file,out);
}
#define stat(path,out) test_stat(path,out)
static void *heap_caps_malloc(size_t n,int caps){assert(caps==3&&n<=684u*1216u);return alloc_fail?NULL:malloc(n);}
static bool book_image_file_dimensions(const char *path,bool png,unsigned *w,unsigned *h){(void)path;(void)png;*w=source_w;*h=source_h;return true;}
static bool book_image_file_grayscale(const char *path,bool png,unsigned w,unsigned h,uint8_t *out){
 (void)path;(void)png;++loads;if(decode_fail)return false;
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
  uint8_t value=(uint8_t)(x*19+y*31+variant);
  if(w<200&&(x<w/4||x>3*w/4||y<h/4||y>3*h/4))value=255;
  out[(size_t)y*w+x]=value;
 }
 if(source_mutates){FILE *f=fopen(source_file,"ab");assert(f);assert(fputc(17,f)!=EOF);assert(!fclose(f));}
 return true;
}
static bool book_image_file_grayscale_active(const char *path,bool png,unsigned w,unsigned h,uint8_t *out,bool (*active)(void)){
 assert(active);decode_rows=0;
 for(unsigned y=0;y<h;++y){if(!active())return false;++decode_rows;}
 return book_image_file_grayscale(path,png,w,h,out);
}
static unsigned epd_get_rotation(void){return rotation;}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 assert(x>=0&&x<684&&y>=0&&y<1216);
 size_t n=(size_t)y*684+x;unsigned shift=(n&1)*4;
 fb[n/2]=(fb[n/2]&~(15u<<shift))|((gray>>4)<<shift);
}
'''
for name in ('inside_rounded','avatar_content_crop','image_pixels','ui_wallpaper_draw','ui_wallpaper_draw_rounded','ui_wallpaper_draw_avatar'):
 unit+=function(name,source)+'\n'
unit+=source.read_text()[source.read_text().index('#define WALL_LOCK_CACHE'):]
unit+=r'''
static uint8_t frame[WALL_LOCK_BYTES],expected[WALL_LOCK_BYTES],second[WALL_LOCK_BYTES],avatar[82*82];
static void set_source(const char *name,size_t n){snprintf(source_file,sizeof(source_file),"%s",name);FILE *f=fopen(name,"wb");assert(f);for(size_t i=0;i<n;++i)assert(fputc((int)i,f)!=EOF);assert(!fclose(f));}
static void clean_expected(void){memset(expected,255,sizeof(expected));assert(ui_wallpaper_draw(expected,"/sdcard/wallpaper.jpg",(EpdRect){0,0,684,1216}));}
static void cache_miss(void){unsigned before=loads;memset(frame,255,sizeof(frame));assert(ui_wallpaper_prepare_lock(frame,"/sdcard/wallpaper.jpg"));assert(loads==before+1&&!memcmp(frame,expected,sizeof(frame)));}
static void cache_hit(void){unsigned before=loads;memset(frame,0,sizeof(frame));assert(ui_wallpaper_prepare_lock(frame,"/sdcard/wallpaper.jpg"));assert(loads==before&&!memcmp(frame,expected,sizeof(frame)));}
static void damage(size_t offset){FILE *f=fopen(WALL_LOCK_CACHE,"r+b");assert(f&&!fseek(f,(long)offset,SEEK_SET));int c=fgetc(f);assert(c!=EOF&&!fseek(f,-1,SEEK_CUR));assert(fputc(c^0x55,f)!=EOF&&!fclose(f));}
int main(void){
 set_source("source.jpg",5000);clean_expected();
 cache_miss();ui_wallpaper_cache_commit(frame);
 struct stat cached;assert(!stat("/sdcard/source.jpg",&cached));
 #undef stat
 assert(!stat(WALL_LOCK_CACHE,&cached)&&cached.st_size==sizeof(wallpaper_cache_t)+WALL_LOCK_BYTES);
 #define stat(path,out) test_stat(path,out)
 for(int i=0;i<20;++i)cache_hit();
 wallpaper_cache_t prior_header;
 FILE *prior=fopen(WALL_LOCK_CACHE,"rb");assert(prior&&fread(&prior_header,1,sizeof(prior_header),prior)==sizeof(prior_header)&&!fclose(prior));
 memcpy(second,frame,sizeof(second));second[0]^=17;
 fat_semantics=true;renames=0;rename_fail_at=3;
 wallpaper_store(second,prior_header,WALL_LOCK_CACHE);rename_fail_at=0;cache_hit();
 wallpaper_store(expected,prior_header,WALL_LOCK_CACHE);cache_hit();fat_semantics=false;

 alloc_fail=decode_fail=true;cache_hit();alloc_fail=decode_fail=false;
 damage(sizeof(wallpaper_cache_t)+8913);cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();
 FILE *f=fopen(WALL_LOCK_CACHE,"wb");assert(f);assert(fwrite("partial",1,7,f)==7&&!fclose(f));
 cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();
 f=fopen(WALL_LOCK_CACHE,"ab");assert(f);assert(fputc(17,f)!=EOF&&!fclose(f));
 cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();
 for(rotation=1;rotation<4;++rotation){cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();}
 rotation=0;variant=59;set_source("replacement.jpg",6000);clean_expected();cache_miss();
 ui_wallpaper_cache_commit(second);cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();
 set_source("replacement.jpg",6100);source_mutates=true;cache_miss();source_mutates=false;
 ui_wallpaper_cache_commit(frame);cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();
 assert(!ui_wallpaper_prepare_lock(frame,"/sdcard/no-extension"));
 assert(!ui_wallpaper_prepare_lock(frame,"/other/wallpaper.jpg"));
 set_source("replacement.jpg",6200);decode_fail=true;
 assert(!ui_wallpaper_prepare_lock(frame,"/sdcard/wallpaper.jpg"));decode_fail=false;
 cache_miss();assert(!remove(WALL_LOCK_CACHE));assert(!rmdir("cache/locks"));assert(!rmdir("cache"));
 f=fopen("cache","wb");assert(f&&!fclose(f));ui_wallpaper_cache_commit(frame);assert(!remove("cache"));
 cache_miss();ui_wallpaper_cache_commit(frame);cache_hit();
 const char *a="/sdcard/pictures/wallpapers/a.jpg",*b="/sdcard/pictures/wallpapers/b.png";
 unsigned warm_before=loads;
 assert(ui_wallpaper_prepare_lock(frame,a));ui_wallpaper_cache_commit(frame);
 assert(ui_wallpaper_prepare_lock(frame,b));ui_wallpaper_cache_commit(frame);
 assert(loads==warm_before+2);
 assert(ui_wallpaper_prepare_lock(frame,a)&&loads==warm_before+2);
 ui_wallpaper_warm_cache(b,warm_active);assert(loads==warm_before+2);
 ui_wallpaper_warm_cache("/sdcard/pictures/wallpapers/cancelled.jpg",warm_cancelled);assert(loads==warm_before+2);
 warm_polls=0;cancel_at=40;warm_before=loads;
 ui_wallpaper_warm_cache("/sdcard/pictures/wallpapers/interrupted.png",warm_mid_cancel);
 assert(decode_rows>0&&decode_rows<source_h&&loads==warm_before);
 ui_wallpaper_warm_cache("/sdcard/pictures/wallpapers/interrupted.png",warm_active);
 assert(loads==warm_before+1);
 ui_wallpaper_warm_cache("/sdcard/pictures/wallpapers/interrupted.png",warm_active);
 assert(loads==warm_before+1);
 // 预热另一张不覆盖前台待提交帧。/ Warming another image never overwrites the pending foreground commit.
 assert(ui_wallpaper_prepare_lock(frame,"/sdcard/pictures/wallpapers/c.jpg"));
 ui_wallpaper_warm_cache("/sdcard/pictures/wallpapers/d.jpg",warm_active);ui_wallpaper_cache_commit(frame);
 warm_before=loads;assert(ui_wallpaper_prepare_lock(frame,"/sdcard/pictures/wallpapers/c.jpg")&&loads==warm_before);
 source_w=180;source_h=280;
 memset(expected,255,sizeof(expected));memset(frame,255,sizeof(frame));
 EpdRect area={58,175,82,82};
 assert(ui_wallpaper_draw_rounded(expected,"/sdcard/avatar.png",area,22));
 assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82));
 unsigned before=loads;ui_wallpaper_draw_avatar(frame,avatar,area,22);
 assert(loads==before&&!memcmp(frame,expected,sizeof(frame)));
 alloc_fail=decode_fail=true;before=loads;memset(avatar,0,sizeof(avatar));assert(ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82)&&loads==before);
 alloc_fail=decode_fail=false;ui_wallpaper_draw_avatar(frame,avatar,area,22);assert(!memcmp(frame,expected,sizeof(frame)));
 uint8_t small[60*60];assert(!ui_wallpaper_avatar_cached("/sdcard/avatar.png",small,60));assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",small,60));assert(ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));
 f=fopen("cache/avatars/avatar-82.bin","r+b");assert(f&&!fseek(f,sizeof(wallpaper_cache_t)+111,SEEK_SET));int damaged=fgetc(f);assert(damaged!=EOF&&!fseek(f,-1,SEEK_CUR)&&fputc(damaged^0x55,f)!=EOF&&!fclose(f));assert(!ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));before=loads;assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82)&&loads==before+1);
 f=fopen("cache/avatars/avatar-82.bin","wb");assert(f&&fwrite("partial",1,7,f)==7&&!fclose(f));assert(!ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82));
 assert(!ui_wallpaper_avatar_cached("/sdcard/other.png",avatar,82));
 set_source("replacement.jpg",6300);assert(!ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));alloc_fail=true;assert(!ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82));alloc_fail=false;
 source_mutates=true;assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82));source_mutates=false;assert(!ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));assert(ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,82));
 assert(!remove(source_file)&&!ui_wallpaper_avatar_cached("/sdcard/avatar.png",avatar,82));

 assert(!ui_wallpaper_prepare_avatar("/sdcard/avatar.png",avatar,129));
 puts("PASS: pixel-identical 16-gray/dithered wallpaper disk hits without decode/allocation; checksum/truncation/trailing bytes/source/rotation invalidation; bounded failed writes and decode fallback; avatar cached vs original rounded paint");
}
'''
# Cache paths are confined to a temporary directory; source stat redirects only SD test paths.
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
 d=Path(folder);c=d/'test.c';exe=d/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main/ui'),str(c),str(ROOT/'main/ui/ui_image_dither.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],cwd=d,check=True)
