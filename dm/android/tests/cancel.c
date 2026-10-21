// 取消语义与确定性完成顺序；测试仅在真实 pthread 等待返回后暂停，保留 sanitizer 拦截及生产锁协议。
// Cancellation semantics and deterministic completion ordering; pause only after a real pthread wait, retaining sanitizer interception and production locking.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../runtime/internal.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
#define TIMEOUT 0xea01u
typedef struct{uint32_t read,write;}event_pair;
static uint64_t call(dm_host*h,unsigned n,uint32_t handle){uint64_t r[31]={handle};return dm_host_svc(h,n,r);}
static event_pair event(dm_host*h){uint64_t r[31]={0};CHECK(!dm_host_svc(h,0x45,r));return(event_pair){(uint32_t)r[2],(uint32_t)r[1]};}
static uint64_t wait_event(dm_host*h,uint32_t handle,int64_t ns,size_t*index){uint64_t r[31]={0,(uintptr_t)&handle,1,(uint64_t)ns};uint64_t rc=dm_host_svc(h,0x18,r);*index=r[1];return rc;}
static void close_event(dm_host*h,event_pair e){CHECK(!call(h,0x16,e.read));CHECK(!call(h,0x16,e.write));}
static uint64_t mono(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return(uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void wait_count(dm_host*h,int arbiter,size_t wanted){uint64_t end=mono()+3000000000ull;
    for(;;){pthread_mutex_lock(&h->mutex);size_t n=arbiter?dm_host_arbiter_waiters(h):dm_host_sync_waiters(h);pthread_mutex_unlock(&h->mutex);if(n==wanted)return;
        CHECK(mono()<end);struct timespec pause={.tv_nsec=1000000};nanosleep(&pause,NULL);}}
static int(*real_wait)(pthread_cond_t*,pthread_mutex_t*);
static pthread_once_t resolve_once=PTHREAD_ONCE_INIT;
static void resolve_wait(void){void*symbol=dlsym(RTLD_NEXT,"pthread_cond_wait");CHECK(symbol);memcpy(&real_wait,&symbol,sizeof(real_wait));
    Dl_info origin;CHECK(dladdr(symbol,&origin)&&origin.dli_fname);printf("pthread_cond_wait continuation: %s\n",origin.dli_fname);
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if __has_feature(thread_sanitizer) || defined(__SANITIZE_THREAD__)
    CHECK(strstr(origin.dli_fname,"tsan"));
#endif
}
static _Thread_local int pause_after_wake;
static struct{pthread_mutex_t mutex;pthread_cond_t condition;dm_host*host;int armed,entered,release;}pause_gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,NULL,0,0,0};
int pthread_cond_wait(pthread_cond_t*condition,pthread_mutex_t*mutex){
    CHECK(!pthread_once(&resolve_once,resolve_wait));int rc=real_wait(condition,mutex);
    if(pause_after_wake&&mutex==&pause_gate.host->mutex){
        CHECK(!pthread_mutex_lock(&pause_gate.mutex));
        if(pause_gate.armed){pause_gate.armed=0;CHECK(!pthread_mutex_unlock(mutex));pause_gate.entered=1;CHECK(!pthread_cond_broadcast(&pause_gate.condition));
            while(!pause_gate.release)CHECK(!real_wait(&pause_gate.condition,&pause_gate.mutex));
            CHECK(!pthread_mutex_unlock(&pause_gate.mutex));CHECK(!pthread_mutex_lock(mutex));
        }else CHECK(!pthread_mutex_unlock(&pause_gate.mutex));
    }
    return rc;
}
typedef struct{
    dm_host*h;uint32_t event,handle,*address;int pause,go,ready,first_done,second_go,finish,detach_implicit;
    int64_t timeout;uint64_t first,second;size_t index;
    pthread_mutex_t mutex;pthread_cond_t condition;pthread_t thread;
}worker;
static void*worker_main(void*arg){worker*w=arg;dm_gate_table gates={0};CHECK(!dm_guest_attach(w->h,&gates));CHECK(!dm_guest_register_current_thread(44,&w->handle));
    CHECK(!pthread_mutex_lock(&w->mutex));w->ready=1;CHECK(!pthread_cond_broadcast(&w->condition));while(!w->go)CHECK(!pthread_cond_wait(&w->condition,&w->mutex));CHECK(!pthread_mutex_unlock(&w->mutex));
    pause_after_wake=w->pause;
    if(w->address){uint64_t r[31]={(uintptr_t)w->address,2,0,(uint64_t)-1};w->first=dm_host_svc(w->h,0x34,r);}else w->first=wait_event(w->h,w->event,w->timeout,&w->index);
    pause_after_wake=0;CHECK(!pthread_mutex_lock(&w->mutex));w->first_done=1;CHECK(!pthread_cond_broadcast(&w->condition));while(!w->second_go&&!w->finish)CHECK(!pthread_cond_wait(&w->condition,&w->mutex));int second=w->second_go;CHECK(!pthread_mutex_unlock(&w->mutex));
    if(second)w->second=wait_event(w->h,w->event,1000000,&w->index);
    if(!w->detach_implicit)CHECK(!dm_guest_detach());return NULL;
}
static void start(worker*w,dm_host*h,uint32_t event_handle,int pause,int64_t timeout,uint32_t*address){
    *w=(worker){.h=h,.event=event_handle,.pause=pause,.timeout=timeout,.address=address};CHECK(!pthread_mutex_init(&w->mutex,NULL));CHECK(!pthread_cond_init(&w->condition,NULL));
    if(pause){pause_gate.host=h;pause_gate.armed=1;pause_gate.entered=0;pause_gate.release=0;}
    CHECK(!pthread_create(&w->thread,NULL,worker_main,w));CHECK(!pthread_mutex_lock(&w->mutex));while(!w->ready)CHECK(!pthread_cond_wait(&w->condition,&w->mutex));w->go=1;CHECK(!pthread_cond_broadcast(&w->condition));CHECK(!pthread_mutex_unlock(&w->mutex));
}
static void first_done(worker*w){CHECK(!pthread_mutex_lock(&w->mutex));while(!w->first_done)CHECK(!pthread_cond_wait(&w->condition,&w->mutex));CHECK(!pthread_mutex_unlock(&w->mutex));}
static void join(worker*w,int second){CHECK(!pthread_mutex_lock(&w->mutex));w->second_go=second;w->finish=!second;CHECK(!pthread_cond_broadcast(&w->condition));CHECK(!pthread_mutex_unlock(&w->mutex));CHECK(!pthread_join(w->thread,NULL));pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);}
static void paused(void){CHECK(!pthread_mutex_lock(&pause_gate.mutex));while(!pause_gate.entered)CHECK(!pthread_cond_wait(&pause_gate.condition,&pause_gate.mutex));CHECK(!pthread_mutex_unlock(&pause_gate.mutex));}
static void resume(void){CHECK(!pthread_mutex_lock(&pause_gate.mutex));pause_gate.release=1;CHECK(!pthread_cond_broadcast(&pause_gate.condition));CHECK(!pthread_mutex_unlock(&pause_gate.mutex));}
static void priority(dm_host*h,uint64_t handle,uint64_t result,uint32_t value){uint64_t r[31]={0,handle,0xaabbccdd};CHECK(dm_host_svc(h,0x0c,r)==result);CHECK(r[1]==(result?handle:value)&&r[2]==0xaabbccdd);}
static void semantics(void){
    dm_host*h=dm_host_create(2u*1024u*1024u);CHECK(h);dm_gate_table gates={0};uint32_t self;CHECK(!dm_guest_bootstrap_main(h,&gates,&self));event_pair e=event(h);size_t index;
    CHECK(call(h,0x19,0)==DM_SVC_INVALID_HANDLE&&call(h,0x19,e.read)==DM_SVC_INVALID_HANDLE&&call(h,0x19,e.write)==DM_SVC_INVALID_HANDLE&&call(h,0x19,0xffff8001u)==DM_SVC_INVALID_HANDLE);
    priority(h,self,0,44);priority(h,0x1ffff8000ull,0,44);priority(h,e.read,DM_SVC_INVALID_HANDLE,0);priority(h,e.write,DM_SVC_INVALID_HANDLE,0);priority(h,0,DM_SVC_INVALID_HANDLE,0);
    CHECK(!call(h,0x19,self)&&!call(h,0x19,self));CHECK(wait_event(h,0,-1,&index)==DM_SVC_INVALID_HANDLE);
    CHECK(!call(h,0x11,e.write));CHECK(!wait_event(h,e.read,-1,&index));CHECK(!call(h,0x12,e.write));
    CHECK(wait_event(h,e.read,0,&index)==TIMEOUT);CHECK(wait_event(h,e.read,-1,&index)==DM_SVC_CANCELLED);CHECK(wait_event(h,e.read,1000000,&index)==TIMEOUT);
    CHECK(!call(h,0x19,0xffff8000u));CHECK(wait_event(h,e.read,-1,&index)==DM_SVC_CANCELLED);
    worker w;
    for(unsigned order=0;order<3;order++){
        start(&w,h,e.read,1,-1,NULL);wait_count(h,0,1);
        CHECK(!call(h,order==0?0x19:0x11,order==0?w.handle:e.write));paused();
        if(order==0)CHECK(!call(h,0x11,e.write));else{CHECK(!call(h,0x12,e.write));if(order==1)CHECK(!call(h,0x19,w.handle));}
        resume();first_done(&w);CHECK(w.first==(order==0?DM_SVC_CANCELLED:0));
        if(order==0){CHECK(w.index==UINT32_MAX);CHECK(!call(h,0x12,e.write));}
        join(&w,1);CHECK(w.second==(order==1?DM_SVC_CANCELLED:TIMEOUT));priority(h,w.handle,0,44);CHECK(!call(h,0x19,w.handle));CHECK(!call(h,0x16,w.handle));CHECK(call(h,0x19,w.handle)==DM_SVC_INVALID_HANDLE);priority(h,w.handle,DM_SVC_INVALID_HANDLE,0);
    }
    start(&w,h,e.read,1,-1,NULL);wait_count(h,0,1);CHECK(!call(h,0x19,w.handle));paused();CHECK(!call(h,0x16,w.handle));CHECK(call(h,0x19,w.handle)==DM_SVC_INVALID_HANDLE);resume();join(&w,0);CHECK(w.first==DM_SVC_CANCELLED);
    event_pair closed=event(h);start(&w,h,closed.read,0,-1,NULL);wait_count(h,0,1);close_event(h,closed);CHECK(!call(h,0x19,w.handle));join(&w,0);CHECK(w.first==DM_SVC_CANCELLED);CHECK(!call(h,0x16,w.handle));
    uint64_t heap[31]={[1]=2u*1024u*1024u};CHECK(!dm_host_svc(h,1,heap));uint32_t*word=(void*)(uintptr_t)heap[1];
    start(&w,h,e.read,0,-1,word);wait_count(h,1,1);CHECK(!call(h,0x19,w.handle));wait_count(h,1,1);uint64_t signal[31]={(uintptr_t)word,0,0,1};CHECK(!dm_host_svc(h,0x35,signal));first_done(&w);CHECK(!w.first);join(&w,1);CHECK(w.second==DM_SVC_CANCELLED);CHECK(!call(h,0x16,w.handle));
    for(unsigned i=0;i<100;i++){start(&w,h,e.read,0,100000,NULL);CHECK(!call(h,0x19,w.handle));first_done(&w);CHECK(w.first==TIMEOUT||w.first==DM_SVC_CANCELLED);join(&w,1);CHECK(w.second==(w.first==TIMEOUT?DM_SVC_CANCELLED:TIMEOUT));CHECK(!call(h,0x16,w.handle));}
    close_event(h,e);CHECK(!dm_guest_detach());CHECK(!call(h,0x16,self));CHECK(!dm_host_destroy(h));
    puts("PASS: pending coalescing/priority, active first completion, signal-reset retention, closed/stale handles, other wait kind and100 timeout/cancel races");
}
static void isolation(void){
    dm_host*a=dm_host_create(2u*1024u*1024u),*b=dm_host_create(2u*1024u*1024u);CHECK(a&&b);event_pair ea=event(a),eb=event(b);worker x,y;
    start(&x,a,ea.read,0,-1,NULL);start(&y,b,eb.read,0,-1,NULL);CHECK(x.handle==y.handle);wait_count(a,0,1);wait_count(b,0,1);
    priority(a,x.handle,0,44);priority(b,y.handle,0,44);priority(a,0xffff8000u,DM_SVC_INVALID_HANDLE,0);
    CHECK(!call(a,0x19,y.handle));first_done(&x);CHECK(x.first==DM_SVC_CANCELLED);wait_count(b,0,1);CHECK(!call(b,0x11,eb.write));join(&x,0);join(&y,0);CHECK(!y.first);
    CHECK(!call(a,0x16,x.handle));CHECK(!call(b,0x16,y.handle));close_event(a,ea);close_event(b,eb);CHECK(!dm_host_destroy(a)&&!dm_host_destroy(b));
    puts("PASS: identical numeric handles resolve only in their own host; independent waiter and lifecycle state");
}
#if defined(__aarch64__)
extern void dm_gate(void),dm_cancel_guest(void);
typedef struct{uintptr_t fn[3];uint64_t event,expected_sp,sp,x18,result,index,done,after,handle,self_cancel;}guest_payload;
_Static_assert(offsetof(guest_payload,self_cancel)==96,"guest ABI");
static void gates(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image image={.header.image_size=page};image.header.segments[0].size=256;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;image.bytes=calloc(1,page);CHECK(image.bytes);
    dm_gate_site sites[3];for(unsigned i=0;i<3;i++){uint32_t ins=i==0?0xd4000321:i==1?0xd4000301:0xd4000141;dm_patch_request req={ins,i*16,128+i*32,(uintptr_t)dm_gate};dm_patch_output out;CHECK(!dm_patch_system(&req,&out));
        memcpy(image.bytes+i*16,&out.replacement,4);uint32_t ret=0xd65f03c0;memcpy(image.bytes+i*16+4,&ret,4);memcpy(image.bytes+req.stub_offset,out.stub,sizeof(out.stub));sites[i]=out.site;}
    dm_image_mapping code={0};CHECK(!dm_image_map(&image,&code));dm_nso_free(&image);for(unsigned i=0;i<3;i++)sites[i].return_address+=(uintptr_t)code.base;dm_gate_table table={sites,3};
    dm_host*h=dm_host_create(2u*1024u*1024u);CHECK(h);uint64_t r[31]={[1]=2u*1024u*1024u};CHECK(!dm_host_svc(h,1,r));uint8_t*heap=(void*)(uintptr_t)r[1];size_t stack=dm_host_thread_stack_minimum();
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)dm_cancel_guest,4));CHECK(!dm_host_register_executable(h,code.base,256));CHECK(!dm_host_register_thread_stack(h,heap,stack));CHECK(!dm_guest_attach(h,&table));event_pair e=event(h);
    for(unsigned i=0;i<2;i++){guest_payload*p=(void*)(heap+65536);memset(p,0,sizeof(*p));for(unsigned j=0;j<3;j++)p->fn[j]=(uintptr_t)code.base+j*16;p->event=e.read;p->expected_sp=(uintptr_t)heap+stack;p->self_cancel=!i;
        uint64_t create[31]={0,(uintptr_t)dm_cancel_guest,(uintptr_t)p,p->expected_sp,44,(uint32_t)-2};CHECK(!dm_host_svc(h,8,create));uint32_t handle=(uint32_t)create[1];p->handle=handle;
        priority(h,handle,0,44);if(!i)CHECK(!call(h,0x19,handle));CHECK(!call(h,9,handle));if(i){wait_count(h,0,1);CHECK(!call(h,0x19,handle));}
        uint64_t wait[31]={0,(uintptr_t)&handle,1,3000000000};CHECK(!dm_host_svc(h,0x18,wait));CHECK(p->done&&!p->after&&p->sp==p->expected_sp&&p->result==DM_SVC_CANCELLED&&p->index==(i?UINT32_MAX:0));CHECK(!call(h,0x16,handle));}
    close_event(h,e);CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));CHECK(!dm_image_unmap(&code));puts("PASS: pure ASM rewritten0x19 w0low32/x0result, prestart/active cancellation, exactSP/x18 and terminal gate");
}
#endif
int main(void){semantics();isolation();
#if defined(__aarch64__)
    gates();
#endif
    return 0;
}
