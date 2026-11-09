#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：漫画原生行绘制与逐像素参考完全比较，覆盖旋转、裁剪和奇数边缘。
English: Compare native comic-row painting with a pixel reference across rotations, clipping and odd edges.
冻结：仅主机测试。/ Frozen: Host tests only.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = """#pragma once
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
int epd_width(void);
int epd_height(void);
enum EpdRotation epd_get_rotation(void);
"""
HARNESS = r'''
#include "ui_comic_image.h"
#include "ui_image_dither.h"
#include "epdiy.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int pw=120,ph=80;
static enum EpdRotation rotation;
int epd_width(void){return pw;}
int epd_height(void){return ph;}
enum EpdRotation epd_get_rotation(void){return rotation;}
static void reference(uint8_t *fb,const uint8_t *gray,int w,int h,int left,int top){
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  int lx=left+x,ly=top+y,nx=lx,ny=ly;
  switch(rotation){
   case EPD_ROT_LANDSCAPE:break;
   case EPD_ROT_PORTRAIT:nx=pw-ly-1;ny=lx;break;
   case EPD_ROT_INVERTED_LANDSCAPE:nx=pw-lx-1;ny=ph-ly-1;break;
   case EPD_ROT_INVERTED_PORTRAIT:nx=ly;ny=ph-lx-1;break;
  }
  if(nx<0||ny<0||nx>=pw||ny>=ph)continue;
  uint8_t value=ui_image_dither_gray(gray[y*w+x],lx,ly)>>4;
  uint8_t *out=fb+(size_t)ny*(pw/2)+nx/2;
  if(nx&1)*out=(*out&15)|(value<<4);else *out=(*out&240)|value;
 }
}
static void compare(int w,int h,int left,int top){
 size_t bytes=(size_t)pw*ph/2;
 uint8_t *fast=malloc(bytes+16),*slow=malloc(bytes+16),*gray=malloc((size_t)w*h);
 assert(fast&&slow&&gray);memset(fast,0xa5,bytes+16);memset(slow,0xa5,bytes+16);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x)gray[y*w+x]=(uint8_t)(x*19+y*37);
 reference(slow,gray,w,h,left,top);ui_comic_image_paint(fast,gray,w,h,left,top);
 assert(!memcmp(fast,slow,bytes+16));free(fast);free(slow);free(gray);
}
int main(void){
 for(rotation=0;rotation<4;++rotation){
  for(int w=1;w<20;++w)for(int h=1;h<20;++h)
   for(int x=-3;x<7;++x)for(int y=-3;y<7;++y)compare(w,h,x,y);
  compare(121,81,19,13);compare(45,33,91,61);compare(12,13,-100,-100);
 }
 pw=1216;ph=684;
 for(rotation=0;rotation<4;++rotation){compare(683,999,0,103);compare(1111,641,17,9);}
 puts("PASS: native comic paint matches every reference pixel in four rotations, Bayer phases, clipping and odd nibbles; source/output guards intact");
}
'''
with tempfile.TemporaryDirectory(prefix="comic-paint-", dir=ROOT / "build") as folder:
    work = Path(folder)
    (work / "epdiy.h").write_text(STUB)
    (work / "test.c").write_text(HARNESS)
    exe = work / "test"
    subprocess.run(["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I" + str(work), "-I" + str(ROOT / "main/ui"), str(work / "test.c"),
                    str(ROOT / "main/ui/ui_comic_image.c"), str(ROOT / "main/ui/ui_image_dither.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
