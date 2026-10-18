// 无游戏 GetThreadPriority 验证：虚拟优先级、句柄生命周期及真实改写门的 x0/x1 ABI。
// Game-free GetThreadPriority verification: virtual priorities, handle lifetimes and x0/x1 ABI through a real rewritten gate.
#include "../runtime/internal.h"
#include "../runtime/bridge.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "generated_entry.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); abort(); } } while (0)
#define UNIT (2u*1024u*1024u)
#define CURRENT 0xffff8000u
#define HIGH 0xa1b2c3d400000000ull
static dm_gate_table gates;
#if defined(__aarch64__)
extern void dm_gate(void);
static dm_image_mapping program;
static dm_gate_site sites[2];
typedef struct { uint64_t result,priority; } returned;
static void word(uint8_t *p,uint32_t value) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i)); }
static void prepare(void) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image image={.header.image_size=page};
    image.header.segments[0].size=192;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);
    word(image.bytes,0xa9bf7bfdu);word(image.bytes+8,0xa8c17bfdu);word(image.bytes+12,0xd65f03c0u);
    word(image.bytes+36,0xd4200000u);
    for(unsigned i=0;i<2;i++) {
        dm_patch_request request={.instruction=0xd4000001u|((i?0x0au:0x0cu)<<5),
            .original_offset=i?32:4,.stub_offset=128+32*i,.host_gate=(uintptr_t)dm_gate};
        dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));
        word(image.bytes+request.original_offset,patch.replacement);memcpy(image.bytes+request.stub_offset,patch.stub,sizeof patch.stub);sites[i]=patch.site;
    }
    CHECK(!dm_image_map(&image,&program));dm_nso_free(&image);
    for(unsigned i=0;i<2;i++)sites[i].return_address+=(uintptr_t)program.base;
    gates=(dm_gate_table){sites,2};
}
#endif
static void DM_NO_UBSAN_FUNCTION query(dm_host *host,uint64_t handle,uint64_t expected_result,uint64_t expected_priority,int real_gate) {
    uint64_t r[31];for(unsigned i=0;i<31;i++)r[i]=0x1234567800000000ull+i;r[1]=handle;
    CHECK(dm_host_svc(host,0x0c,r)==expected_result&&r[0]==expected_result&&r[1]==expected_priority);
    for(unsigned i=2;i<31;i++)CHECK(r[i]==0x1234567800000000ull+i);
#if defined(__aarch64__)
    if(real_gate) {
        dm_guest_context *context=dm_guest_current();CHECK(context&&context->host==host&&dm_bridge_on_native_stack());
        uint64_t calls=context->bridge->calls;
        typedef returned(*function)(uint64_t,uint64_t);
        returned value=((function)(void*)program.base)(0xdeadbeef,handle);
        CHECK(value.result==expected_result&&value.priority==expected_priority&&context->bridge->calls==calls+1);
        CHECK(context->bridge->last_native_sp>=context->bridge->native_low&&context->bridge->last_native_sp<context->bridge->native_high);
        CHECK(dm_bridge_on_native_stack());
    }
#else
    (void)real_gate;
#endif
}
static void bad(dm_host *host,uint64_t handle,int gate) { query(host,handle,DM_SVC_INVALID_HANDLE,handle,gate); }
static void close_handle(dm_host *host,uint32_t handle) { uint64_t r[31]={handle};CHECK(!dm_host_svc(host,0x16,r)); }
typedef struct { dm_host *host; uint32_t handle; int priority; } external;
static void *registered(void *arg) {
    external *state=arg;CHECK(!dm_guest_attach(state->host,&gates));CHECK(!dm_guest_register_current_thread(state->priority,&state->handle));
    query(state->host,CURRENT,0,(uint32_t)state->priority,1);CHECK(!dm_guest_detach());return NULL;
}
int main(void) {
#if defined(__aarch64__)
    prepare();
#endif
    dm_host *host=dm_host_create(UNIT),*other=dm_host_create(UNIT);CHECK(host&&other);
    bad(host,CURRENT,0);bad(host,HIGH|CURRENT,0);bad(host,0,0);
#if defined(__aarch64__)
    CHECK(!dm_host_register_executable(host,program.base,192));
#endif
    CHECK(!dm_guest_attach(host,&gates));bad(host,CURRENT,1);bad(other,CURRENT,0);
    uint32_t mine;CHECK(!dm_guest_register_current_thread(44,&mine));
    int native_policy_before,native_policy_after;struct sched_param native_before,native_after;
    CHECK(!pthread_getschedparam(pthread_self(),&native_policy_before,&native_before));
    query(host,CURRENT,0,44,1);query(host,HIGH|CURRENT,0,44,1);query(host,mine,0,44,1);query(host,HIGH|mine,0,44,1);
    bad(other,HIGH|CURRENT,0);bad(host,0xffff8001u,1);
    uint64_t event[31]={0};CHECK(!dm_host_svc(host,0x45,event));bad(host,event[1],1);bad(host,HIGH|event[2],1);
    close_handle(host,(uint32_t)event[1]);close_handle(host,(uint32_t)event[2]);
    external state={host,0,12};pthread_t worker;CHECK(!pthread_create(&worker,NULL,registered,&state));CHECK(!pthread_join(worker,NULL));
    query(host,state.handle,0,12,1);close_handle(host,state.handle);bad(host,state.handle,1);
    puts("PASS: current/registered/completed external thread, pseudo/w1-low32, wrong-host and event rejection");
#if defined(__aarch64__)
    uint64_t heap[31]={[1]=UNIT};CHECK(!dm_host_svc(host,1,heap));uint8_t *base=(uint8_t*)(uintptr_t)heap[1];
    size_t stack_size=65536;CHECK(!dm_host_register_thread_stack(host,base,stack_size));
    const unsigned priorities[]={0,17,63};uint32_t previous=0;
    for(unsigned i=0;i<3;i++) {
        uint64_t create[31]={0,(uintptr_t)program.base+32,0,(uintptr_t)(base+stack_size),priorities[i],(uint32_t)-2};
        CHECK(!dm_host_svc(host,8,create));uint32_t handle=(uint32_t)create[1];CHECK(handle!=previous);
        if(previous)bad(host,previous,1);
        query(host,handle,0,priorities[i],1);query(host,HIGH|handle,0,priorities[i],1);
        uint64_t start[31]={handle};CHECK(!dm_host_svc(host,9,start));
        uint64_t wait[31]={0,(uintptr_t)&handle,1,3000000000ull};CHECK(!dm_host_svc(host,0x18,wait)&&wait[1]==0);
        query(host,handle,0,priorities[i],1);close_handle(host,handle);bad(host,HIGH|handle,1);previous=handle;
    }
    CHECK(!dm_host_unregister_thread_stack(host,base+stack_size));
    puts("PASS: unstarted/completed managed priorities 0/17/63, stale generations and real ExitThread gates");
#endif
    CHECK(!pthread_getschedparam(pthread_self(),&native_policy_after,&native_after));
    CHECK(native_policy_before==native_policy_after&&native_before.sched_priority==native_after.sched_priority);
    close_handle(host,mine);bad(host,mine,1);query(host,CURRENT,0,44,1);
    CHECK(!dm_guest_detach());bad(host,CURRENT,0);query(host,0,DM_SVC_INVALID_HANDLE,0,0);
    CHECK(!dm_host_destroy(other));CHECK(!dm_host_destroy(host));
#if defined(__aarch64__)
    CHECK(!dm_image_unmap(&program));puts("PASS: rewritten GetThreadPriority x0 Result/x1 output and native service SP; native scheduling unchanged");
#endif
    return 0;
}
