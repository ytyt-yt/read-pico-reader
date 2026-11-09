/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际阅读波形的驱动边界；光学灰度和残影仍需实屏验证。
 * English: Validate actual reader pulse boundaries; optical tone and ghosting need panel tests.
 */
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"

static enum EpdRotation test_rotation;
int epd_width(void) { return 1216; }
int epd_height(void) { return 684; }
enum EpdRotation epd_get_rotation(void) { return test_rotation; }
static uint8_t test_front[1216*684/2], test_back[1216*684/2], test_original[1216*684/2];
static uint8_t test_old[1216*684/2];
static void test_fast_target(void) {
    EpdiyHighlevelState hl = {.front_fb=test_front, .back_fb=test_back};
    const EpdWaveform* waveform = display_reader_fast_text_waveform();
    const EpdWaveformPhases* native = e0470_waveform_phases(&E0470_FULL_WAVEFORM, MODE_DU);
    assert(waveform == &E0470_FULL_WAVEFORM && native && native->phases == E0470_FULL_DU_FRAMES);
    assert(e0470_waveform_phases(waveform, MODE_DU) == native && !native->phase_times);
    uint8_t original[E0470_FULL_DU_FRAMES*16*4]; memcpy(original,native->luts,sizeof(original));
    for(int f=0;f<native->phases;++f) {
        assert(e0470_phase_action(native,f,0,0)==0 && e0470_phase_action(native,f,15,15)==0);
    }
    for(int rot=0;rot<4;++rot) {
        test_rotation=(enum EpdRotation)rot;
        int lw=(rot&1)?684:1216, lh=(rot&1)?1216:684;
        EpdRect areas[]={{31,33,17,11},{-3,-4,9,13},{lw-5,lh-7,50,60},{0,0,lw,lh},
            {INT_MAX-2,0,INT_MAX,1},{0,INT_MAX-1,1,INT_MAX},{0,0,0,5},{-50,-60,3,3}};
        for(unsigned c=0;c<sizeof(areas)/sizeof(areas[0]);++c) {
            for(size_t i=0;i<sizeof(test_front);++i) {
                test_front[i]=test_original[i]=(uint8_t)(i*29+37);
                test_back[i]=test_old[i]=(uint8_t)(i*137+81);
            }
            EpdRect a=areas[c]; bool changed=display_reader_fast_text_prepare(&hl,a);
            assert(changed==(c<4));
            assert(!memcmp(test_back,test_old,sizeof(test_back)));
            for(int y=0;y<684;++y)for(int x=0;x<1216;++x) {
                int lx,ly;
                switch(rot) {
                    case 0:lx=x;ly=y;break;
                    case 1:lx=y;ly=1215-x;break;
                    case 2:lx=1215-x;ly=683-y;break;
                    default:lx=683-y;ly=x;break;
                }
                size_t at=(size_t)y*1216+x;unsigned shift=(at&1)*4;
                unsigned before=(test_original[at/2]>>shift)&15;
                bool inside=(int64_t)lx>=a.x&&(int64_t)lx<(int64_t)a.x+a.width&&
                    (int64_t)ly>=a.y&&(int64_t)ly<(int64_t)a.y+a.height&&a.width>0&&a.height>0;
                assert(((test_front[at/2]>>shift)&15)==(inside?(before<8?0u:15u):before));
            }
        }
    }
    hl.back_fb=hl.front_fb;assert(!display_reader_fast_text_prepare(&hl,(EpdRect){0,0,16,16}));
    assert(!display_reader_fast_text_prepare(NULL,(EpdRect){0,0,16,16}));
    assert(!memcmp(original,native->luts,sizeof(original)));
    puts("PASS: Pico native DU20 bytes/phases and equal BW holds; all16 targets,4 rotations,clipping,odd nibbles,outside grays and real history retained");
}

