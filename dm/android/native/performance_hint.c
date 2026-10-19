#include "performance_hint.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <android/performance_hint.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// NDK 接口从 API 33 开始提供；动态解析令 API 29–32 仍能加载应用。
// NDK interfaces start at API 33; dynamic resolution keeps the application loadable on API 29–32.
typedef struct {
    APerformanceHintManager *(*manager)(void);
    APerformanceHintSession *(*create)(APerformanceHintManager *,const int32_t *,size_t,int64_t);
    int (*report)(APerformanceHintSession *,int64_t);
    void (*close)(APerformanceHintSession *);
    int64_t (*preferred_rate)(APerformanceHintManager *);
} hint_api;

static hint_api api;
static pthread_once_t load_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t hint_lock=PTHREAD_MUTEX_INITIALIZER;
static APerformanceHintSession *session;
static int available,attempted,reported;
static int32_t bound_tid;
static const int64_t target_ns=16666667;
static int paused,cycle_pending,cycle_invalid,cycle_index;
static const void *cycle_window;
static int32_t cycle_tid;
static uint64_t cycle_start,cycle_wsi,accepted,skipped,cycles,work_total,wsi_total;

static uint64_t monotonic_ns(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
}
static void cycle_clear(void) {
    cycle_pending=cycle_invalid=0;cycle_window=NULL;cycle_start=cycle_wsi=0;
}

static void load_api(void) {
    const char *enabled=getenv("DM_ANDROID_PERF_HINT");
    if(enabled&&!strcmp(enabled,"0")){fprintf(stderr,"[dm] [adpf] disabled by launch environment\n");return;}
    void *library=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"[dm] [adpf] libandroid load failed\n");return;}
    // Android 的函数指针与 dlsym 地址同大小；保持库句柄存活至进程退出。
    // Android function pointers have the size of dlsym addresses; retain the library handle until process exit.
    _Static_assert(sizeof api.manager==sizeof(void *),"Android function pointer size");
#define LOAD_HINT(field,name) do {void *symbol=dlsym(library,name);memcpy(&api.field,&symbol,sizeof api.field);} while(0)
    LOAD_HINT(manager,"APerformanceHint_getManager");
    LOAD_HINT(create,"APerformanceHint_createSession");
    LOAD_HINT(report,"APerformanceHint_reportActualWorkDuration");
    LOAD_HINT(close,"APerformanceHint_closeSession");
    LOAD_HINT(preferred_rate,"APerformanceHint_getPreferredUpdateRateNanos");
#undef LOAD_HINT
    available=api.manager&&api.create&&api.report&&api.close;
    // 冷路径写入已有游戏日志，避免设备过滤 logcat 后误判会话状态。
    // Mirror cold-path evidence to the existing game log when device logcat filters hide session state.
    fprintf(stderr,"[dm] [adpf] API available=%d\n",available);
    if(!available)__android_log_print(ANDROID_LOG_INFO,"D2R","[adpf] native performance hints unavailable; continue with normal scheduling");
}

void dm_android_performance_frame_begin(const void *window,int index) {
    pthread_once(&load_once,load_api);
    if(!available)return;
    pthread_mutex_lock(&hint_lock);
    // 重复 acquire 不能覆盖起点；等下次 present 丢弃歧义周期后再恢复。
    // Repeated acquire cannot overwrite the start; discard the ambiguous cycle at present, then recover.
    if(!paused){
        if(cycle_pending)cycle_invalid=1;
        else {
            cycle_pending=1;cycle_window=window;cycle_index=index;cycle_tid=gettid();cycle_start=monotonic_ns();cycle_wsi=0;
            cycle_invalid=!window||index<0||index>=4||!cycle_start;
        }
    }
    pthread_mutex_unlock(&hint_lock);
}

void dm_android_performance_wsi_calls(uint64_t duration_ns) {
    pthread_mutex_lock(&hint_lock);
    if(cycle_pending&&!cycle_invalid){
        if(cycle_tid!=gettid()||duration_ns>UINT64_MAX-cycle_wsi)cycle_invalid=1;
        else cycle_wsi+=duration_ns;
    }
    pthread_mutex_unlock(&hint_lock);
}

