// 有界 TP 键注册表与栈域切换；SAN 钩子仅在纯过渡辅助函数中免插桩，实际服务照常插桩。
// Bounded TP-key registry and stack domains; only pure transition helpers skip SAN instrumentation, actual services stay instrumented.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bridge.h"
#include "gate_frame.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define ASAN_FIBERS 1
#include <sanitizer/common_interface_defs.h>
#endif
#if __has_feature(thread_sanitizer) || defined(__SANITIZE_THREAD__)
#define TSAN_FIBERS 1
#include <sanitizer/tsan_interface.h>
#endif
#define TRANSITION __attribute__((visibility("hidden"),disable_sanitizer_instrumentation,no_sanitize("address","thread","shadow-call-stack")))
_Static_assert(sizeof(dm_bridge_record)==128,"bridge record");
_Static_assert(offsetof(dm_bridge_record,active)==80&&offsetof(dm_bridge_record,domain)==88,"bridge record fields");
_Static_assert(sizeof(dm_bridge_frame)==160&&offsetof(dm_bridge_frame,args)==80,"bridge frame");
_Static_assert(offsetof(dm_bridge_frame,action)==32&&offsetof(dm_bridge_frame,x29)==72,"bridge frame fields");
__attribute__((visibility("hidden"))) _Alignas(64) dm_bridge_record dm_bridge_records[DM_BRIDGE_SLOTS];
static pthread_mutex_t registry_mutex=PTHREAD_MUTEX_INITIALIZER;
static uint64_t next_generation;
// 固定改写峰值为 256 字节 x18 跳板加 800 字节调用门；客体自身深度另计。
// Fixed rewrite peak is a 256-byte x18 stub plus the 800-byte gate; guest live depth is additional.
size_t dm_host_thread_stack_minimum(void){return DM_BRIDGE_SANITIZED?65536u:4096u;}
size_t dm_host_thread_stack_entry_headroom(void){return 1056u+(DM_BRIDGE_SANITIZED?32768u:0u);}
static uintptr_t physical_sp(void){
#if defined(__aarch64__)
    uintptr_t sp;__asm__ volatile("mov %0,sp":"=r"(sp));return sp;
#else
    return (uintptr_t)__builtin_frame_address(0);
#endif
}
static uintptr_t native_key(void){
#if defined(__aarch64__) && defined(__APPLE__)
    uintptr_t key;__asm__ volatile("mrs %0,tpidrro_el0":"=r"(key));return key&~(uintptr_t)7;
#elif defined(__aarch64__)
    uintptr_t key;__asm__ volatile("mrs %0,tpidr_el0":"=r"(key));return key;
#else
    return (uintptr_t)pthread_self();
#endif
}
int dm_bridge_attach(dm_guest_context*c,dm_bridge_sanitizers*san){
    uintptr_t low,high,key=native_key();if(key<=1)return EINVAL;
#if defined(__APPLE__)
    high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());size_t size=pthread_get_stacksize_np(pthread_self());low=high-size;
#else
    pthread_attr_t attr;int error=pthread_getattr_np(pthread_self(),&attr);if(error)return error;
    void*base;size_t size;error=pthread_attr_getstack(&attr,&base,&size);pthread_attr_destroy(&attr);if(error)return error;
    low=(uintptr_t)base;high=low+size;
#endif
    uintptr_t sp=physical_sp();if(!low||high<=low||sp<low||sp>=high)return EINVAL;
    pthread_mutex_lock(&registry_mutex);size_t slot=DM_BRIDGE_SLOTS;int result=ENOSPC;
    for(size_t i=0;i<DM_BRIDGE_SLOTS;i++){
        uintptr_t current=atomic_load_explicit(&dm_bridge_records[i].key,memory_order_relaxed);
        if(current==key){result=EALREADY;goto done;}
        if(!current&&slot==DM_BRIDGE_SLOTS)slot=i;
    }
    if(slot!=DM_BRIDGE_SLOTS){dm_bridge_record*r=&dm_bridge_records[slot];
        memset((char*)r+sizeof(r->key),0,sizeof(*r)-sizeof(r->key));
        #if ASAN_FIBERS || TSAN_FIBERS
        r->flags=1;
#endif
        r->context=c;r->native_low=low;r->native_high=high;r->san=san;r->generation=++next_generation;
        c->bridge=r;atomic_store_explicit(&r->key,key,memory_order_release);result=0;
    }
