#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实壁纸洗牌/提交/重启/坏图/媒体边界。/ English: Actual shuffle, commit, reboot, corrupt images and media boundaries."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/wallpaper_rotation.c').read_text()
source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "wallpaper_rotation.h"
#define WALLPAPER_ROTATION_ROOT "statecache"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_IMAGE_FILE_MAX (50u*1024u*1024u)
static bool random_enabled,media_ready=true,deny_write,decode_bad;
static unsigned decodes;
static const char *fixed="/sdcard/fixed.jpg";
static bool app_settings_wallpaper_random(void){return random_enabled;}
static const char *app_settings_wallpaper_path(void){return fixed;}
static bool read_pico_sd_io_ready(void){return media_ready;}
static void *heap_caps_calloc(size_t n,size_t bytes,int caps){assert(caps==3);return calloc(n,bytes);}
static uint32_t esp_random(void){static uint32_t value=487131;value^=value<<13;value^=value>>17;value^=value<<5;return value;}
static const char *map(const char *path,char buffer[1024]){if(!strncmp(path,"/sdcard/",8)){snprintf(buffer,1024,"card/%s",path+8);return buffer;}return path;}
static int path_stat(const char *p,struct stat *s){char b[1024];return stat(map(p,b),s);}
static FILE *path_open(const char *p,const char *mode){char b[1024];if(deny_write&&strchr(mode,'w')){errno=EIO;return NULL;}return fopen(map(p,b),mode);}
static DIR *path_dir(const char *p){char b[1024];return opendir(map(p,b));}
static bool ui_wallpaper_prepare_lock(uint8_t *frame,const char *path){
 ++decodes;if(!path[0])return false;FILE *f=path_open(path,"rb");if(!f)return false;
 int value=fgetc(f);fclose(f);if(value=='X'||(decode_bad&&strcmp(path,fixed)))return false;
 *frame=(uint8_t)value;return true;
}
#define stat(p,s) path_stat(p,s)
#define fopen(p,m) path_open(p,m)
#define opendir(p) path_dir(p)
'''+source+r'''
static void image(unsigned n,char value){char path[128];snprintf(path,sizeof(path),"card/pictures/wallpapers/%03u.jpg",n);FILE*f=fopen(path,"wb");assert(f&&fputc(value,f)!=EOF&&!fclose(f));}
static void new_frame(uint8_t *f){wallpaper_rotation_begin();assert(wallpaper_rotation_prepare(f));}
int main(void){
 assert(!mkdir("card",0775)&&!mkdir("card/pictures",0775)&&!mkdir("card/pictures/wallpapers",0775));
 FILE*f=fopen("card/fixed.jpg","wb");assert(f&&fputc('F',f)!=EOF&&!fclose(f));uint8_t frame=0;
 assert(wallpaper_rotation_prepare(&frame)&&frame=='F');
 random_enabled=true;assert(!wallpaper_rotation_refresh());new_frame(&frame);assert(frame=='F');wallpaper_rotation_finish(true);
 for(unsigned i=0;i<80;++i)image(i,'A'+i%20);
 assert(wallpaper_rotation_refresh()==80&&!wallpaper_rotation_overflow());
 bool seen[80]={0};char previous[288]="";
 for(unsigned i=0;i<80;++i){new_frame(&frame);const char *p=wallpaper_rotation_path();unsigned index;assert(sscanf(strrchr(p,'/')+1,"%u",&index)==1&&index<80&&!seen[index]);seen[index]=true;snprintf(previous,sizeof(previous),"%s",p);wallpaper_rotation_finish(true);}
 new_frame(&frame);assert(strcmp(previous,wallpaper_rotation_path()));
 char pending[288];snprintf(pending,sizeof(pending),"%s",wallpaper_rotation_path());
 wallpaper_rotation_finish(false);assert(!strcmp(previous,wallpaper_rotation_path()));
 new_frame(&frame);assert(!strcmp(pending,wallpaper_rotation_path()));wallpaper_rotation_finish(true);
 unsigned cursor=s_saved->cursor;wallpaper_rotation_invalidate();assert(!strcmp(pending,wallpaper_rotation_path())&&s_saved->cursor==cursor);
 // 唤醒重复绘图不消费队列。/ Repeated wake artwork never consumes the queue.
 for(unsigned i=0;i<4;++i){assert(wallpaper_rotation_prepare(&frame));assert(s_saved->cursor==cursor&&!strcmp(pending,wallpaper_rotation_path()));}
 decode_bad=true;assert(wallpaper_rotation_prepare(&frame)&&frame=='F');
 assert(!strcmp(fixed,wallpaper_rotation_path())&&s_saved->cursor==cursor);decode_bad=false;
 // 只有成功推屏才持久化；只读卡仍能在本次运行轮换。/ Presentation gates persistence; read-only media still rotates in this session.
 new_frame(&frame);deny_write=true;wallpaper_rotation_finish(true);deny_write=false;
 wallpaper_rotation_invalidate();assert(!strcmp(pending,wallpaper_rotation_path()));
 assert(wallpaper_rotation_refresh()==80);decode_bad=true;unsigned before=decodes;new_frame(&frame);
 assert(frame=='F'&&decodes-before<=17);wallpaper_rotation_finish(true);decode_bad=false;
 assert(!strcmp(fixed,wallpaper_rotation_path()));
 // 增删改图重建集合，不把隐藏项/子目录当图片。/ Reconcile changes; hidden entries and directories never become images.
 image(80,'B');assert(wallpaper_rotation_refresh()==81);
 assert(!remove("card/pictures/wallpapers/080.jpg"));assert(wallpaper_rotation_refresh()==80);
 f=fopen("card/pictures/wallpapers/.hidden.jpg","wb");assert(f&&fputc('H',f)!=EOF&&!fclose(f));
 assert(!mkdir("card/pictures/wallpapers/subdir.png",0775));assert(wallpaper_rotation_refresh()==80);
 for(unsigned i=80;i<260;++i){image(i,'C');}assert(wallpaper_rotation_refresh()==256&&wallpaper_rotation_overflow());
 // 损坏/截断的进度不造成越界；无卡不读取旧卡列表。/ Corrupt/truncated progress is bounded; missing media cannot use old-card bags.
 wallpaper_rotation_invalidate();f=fopen(STATE_PATH,"wb");assert(f&&fwrite("bad",1,3,f)==3&&!fclose(f));
 assert(wallpaper_rotation_refresh()==256);new_frame(&frame);wallpaper_rotation_finish(true);
 wallpaper_rotation_invalidate();media_ready=false;assert(!wallpaper_rotation_refresh());media_ready=true;
 assert(wallpaper_rotation_refresh()==256);
 // 16坏图且固定图不可用时，下次继续找第17张；推屏撤销不能跳过有效图或改变显示身份。
 // With 16 bad candidates and no fixed fallback, reach candidate 17 next time; an aborted display retries it and retains displayed identity.
 for(unsigned i=17;i<260;++i){char p[128];snprintf(p,sizeof(p),"card/pictures/wallpapers/%03u.jpg",i);assert(!remove(p));}
 for(unsigned i=0;i<17;++i)image(i,'X');
 image(16,'G');fixed="/sdcard/unusable.jpg";
 assert(wallpaper_rotation_refresh()==17);
 for(unsigned i=0;i<17;++i)s_saved->order[i]=s_items[i].key;
 s_saved->cursor=0;snprintf(previous,sizeof(previous),"%s",wallpaper_rotation_path());
 before=decodes;wallpaper_rotation_begin();assert(!wallpaper_rotation_prepare(&frame));
 assert(decodes-before==17&&s_saved->cursor==16);wallpaper_rotation_finish(false);
 assert(!strcmp(previous,wallpaper_rotation_path()));
 new_frame(&frame);assert(frame=='G'&&strstr(wallpaper_rotation_path(),"016.jpg"));
 wallpaper_rotation_finish(false);assert(s_saved->cursor==16&&!strcmp(previous,wallpaper_rotation_path()));
 new_frame(&frame);assert(frame=='G'&&strstr(wallpaper_rotation_path(),"016.jpg"));wallpaper_rotation_finish(true);
 wallpaper_rotation_invalidate();assert(strstr(wallpaper_rotation_path(),"016.jpg")&&s_saved->cursor==17);
 wallpaper_rotation_invalidate();
 puts("PASS: 80-image nonrepeating rounds, boundary avoidance, successful-display commits, wake/reboot stability, bounded bad-image fallback, read-only/corrupt state and 256-image limit");
}
'''
with tempfile.TemporaryDirectory(prefix='wallpaper-rotation-') as folder:
 p=Path(folder);c=p/'test.c';exe=p/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main'),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],cwd=p,check=True)
