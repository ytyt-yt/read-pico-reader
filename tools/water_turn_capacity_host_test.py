#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：复用引擎oracle验证53/64/65相完整输出、区域和失败回滚。
English: Reuse the engine oracle for complete53/64/65-phase output, crops and failure rollback.
"""
from pathlib import Path
import re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
source=(R/'tools/water_turn_host_test.c').read_text()
prefix=source[:source.index('int main(void) {')]
unit=r'''
int main(void){
 EpdiyHighlevelState hl={front,back,diff,dirty_lines,dirty_columns};
 e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
 for(int rot=0;rot<4;rot++)for(int dir=0;dir<4;dir++){
  rotation=(enum EpdRotation)rot;direction=(e0470_turn_dir_t)dir;
  EpdRect logical=(rot&1)?(EpdRect){0,0,HEIGHT,WIDTH}:(EpdRect){0,0,WIDTH,HEIGHT};
  memset(front,0x24,sizeof(front));memset(back,0xff,sizeof(back));memset(phase_hits,0,sizeof(phase_hits));
  scans=differences=powerons=fail_at=0;now_us=0;test_bands=8;test_launch_step=1;check_direction=true;
  assert(e0470_page_turn(&hl,logical,direction)==EPD_DRAW_SUCCESS);
  assert(scans==TEST_TICKS&&differences==1&&powerons==1&&!staged_luts&&!memcmp(front,back,sizeof(front)));
  for(int p=0;p<TEST_PHASES;p++)assert(phase_hits[p]==8);
  EpdRect crop={0,160,logical.width,logical.height-288};
  memset(back,0xff,sizeof(back));scans=differences=0;check_direction=false;
  assert(e0470_page_turn(&hl,crop,direction)==EPD_DRAW_SUCCESS&&scans==TEST_TICKS);check_crop_baseline(crop);
 }
 // 慢扫描不得截断相位；条带数和补等不改变每像素的完整序列。
 // Slow scans never truncate phases; bands and pacing retain each pixel's complete sequence.
 memset(back,0xff,sizeof(back));memset(phase_hits,0,sizeof(phase_hits));
 scans=differences=0;now_us=0;scan_time_us=15000;
 assert(e0470_page_turn(&hl,(EpdRect){0,0,HEIGHT,WIDTH},direction)==EPD_DRAW_SUCCESS);
 assert(scans==TEST_TICKS&&now_us==(int64_t)TEST_TICKS*scan_time_us);
 for(int p=0;p<TEST_PHASES;p++)assert(phase_hits[p]==8);
 scan_time_us=7000;
 for(int failure=1;failure<=TEST_TICKS;failure++){
  memset(back,0xff,sizeof(back));scans=differences=0;fail_at=failure;
  EpdRect full={0,0,HEIGHT,WIDTH};
  assert(e0470_page_turn(&hl,full,direction)==EPD_DRAW_OTHER_ERROR);
  assert(scans==failure&&!staged_luts);
  for(size_t i=0;i<sizeof(back);i++)assert(back[i]==0xff);
 }
 e0470_page_turn_release();
 printf("PASS: actual%d-phase ripple engine, all4 rotations/directions,8 bands,%d complete ticks, slow scans, header/footer crop preservation and every failed tick rolled back\n",TEST_PHASES,TEST_TICKS);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for phases in (53,64,65):
  # 只替换oracle的相数，保持真实扫描及LUT回调。/ Change only the oracle phase count, retaining real scan/LUT callbacks.
  oracle=re.sub(r'\b37\b',str(phases),prefix)
  constants=f'#define TEST_PHASES {phases}\n#define TEST_TICKS {phases+7}\n'
  (p/'test.c').write_text(oracle+constants+unit)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'tools/water_turn_stubs'),'-I'+str(R/'components/e0470_page_turn/include'),str(p/'test.c'),str(R/'components/e0470_page_turn/e0470_page_turn.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
