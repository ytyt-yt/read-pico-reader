/* SPDX-License-Identifier: Apache-2.0
 * 中文：一个有界低优先级封面工作队列；UI只收取结果，旧代次不写入新页面。
 * English: One bounded low-priority cover queue; the UI takes results and stale generations never alter a new page.
 */
#include "book_cover_jobs.h"
#include "book_cover_auto.h"
#include "book_title.h"
#include "ui_image_dither.h"
#include "read_pico_sd.h"
#include "ui_wallpaper.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { EMPTY,QUEUED,RUNNING,AUTO,READY,FAILED,TAKEN,CHECK_QUEUED,CHECK_READY,CHECK_FAILED };
typedef struct { book_cover_job_t request;book_cover_result_t result;int state;bool auto_cache;uint8_t *automatic; } cover_job_slot_t;
static cover_job_slot_t *s_jobs;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned s_generation;
static bool s_running,s_paused;
static char s_wallpaper[288];
static unsigned s_wallpaper_generation;
static bool wallpaper_job_current(void) {
    portENTER_CRITICAL(&s_lock);
    bool active=!s_paused && s_wallpaper_generation==s_generation;
    portEXIT_CRITICAL(&s_lock);
    return active && read_pico_sd_io_ready();
}

static uint8_t contrast_gray(uint8_t value,unsigned contrast) {
    int v=128+((int)value-128)*(int)contrast/100;
    return (uint8_t)(v<0?0:v>255?255:v)&0xf0;
}
static bool white_edge(const uint8_t *gray,unsigned sw,unsigned sh,book_cover_job_t *q) {
    book_crop_t crop=book_cover_crop(sw,sh,q->width,q->height);unsigned sum=0,count=0;
    for(unsigned y=9;y<39;y+=5)for(unsigned x=11;x<35;x+=4){
        unsigned sx=crop.x+(uint64_t)x*crop.width/q->width,sy=crop.y+(uint64_t)y*crop.height/q->height;
        if(sx>=sw)sx=sw-1;
        if(sy>=sh)sy=sh-1;
        sum+=contrast_gray(gray[(size_t)sy*sw+sx],q->contrast);++count;
    }
    return sum<count*145;
}
static bool current(unsigned generation) {
    portENTER_CRITICAL(&s_lock);bool ok=!s_paused&&generation==s_generation;portEXIT_CRITICAL(&s_lock);return ok;
}
static void prepare(cover_job_slot_t *job,unsigned generation) {
    book_cover_job_t *q=&job->request;unsigned sw=BOOK_COVER_W,sh=BOOK_COVER_H;
    struct stat st;if(!current(generation)||stat(q->path,&st)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)))return;
    const uint64_t fingerprint=book_cover_source_fingerprint(q->path);
    if(job->automatic&&(job->result.size!=st.st_size||job->result.modified!=st.st_mtime||
       job->result.source_fingerprint!=fingerprint))return;
    job->result.size=st.st_size;job->result.modified=st.st_mtime;job->result.source_fingerprint=fingerprint;
    if(!job->automatic&&q->fast&&(!q->natural||book_cover_list_cache_dimensions(q->path,q->title,q->author,&sw,&sh))){
        if(q->natural){q->width=sw;q->height=sh;q->stride=(sw+7)/8;}
        size_t bytes=(size_t)q->stride*q->height;
        uint8_t *bits=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);bool edge=false;
        book_fast_cover_key_t key={q->width,q->height,q->stride,q->contrast,q->phase};
        if(bits&&book_cover_fast_cache_read(q->path,q->title,q->author,key,bits,&edge)){
            job->result.pixels=bits;job->result.fast=true;job->result.white_edge=edge;goto ready;
        }free(bits);
    }
    bool from_auto=job->automatic!=NULL;
    uint8_t *gray=job->automatic;job->automatic=NULL;
    if(gray&&job->auto_cache){book_cover_store_generated(q->path,q->title,q->author,gray,q->list,st.st_size,st.st_mtime);}
    if(!gray) {
        const size_t need=BOOK_COVER_W*BOOK_COVER_H;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<need+1536u*1024u)return;
        gray=heap_caps_malloc(need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!gray)return;
        int result=book_cover_load_io(q->path,q->title,q->author,gray,q->list,&sw,&sh,&job->auto_cache);
        if(result<0){free(gray);return;}
        if(!current(generation)){free(gray);return;}
        if(!result){if(q->artwork_only){free(gray);return;}job->result.pixels=gray;job->state=AUTO;return;}
    }
    if(!q->fast){job->result.pixels=gray;q->width=sw;q->height=sh;goto ready;}
    if(q->natural){q->width=sw;q->height=sh;q->stride=(sw+7)/8;}
    uint8_t *bits=heap_caps_calloc(q->height,q->stride,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!bits){free(gray);return;}
    book_crop_t crop=book_cover_crop(sw,sh,q->width,q->height);
    for(unsigned y=0;y<q->height;++y){
        if(!(y&15)&&!current(generation)){free(bits);free(gray);return;}
        unsigned sy=crop.y+(uint64_t)y*crop.height/q->height;
        for(unsigned x=0;x<q->width;++x){
            unsigned sx=crop.x+(uint64_t)x*crop.width/q->width;
            uint8_t tone=contrast_gray(gray[(size_t)sy*sw+sx],q->contrast);
            bool white=q->natural?ui_image_dither_bw(tone,x,y):ui_image_dither_cover_bw((tone>>4)*17u,q->x+x,q->y+y);
            if(white)bits[(size_t)y*q->stride+x/8]|=1u<<(x&7);
        }
    }
    job->result.white_edge=q->natural?false:white_edge(gray,sw,sh,q);free(gray);
    if(!current(generation)){free(bits);return;}
    book_fast_cover_key_t key={q->width,q->height,q->stride,q->contrast,q->phase};
    if(!from_auto)book_cover_fast_cache_write_checked(q->path,q->title,q->author,key,bits,job->result.white_edge,st.st_size,st.st_mtime);
    job->result.pixels=bits;job->result.fast=true;