static void session_create_locked(int32_t tid) {
    if(!attempted){
        attempted=1;
        APerformanceHintManager *manager=api.manager();
        if(manager)session=api.create(manager,&tid,1,target_ns);
        if(session){
            bound_tid=tid;
            int64_t preferred=api.preferred_rate?api.preferred_rate(manager):-1;
            fprintf(stderr,"[dm] [adpf] session created tid=%d target_ns=%lld preferred_ns=%lld\n",bound_tid,(long long)target_ns,(long long)preferred);
            __android_log_print(ANDROID_LOG_INFO,"D2R","[adpf] session tid=%d target=%.3f ms preferred_report=%.3f ms",bound_tid,target_ns/1e6,preferred<0?-1:preferred/1e6);
        }else {fprintf(stderr,"[dm] [adpf] session creation failed manager=%d tid=%d\n",manager!=NULL,tid);__android_log_print(ANDROID_LOG_INFO,"D2R","[adpf] session not supported/available; continue with normal scheduling");}
    }
}

void dm_android_performance_frame_end(const void *window,int index) {
    uint64_t ended=monotonic_ns();int32_t tid=gettid();
    pthread_once(&load_once,load_api);
    pthread_mutex_lock(&hint_lock);
    if(paused||!available){cycle_clear();pthread_mutex_unlock(&hint_lock);return;}
    uint64_t elapsed=ended>=cycle_start?ended-cycle_start:0;
    int valid=cycle_pending&&!cycle_invalid&&cycle_window==window&&cycle_index==index&&cycle_tid==tid&&elapsed>cycle_wsi;
    uint64_t work_ns=valid?elapsed-cycle_wsi:0,wsi_ns=cycle_wsi;
    // 周期墙钟保留真实依赖等待；仅扣 WSI 两个完整调用，含少量驱动CPU，不称为纯等待。
    // Cycle wall time retains dependency waits; subtract only both full WSI calls, including some driver CPU, not pure idle.
    cycle_clear();cycles++;
    if(valid&&work_ns&&work_ns<=INT64_MAX){session_create_locked(tid);valid=session&&bound_tid==tid;}else valid=0;
    if(valid){
        int result=api.report(session,(int64_t)work_ns);
        if(!reported||result){fprintf(stderr,"[dm] [adpf] report bound_tid=%d caller_tid=%d work_ns=%llu result=%d\n",bound_tid,gettid(),(unsigned long long)work_ns,result);reported=1;}
        if(!result){accepted++;work_total+=work_ns;wsi_total+=wsi_ns;}else {
            skipped++;
            __android_log_print(ANDROID_LOG_INFO,"D2R","[adpf] report failed (%d) tid=%d; close hint session",result,bound_tid);
            api.close(session);session=NULL;bound_tid=0;
        }
    }else skipped++;
    if(cycles==1||!(cycles%120))fprintf(stderr,"[dm] [adpf-cycle] cycles=%llu accepted=%llu skipped=%llu caller_tid=%d bound_tid=%d last_work_ns=%llu last_wsi_calls_ns=%llu accepted_mean_ms=%.3f excluded_wsi_mean_ms=%.3f target_ns=%lld clock=monotonic dependency_wait_included=1 worker_not_added=1\n",(unsigned long long)cycles,(unsigned long long)accepted,(unsigned long long)skipped,tid,bound_tid,(unsigned long long)(valid?work_ns:0),(unsigned long long)wsi_ns,accepted?work_total/(accepted*1e6):0,accepted?wsi_total/(accepted*1e6):0,(long long)target_ns);
    pthread_mutex_unlock(&hint_lock);
}

void dm_android_performance_close(void) {
    pthread_mutex_lock(&hint_lock);
    if(session){api.close(session);session=NULL;}
    bound_tid=0;attempted=0;reported=0;
    paused=1;cycle_clear();accepted=skipped=cycles=work_total=wsi_total=0;
    pthread_mutex_unlock(&hint_lock);
}
void dm_android_performance_resume(void) {
    pthread_mutex_lock(&hint_lock);cycle_clear();paused=0;pthread_mutex_unlock(&hint_lock);
}

#else
void dm_android_performance_frame_begin(const void *window,int index) {(void)window;(void)index;}
void dm_android_performance_frame_end(const void *window,int index) {(void)window;(void)index;}
void dm_android_performance_wsi_calls(uint64_t duration_ns) {(void)duration_ns;}
void dm_android_performance_close(void) {}
void dm_android_performance_resume(void) {}
#endif