int main(void) {
    e0470_waveform_init();
    test_fast_target();
    const EpdWaveformPhases* old = e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM, MODE_GL16);
    uint8_t saved[E0470_GL16_FRAMES * 16 * 4];
    assert(old && old->phases == E0470_GL16_FRAMES && !old->phase_times);
    memcpy(saved, old->luts, sizeof(saved));
    const EpdWaveform* wave = display_reader_text_waveform();
    const EpdWaveformPhases* soft = e0470_waveform_phases(wave, MODE_GL16);
    assert(wave != &E0470_TEXTTURN_WAVEFORM && display_reader_text_waveform() == wave);
    assert(wave->temp_intervals == E0470_TEXTTURN_WAVEFORM.temp_intervals);
    assert(soft && soft->phases == old->phases && soft->phase_times == old->phase_times);
    unsigned changed = 0;
    for (int from = 0; from < 16; ++from) for (int to = 0; to < 16; ++to) {
        int a_count[4] = {0}, b_count[4] = {0}, a_balance = 0, b_balance = 0;
        int a_peak = 0, b_peak = 0, first = -1, last = -1;
        unsigned differences = 0;
        for (int f = 0; f < old->phases; ++f) {
            int a = e0470_phase_action(old, f, to, from), b = e0470_phase_action(soft, f, to, from);
            ++a_count[a]; ++b_count[b];
            if (a != b) ++differences;
            if (from != 15 || to == 0 || to == 15) assert(a == b);
            if (a == 0 || a == 3) assert(b == a);
            if (a) {if (first < 0) first = f; last = f;}
            a_balance += a == 1 ? 1 : a == 2 ? -1 : 0;
            b_balance += b == 1 ? 1 : b == 2 ? -1 : 0;
            if (a_balance > a_peak) a_peak = a_balance;
            if (b_balance > b_peak) b_peak = b_balance;
        }
        assert(!memcmp(a_count, b_count, sizeof(a_count)));
        if (first >= 0) assert(e0470_phase_action(old, first, to, from) == e0470_phase_action(soft, first, to, from));
        if (last >= 0) assert(e0470_phase_action(old, last, to, from) == e0470_phase_action(soft, last, to, from));
        if (differences) {
            assert(from == 15 && to > 0 && to < 15 && differences <= 4);
            assert(b_peak < a_peak && a_peak - b_peak <= 2);
            ++changed;
            printf("15->%d net black pulse peak %d -> %d; phases ", to, a_peak, b_peak);
            for (int f = 0; f < soft->phases; ++f) putchar(".BW?"[e0470_phase_action(soft, f, to, from)]);
            putchar('\n');
        }
    }
    assert(changed > 0 && !memcmp(saved, old->luts, sizeof(saved)));
    // 保留旧文字辅助表的同灰保持及灰边调序；水波纹在下方独立检查完整迁移。
    // Retain the legacy helper's equal-gray holds and edge ordering; test full ripple transitions below.
    const EpdWaveformPhases* raw=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
    unsigned held=0,edge_runs=0;
    for(int to=0;to<16;++to) {
        int old_run=0,new_run=0,old_max=0,new_max=0,old_same=0;
        int raw_counts[4]={0},soft_counts[4]={0};
        for(int f=0;f<raw->phases;++f) {
            int a=e0470_phase_action(raw,f,to,15),b=e0470_phase_action(soft,f,to,15);
            ++raw_counts[a];++soft_counts[b];
            old_run=a==1?old_run+1:0;new_run=b==1?new_run+1:0;
            if(old_run>old_max)old_max=old_run;
            if(new_run>new_max)new_max=new_run;
            old_same+=e0470_phase_action(raw,f,to,to)!=0;
            assert(e0470_phase_action(soft,f,to,to)==0);
        }
        held+=old_same>0;
        if(to>0&&to<15) {
            assert(!memcmp(raw_counts,soft_counts,sizeof(raw_counts)));
            assert(new_max<=old_max);edge_runs+=new_max<old_max;
        }
    }
    assert(held==16&&edge_runs>=10);
    puts("PASS: legacy text helper holds all16 unchanged grays and reduces at least ten gray-edge dark runs; targets and37 phases unchanged");
    // 普通翻页柔和档：厂商 GL16 原序，等灰全保持；末尾定稿保持之前补十二相整屏擦白，
    // 再补四相重推黑，抵消整屏白推对黑字的冲淡。
    // Soft ordinary turn: vendor GL16 phases, every equal pair held, twelve whole-panel white
    // phases plus four black re-drive phases inserted before the vendor's trailing settle holds.
    const EpdWaveform* soft_turn = display_reader_soft_turn_waveform();
    assert(soft_turn == display_reader_soft_turn_waveform() && soft_turn != &E0470_WAVEFORM);
    const EpdWaveformPhases* st = e0470_waveform_phases(soft_turn, MODE_GL16);
    const EpdWaveformPhases* vendor = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    assert(vendor && st && !st->phase_times && !vendor->phase_times);
    assert(st->phases == vendor->phases + 16);
    const int drive = vendor->phases - 3;
    unsigned equal_gray_refreshes = 0;
    for (int from = 0; from < 16; ++from)
        for (int to = 0; to < 16; ++to)
            for (int f = 0; f < drive; ++f) {
                const int a = e0470_phase_action(vendor, f, to, from);
                const int b = e0470_phase_action(st, f, to, from);
                if (to == 0 && from == 0) {
                    assert(b == (a == 2 ? 0 : a));
                    continue;
                }
                assert(b == a);
                if (to == from && a != 0) ++equal_gray_refreshes;
            }
    assert(equal_gray_refreshes > 0);
    for (int f = drive; f < drive + 12; ++f)
        for (int from = 0; from < 16; ++from)
            for (int to = 0; to < 16; ++to)
                assert(e0470_phase_action(st, f, to, from) ==
                       (to == 15 ? 2 : 0));
    for (int f = drive + 12; f < drive + 16; ++f)
        for (int from = 0; from < 16; ++from)
            for (int to = 0; to < 16; ++to)
                assert(e0470_phase_action(st, f, to, from) ==
                       (to == 0 ? 1 : 0));
    for (int i = 0; i < 3; ++i)
        for (int from = 0; from < 16; ++from)
            for (int to = 0; to < 16; ++to) {
                assert(e0470_phase_action(st, drive + 16 + i, to, from) ==
                       e0470_phase_action(vendor, drive + i, to, from));
                assert(e0470_phase_action(st, drive + 16 + i, to, from) == 0);
            }
    puts("PASS: soft ordinary turn keeps vendor sequences and equal-gray refresh, drops only black-on-black erase phases, inserts twelve whole-white phases and four black re-drive phases before the vendor settle holds");
    {
    const EpdWaveform* water_wave=display_reader_water_waveform();
    const EpdWaveformPhases* water=e0470_waveform_phases(water_wave,MODE_GL16);
    assert(water_wave==display_reader_water_waveform() && water && water->phases==E0470_GL16_FRAMES+16);
    assert(water_wave->temp_intervals==display_reader_text_waveform()->temp_intervals && !water->phase_times);
    const EpdWaveformPhases* water_src=e0470_waveform_phases(display_reader_text_waveform(),MODE_GL16);
    assert(water_src && water_src->phases==E0470_GL16_FRAMES);
    // 源表只移出末尾三相保持，之后加12相擦白和4相补黑。
    // Move only the source three holds after12 white/four black phases.
    const int water_drive=E0470_GL16_FRAMES-3;
    for(int from=0;from<16;++from)for(int to=0;to<16;++to){
        for(int f=0;f<water_drive;++f)
            assert(e0470_phase_action(water,f,to,from)==e0470_phase_action(water_src,f,to,from));
        for(int f=water_drive;f<water_drive+12;++f)
            assert(e0470_phase_action(water,f,to,from)==(to==15?2:0));
        for(int f=water_drive+12;f<water_drive+16;++f)
            assert(e0470_phase_action(water,f,to,from)==(to==0?1:0));
        for(int f=0;f<3;++f){
            assert(e0470_phase_action(water,water_drive+16+f,to,from)==
                   e0470_phase_action(water_src,water_drive+f,to,from));
            assert(e0470_phase_action(water,water_drive+16+f,to,from)==0);
        }
        if(from==to && from>0 && from<15)for(int f=0;f<water->phases;++f)assert(e0470_phase_action(water,f,to,from)==0);
    }
    puts("PASS: Pico reader ripple retains every original gray transition/three settle holds, twelve whole-white/four black-redrive phases, equal middle grays held,53 phases and cached identity");
    }
    {
    const EpdWaveformPhases* water_src=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GL16);
    assert(water_src && water_src->phases==E0470_FULL_GL16_FRAMES);
    uint8_t full_saved[E0470_FULL_GL16_FRAMES*16*4];
    memcpy(full_saved,water_src->luts,sizeof(full_saved));
    const EpdWaveform* water_wave=display_reader_clear_water_waveform();
    const EpdWaveformPhases* water=e0470_waveform_phases(water_wave,MODE_GL16);
    assert(water_wave!=display_reader_water_waveform() && water_wave==display_reader_clear_water_waveform() && water && water->phases==65);
    assert(water_wave->temp_intervals==E0470_FULL_WAVEFORM.temp_intervals && !water->phase_times);
    // 完整48相GL16不调序、不裁擦除或饱和段；同灰仍按原序列重建，防止字缘漂移积累。
    // Keep all48 GL16 phases in order, including erase/saturation and equal-gray edge re-drive.
    const int water_drive=E0470_FULL_GL16_FRAMES-2;
    unsigned equal_middle_redrives=0;
    for(int from=0;from<16;++from)for(int to=0;to<16;++to){
        for(int f=0;f<water_drive;++f)
            assert(e0470_phase_action(water,f,to,from)==e0470_phase_action(water_src,f,to,from));
        for(int f=water_drive;f<water_drive+12;++f)
            assert(e0470_phase_action(water,f,to,from)==(to==15?2:0));
        for(int f=water_drive+12;f<water_drive+16;++f)
            assert(e0470_phase_action(water,f,to,from)==(to==0?1:0));
        for(int f=0;f<2;++f){
            assert(e0470_phase_action(water,water_drive+16+f,to,from)==
                   e0470_phase_action(water_src,water_drive+f,to,from));
        }
        for(int f=0;f<3;++f)assert(e0470_phase_action(water,water_drive+16+f,to,from)==0);
        if(from==to && from>0 && from<15) {
            unsigned actions=0;
            for(int f=0;f<water_drive;++f)actions+=e0470_phase_action(water,f,to,from)!=0;
            assert(actions>0);
            ++equal_middle_redrives;
        }
    }
    assert(equal_middle_redrives==14 && !memcmp(full_saved,water_src->luts,sizeof(full_saved)));
    assert(!memcmp(saved,old->luts,sizeof(saved)));
    puts("PASS: Pico Clear ripple retains all256 complete48-phase GL16 transitions,14 equal-middle-gray re-drives, twelve whole-white/four black-redrive phases, three final holds,65 phases and cached identity");
    puts("PASS: legacy text/ordinary/DU profiles and source LUTs retained; optical validation pending");
    }
    return 0;
}
