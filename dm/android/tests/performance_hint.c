#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <android/performance_hint.h>

_Atomic int dm_debug_collection_enabled=1;

static uint64_t test_now=1000000000;
static int32_t test_tid=123;
static int creates,reports,closes,updates,report_error,update_error;
static int mode;
static int64_t actual,target;
static APerformanceHintManager manager;
static APerformanceHintSession test_session;
static int test_clock(clockid_t clock,struct timespec *out) {
    (void)clock;out->tv_sec=test_now/1000000000;out->tv_nsec=test_now%1000000000;return 0;
}
static pid_t test_gettid(void) {return test_tid;}
static void *test_dlopen(const char *name,int flags) {(void)name;(void)flags;return &manager;}
static APerformanceHintManager *get_manager(void) {return mode==3?NULL:&manager;}
static APerformanceHintSession *create_session(APerformanceHintManager *m,const int32_t *t,size_t n,int64_t period) {
    assert(m==&manager&&n==1&&*t==test_tid);creates++;target=period;return &test_session;
}
static int report_session(APerformanceHintSession *s,int64_t work) {assert(s==&test_session);reports++;actual=work;return report_error;}
static int update_session(APerformanceHintSession *s,int64_t period) {assert(s==&test_session);updates++;target=period;return update_error;}
static void close_session(APerformanceHintSession *s) {assert(s==&test_session);closes++;}
static int64_t preferred_rate(APerformanceHintManager *m) {assert(m==&manager);return 16666667;}
static void *test_dlsym(void *library,const char *name) {
    assert(library==&manager);
    if(mode==2)return NULL;
    if(!strcmp(name,"APerformanceHint_getManager"))return (void*)get_manager;
    if(!strcmp(name,"APerformanceHint_createSession"))return (void*)create_session;
    if(!strcmp(name,"APerformanceHint_reportActualWorkDuration"))return (void*)report_session;
    if(!strcmp(name,"APerformanceHint_updateTargetWorkDuration"))return (void*)update_session;
    if(!strcmp(name,"APerformanceHint_closeSession"))return (void*)close_session;
    if(!strcmp(name,"APerformanceHint_getPreferredUpdateRateNanos"))return (void*)preferred_rate;
    return NULL;
}
int __android_log_print(int priority,const char *tag,const char *format,...) {(void)priority;(void)tag;(void)format;return 0;}
int __system_property_get(const char *name,char *value) {(void)name;if(mode==1){strcpy(value,"0");return 1;}*value=0;return 0;}
#define __ANDROID__ 1
#define clock_gettime test_clock
#define gettid test_gettid
#define dlopen test_dlopen
#define dlsym test_dlsym
#include "../native/performance_hint.c"

static void begin(const void *w,int index,uint64_t period) {dm_android_performance_frame_begin(w,index,period);}
static void end(const void *w,int index) {test_now+=10000000;dm_android_performance_frame_end(w,index);}
int main(int argc,char **argv) {
    const void *w=&manager;unsetenv("DM_ANDROID_PERF_HINT");
    if(argc>1){
        mode=!strcmp(argv[1],"disabled")?1:!strcmp(argv[1],"unavailable")?2:3;
        begin(w,0,16666667);end(w,0);begin(w,1,16666667);end(w,1);
        assert(!creates&&!reports);puts("PASS: disabled/unavailable/unsupported fallback");return 0;
    }
    begin(w,1,16666667);dm_android_performance_wsi_calls(2000000);end(w,1);
    assert(creates==1&&reports==1&&actual==8000000&&target==16666667);
    begin(w,2,33333333);end(w,2);assert(updates==1&&target==33333333&&reports==2);
    int before=reports;
    begin(w,0,16666667);begin(w,0,16666667);end(w,0);assert(reports==before);
    begin(w,0,16666667);end(w,1);assert(reports==before);
    begin(w,0,16666667);test_tid++;end(w,0);test_tid--;assert(reports==before);
    begin(w,0,16666667);dm_android_performance_wsi_calls(UINT64_MAX);end(w,0);assert(reports==before);
    begin(w,0,0);end(w,0);assert(reports==before&&!session&&closes==1);
    begin(w,0,UINT64_MAX);end(w,0);assert(reports==before);
    begin(w,0,16666667);dm_android_performance_close();end(w,0);assert(reports==before&&closes==1);
    dm_android_performance_resume();begin(w,0,16666667);end(w,0);assert(creates==2&&reports==before+1);
    report_error=32;begin(w,0,16666667);end(w,0);assert(closes==2&&!session);
    before=reports;begin(w,0,16666667);end(w,0);assert(reports==before);
    report_error=0;dm_android_performance_close();dm_android_performance_resume();
    begin(w,0,16666667);end(w,0);update_error=32;before=reports;
    begin(w,0,33333333);end(w,0);assert(reports==before&&!session&&closes==3);
    puts("PASS: real ADPF cycle pairing, WSI exclusion, target changes and lifecycle");
    return 0;
}