done:pthread_mutex_unlock(&registry_mutex);return result;
}
void dm_bridge_detach(dm_guest_context*c){
    dm_bridge_record*r=c->bridge;if(!r)return;
    uintptr_t sp=physical_sp();if(r->active||r->domain!=DM_BRIDGE_NATIVE||sp<r->native_low||sp>=r->native_high)abort();
    pthread_mutex_lock(&registry_mutex);atomic_store_explicit(&r->key,0,memory_order_release);c->bridge=NULL;
    pthread_mutex_unlock(&registry_mutex);
}
size_t dm_bridge_registry_count(void){size_t count=0;pthread_mutex_lock(&registry_mutex);
    for(size_t i=0;i<DM_BRIDGE_SLOTS;i++)if(atomic_load_explicit(&dm_bridge_records[i].key,memory_order_relaxed))count++;
    pthread_mutex_unlock(&registry_mutex);return count;
}
int dm_bridge_on_native_stack(void){dm_guest_context*c=dm_guest_current();if(!c||!c->bridge)return 0;
    dm_bridge_record*r=c->bridge;uintptr_t sp=physical_sp();return sp>=r->native_low&&sp<r->native_high;
}
int dm_bridge_prepare(dm_bridge_record*r,uintptr_t*run,uintptr_t low,size_t size){
    if(!r||r->domain!=DM_BRIDGE_NATIVE||r->active||!run||!size||size>UINTPTR_MAX-low)return EINVAL;
#if TSAN_FIBERS
    r->san->native_fiber=__tsan_get_current_fiber();r->san->guest_fiber=__tsan_create_fiber(0);
    if(!r->san->guest_fiber)return ENOMEM;
#endif
    r->run_slot=run;r->guest_low=low;r->guest_high=low+size;return 0;
}
TRANSITION void dm_bridge_start_guest(dm_bridge_record*r){
    r->domain=DM_BRIDGE_GUEST;
#if ASAN_FIBERS
    __sanitizer_start_switch_fiber(&r->san->native_fake,(void*)r->guest_low,r->guest_high-r->guest_low);
#endif
#if TSAN_FIBERS
    __tsan_release(r);
    __tsan_switch_to_fiber(r->san->guest_fiber,__tsan_switch_to_fiber_no_sync);
#endif
}
TRANSITION void dm_bridge_finish_guest(dm_bridge_record*r){
#if TSAN_FIBERS
    __tsan_acquire(r);
#endif
#if ASAN_FIBERS
    __sanitizer_finish_switch_fiber(r->san->guest_fake,NULL,NULL);
#else
    (void)r;
#endif
}
TRANSITION void dm_bridge_finish_native(dm_bridge_record*r){
#if ASAN_FIBERS
    __sanitizer_finish_switch_fiber(r->san->native_fake,NULL,NULL);
#endif
#if TSAN_FIBERS
    __tsan_acquire(r);
    __tsan_destroy_fiber(r->san->guest_fiber);r->san->guest_fiber=NULL;
#endif
    r->domain=DM_BRIDGE_NATIVE;r->run_slot=NULL;r->active=NULL;
}
TRANSITION void dm_bridge_exit_direct(dm_bridge_record*r){
    if(r->domain!=DM_BRIDGE_GUEST||r->active)abort();r->domain=DM_BRIDGE_EXITING;
#if ASAN_FIBERS
    __sanitizer_start_switch_fiber(NULL,(void*)r->native_low,r->native_high-r->native_low);
#endif
#if TSAN_FIBERS
    __tsan_release(r);
    __tsan_switch_to_fiber(r->san->native_fiber,__tsan_switch_to_fiber_no_sync);
#endif
}
TRANSITION dm_bridge_frame*dm_bridge_source(dm_bridge_frame*f){
    if(f->source_domain==DM_BRIDGE_GUEST){dm_bridge_record*r=f->owner;
#if ASAN_FIBERS
        __sanitizer_start_switch_fiber(&r->san->guest_fake,(void*)r->native_low,r->native_high-r->native_low);
#endif
#if TSAN_FIBERS
        __tsan_release(r);
        __tsan_switch_to_fiber(r->san->native_fiber,__tsan_switch_to_fiber_no_sync);
#endif
        (void)r;
    }return f;
}
extern uint64_t gate_dispatch(uint64_t,uint64_t*);
__attribute__((noinline)) static void service(dm_bridge_frame*f){
    dm_bridge_record*r=f->owner;uintptr_t sp=physical_sp();
    if(dm_guest_current()!=r->context||sp<r->native_low||sp>=r->native_high)abort();
#if defined(__aarch64__)
    uintptr_t fpcr,fpsr;__asm__ volatile("mrs %0,fpcr":"=r"(fpcr));__asm__ volatile("mrs %0,fpsr":"=r"(fpsr));
    if(f->source_domain==DM_BRIDGE_GUEST){uintptr_t*run=(uintptr_t*)*r->run_slot;if(fpcr!=run[13]||fpsr!=run[14])abort();}
#endif
    r->calls++;r->last_native_sp=sp;
    if(f->kind==0)f->result=gate_dispatch(f->args[0],(uint64_t*)(uintptr_t)f->args[1]);
    else f->result=dm_bridge_import_dispatch(f->kind,f->args);
}
TRANSITION void dm_bridge_arrive(dm_bridge_frame*f){
#if TSAN_FIBERS
    if(f->source_domain==DM_BRIDGE_GUEST)__tsan_acquire(f->owner);
#endif
#if ASAN_FIBERS
    if(f->source_domain==DM_BRIDGE_GUEST)__sanitizer_finish_switch_fiber(f->owner->san->native_fake,NULL,NULL);
#endif
    service(f);
}
TRANSITION dm_bridge_frame*dm_bridge_depart(dm_bridge_frame*f){
    if(f->source_domain==DM_BRIDGE_GUEST){dm_bridge_record*r=f->owner;
#if ASAN_FIBERS
        __sanitizer_start_switch_fiber(&r->san->native_fake,(void*)r->guest_low,r->guest_high-r->guest_low);
#endif
#if TSAN_FIBERS
        __tsan_release(r);
        __tsan_switch_to_fiber(r->san->guest_fiber,__tsan_switch_to_fiber_no_sync);
#endif
        (void)r;
    }return f;
}
TRANSITION dm_bridge_frame*dm_bridge_resume(dm_bridge_frame*f){
#if TSAN_FIBERS
    if(f->source_domain==DM_BRIDGE_GUEST)__tsan_acquire(f->owner);
#endif
#if ASAN_FIBERS
    if(f->source_domain==DM_BRIDGE_GUEST)__sanitizer_finish_switch_fiber(f->owner->san->guest_fake,NULL,NULL);
#endif
    return f;
}
TRANSITION uintptr_t dm_bridge_retire(dm_bridge_frame*f){
    dm_bridge_record*r=f->owner;
#if TSAN_FIBERS
    __tsan_acquire(r);
#endif
#if ASAN_FIBERS
    __sanitizer_finish_switch_fiber(r->san->guest_fake,NULL,NULL);
    __sanitizer_start_switch_fiber(NULL,(void*)r->native_low,r->native_high-r->native_low);
#endif
#if TSAN_FIBERS
    __tsan_release(r);
    __tsan_switch_to_fiber(r->san->native_fiber,__tsan_switch_to_fiber_no_sync);
#endif
    return *r->run_slot;
}
int dm_bridge_request_exit(dm_guest_context*c){
    dm_bridge_record*r=c->bridge;if(!r||r->domain!=DM_BRIDGE_SERVICE)return 0;
    dm_bridge_frame*f=r->active;if(!f||f->kind||f->source_domain!=DM_BRIDGE_GUEST)return -1;
    const dm_gate_table*t=c->gates;
    for(size_t i=0;i<t->count;i++)if(t->sites[i].return_address==f->args[0]&&t->sites[i].kind==DM_GATE_SVC&&t->sites[i].argument==0x0a){f->action=1;return 1;}
    return -1;
}