ready:
    job->result.width=q->width;job->result.height=q->height;
    struct stat after;
    if(stat(q->path,&after)||after.st_size!=st.st_size||after.st_mtime!=st.st_mtime||
       book_cover_source_fingerprint(q->path)!=fingerprint){free(job->result.pixels);job->result.pixels=NULL;}
}
static void worker(void *unused) {
    (void)unused;
    for(;;){
        cover_job_slot_t job={0};int slot=-1;unsigned generation;
        portENTER_CRITICAL(&s_lock);
        if(!s_paused&&s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)if(s_jobs[i].state==QUEUED||s_jobs[i].state==CHECK_QUEUED){slot=(int)i;break;}
        if(slot<0 && !s_paused && s_wallpaper[0]) {
            char path[288];memcpy(path,s_wallpaper,sizeof(path));s_wallpaper[0]=0;
            s_wallpaper_generation=s_generation;
            portEXIT_CRITICAL(&s_lock);
            ui_wallpaper_warm_cache(path,wallpaper_job_current);
            vTaskDelay(1);continue;
        }
        if(slot<0){s_running=false;portEXIT_CRITICAL(&s_lock);break;}
        job=s_jobs[slot];s_jobs[slot].automatic=NULL;s_jobs[slot].state=RUNNING;generation=s_generation;
        portEXIT_CRITICAL(&s_lock);
        bool checking=job.state==CHECK_QUEUED;
        if(checking){
            if(current(generation))job.result.source_fingerprint=book_cover_source_fingerprint(job.request.path);
        }else prepare(&job,generation);
        portENTER_CRITICAL(&s_lock);
        if(!s_paused&&generation==s_generation){
            s_jobs[slot].auto_cache=job.auto_cache;s_jobs[slot].result=job.result;s_jobs[slot].state=checking?CHECK_READY:job.result.pixels?(job.state==AUTO?AUTO:READY):FAILED;
            job.result.pixels=NULL;
        }
        portEXIT_CRITICAL(&s_lock);
        free(job.result.pixels);free(job.automatic);vTaskDelay(1);
    }
    vTaskDelete(NULL);
}
static void start_worker(void) {
    portENTER_CRITICAL(&s_lock);bool queued=s_wallpaper[0]!=0;
    if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)queued|=s_jobs[i].state==QUEUED||s_jobs[i].state==CHECK_QUEUED;
    bool start=queued&&!s_running&&!s_paused;if(start)s_running=true;portEXIT_CRITICAL(&s_lock);
    if(start&&xTaskCreate(worker,"covers",12288,NULL,1,NULL)!=pdPASS){
        uint8_t *old[BOOK_COVER_JOB_SLOTS]={0};
        portENTER_CRITICAL(&s_lock);s_running=false;s_wallpaper[0]=0;
        if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)if(s_jobs[i].state==QUEUED||s_jobs[i].state==CHECK_QUEUED){
            old[i]=s_jobs[i].automatic;s_jobs[i].automatic=NULL;
            s_jobs[i].state=s_jobs[i].state==CHECK_QUEUED?CHECK_FAILED:FAILED;
        }
        portEXIT_CRITICAL(&s_lock);
        for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)free(old[i]);
    }
}
unsigned book_cover_jobs_begin(void) {
    if(!s_jobs)s_jobs=heap_caps_calloc(BOOK_COVER_JOB_SLOTS,sizeof(*s_jobs),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *old[BOOK_COVER_JOB_SLOTS*2]={0};
    portENTER_CRITICAL(&s_lock);unsigned generation=++s_generation;s_paused=false;
    if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i){old[i*2]=s_jobs[i].result.pixels;old[i*2+1]=s_jobs[i].automatic;memset(&s_jobs[i],0,sizeof(s_jobs[i]));}
    portEXIT_CRITICAL(&s_lock);
    for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS*2;++i)free(old[i]);
    return generation;
}
static bool same_request(const book_cover_job_t *a,const book_cover_job_t *b) {
    return !strcmp(a->path,b->path)&&!strcmp(a->title,b->title)&&!strcmp(a->author,b->author)&&
        a->list==b->list&&a->fast==b->fast&&a->natural==b->natural&&a->artwork_only==b->artwork_only&&a->width==b->width&&a->height==b->height&&
        a->stride==b->stride&&a->contrast==b->contrast&&a->phase==b->phase&&a->x==b->x&&a->y==b->y;
}
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *request) {
    // 挂载交接期间不重新打开旧卡封面；恢复后由书架扫描重排队。
    // Never reopen old-card cover files during mount handoff; shelf scans requeue after recovery.
    if(request&&!strncmp(request->path,"/sdcard/",8)&&!read_pico_sd_io_ready())return false;
    if(!s_jobs||!request||slot>=BOOK_COVER_JOB_SLOTS||!request->path[0])return false;
    portENTER_CRITICAL(&s_lock);bool ok=generation==s_generation&&!s_paused;
    if(ok&&s_jobs[slot].state!=EMPTY&&!same_request(&s_jobs[slot].request,request))ok=false;
    if(ok&&s_jobs[slot].state==EMPTY){s_jobs[slot].request=*request;s_jobs[slot].state=QUEUED;}
    portEXIT_CRITICAL(&s_lock);if(ok)start_worker();return ok;
}
int book_cover_jobs_take(unsigned generation,unsigned slot,book_cover_result_t *result) {
    if(!s_jobs||slot>=BOOK_COVER_JOB_SLOTS||!result)return -1;
    cover_job_slot_t automatic={0};int status=0;
    portENTER_CRITICAL(&s_lock);
    if(generation!=s_generation||s_paused)status=-1;
    else if(s_jobs[slot].state==READY){*result=s_jobs[slot].result;s_jobs[slot].result.pixels=NULL;s_jobs[slot].state=TAKEN;status=1;}
    else if(s_jobs[slot].state==AUTO){automatic=s_jobs[slot];s_jobs[slot].result.pixels=NULL;s_jobs[slot].state=RUNNING;}
    else if(s_jobs[slot].state==FAILED){*result=s_jobs[slot].result;status=-1;}
    else if(s_jobs[slot].state==TAKEN)status=-1;
    portEXIT_CRITICAL(&s_lock);
    if(automatic.result.pixels){
        char canonical[256];const char *title=automatic.request.title;
        if(book_title_from_path(automatic.request.path,canonical,sizeof(canonical)))title=canonical;
        bool ok=book_auto_cover_render(automatic.request.path,title,automatic.request.author,BOOK_COVER_W,BOOK_COVER_H,automatic.result.pixels);
        portENTER_CRITICAL(&s_lock);
        if(generation==s_generation&&!s_paused){
            s_jobs[slot].state=ok?QUEUED:FAILED;
            if(ok){s_jobs[slot].automatic=automatic.result.pixels;automatic.result.pixels=NULL;}
        }
        portEXIT_CRITICAL(&s_lock);free(automatic.result.pixels);start_worker();
    }
    return status;
}
int book_cover_jobs_recheck(unsigned generation,unsigned slot,uint64_t *fingerprint) {
    if(!s_jobs||slot>=BOOK_COVER_JOB_SLOTS||!fingerprint)return -1;
    bool media_ready=read_pico_sd_io_ready();int status=0;
    portENTER_CRITICAL(&s_lock);
    cover_job_slot_t *job=&s_jobs[slot];
    if(generation!=s_generation||s_paused||job->state==EMPTY||
       (!media_ready&&!strncmp(job->request.path,"/sdcard/",8)))status=-1;
    else if(job->state==CHECK_READY){
        *fingerprint=job->result.source_fingerprint;job->state=TAKEN;status=1;
    }else if(job->state==CHECK_FAILED){job->state=TAKEN;status=-1;}
    else if(job->state==TAKEN||job->state==FAILED)job->state=CHECK_QUEUED;
    portEXIT_CRITICAL(&s_lock);
    if(!status)start_worker();
    return status;
}
void book_cover_jobs_cancel(unsigned generation) {
    uint8_t *old[BOOK_COVER_JOB_SLOTS*2]={0};
    portENTER_CRITICAL(&s_lock);
    if(generation==s_generation){
        ++s_generation;s_paused=true;s_wallpaper[0]=0;
        if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i){
            old[i*2]=s_jobs[i].result.pixels;old[i*2+1]=s_jobs[i].automatic;
            memset(&s_jobs[i],0,sizeof(s_jobs[i]));
        }
    }
    portEXIT_CRITICAL(&s_lock);
    for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS*2;++i)free(old[i]);
}
bool book_cover_jobs_quiesce(unsigned timeout_ms) {
    book_cover_jobs_cancel(s_generation);
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    for(;;){portENTER_CRITICAL(&s_lock);bool active=s_running;portEXIT_CRITICAL(&s_lock);
        if(!active)return true;
        if(esp_timer_get_time()>=deadline)return false;
        vTaskDelay(pdMS_TO_TICKS(10));}
}

bool book_cover_jobs_prefetch_wallpaper(const char *path) {
    if (!path || strncmp(path,"/sdcard/pictures/wallpapers/",sizeof("/sdcard/pictures/wallpapers/")-1) || strlen(path)>=sizeof(s_wallpaper) || !read_pico_sd_io_ready()) return false;
    portENTER_CRITICAL(&s_lock);
    bool ok=!s_running && !s_wallpaper[0];
    if (ok) { snprintf(s_wallpaper,sizeof(s_wallpaper),"%s",path);s_paused=false; }
    portEXIT_CRITICAL(&s_lock);
    if (ok) start_worker();
    return ok;
}
