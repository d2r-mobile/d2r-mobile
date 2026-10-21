// 无游戏线程生命周期验证；依据 SVC 线程／等待 ABI，运行自有入口与改写 ExitThread，覆盖栈租约、TLS、句柄关闭和混合等待。
// Game-free thread lifecycle verification; based on thread/wait SVC ABI, execute owned entries and rewritten ExitThread, checking stack leases, TLS, handle closure and mixed waits.
#include "../runtime/internal.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define STACK 65536u
#define BAD_HANDLE 0xe401u
#define TIMEOUT 0xea01u
#if defined(__aarch64__)
static uint64_t call(dm_host*h,unsigned n,uint32_t handle){uint64_t r[31]={handle};return dm_host_svc(h,n,r);}
static uint64_t wait_any(dm_host*h,const uint32_t*handles,size_t n,int64_t ns,size_t*index){
    uint64_t r[31]={0,(uintptr_t)handles,n,(uint64_t)ns};uint64_t result=dm_host_svc(h,0x18,r);*index=r[1];return result;
}
static uint64_t mono(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return(uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void wait_state(dm_host*h,int waiters,size_t count){uint64_t deadline=mono()+3000000000ull;
    for(;;){pthread_mutex_lock(&h->mutex);size_t n=waiters?dm_host_sync_waiters(h):dm_host_threads_pinned(h);pthread_mutex_unlock(&h->mutex);
        if(n==count)return;CHECK(mono()<deadline);struct timespec pause={.tv_nsec=1000000};nanosleep(&pause,NULL);}}
static uint64_t id(dm_host*h,uint32_t handle){uint64_t r[31]={[1]=handle};CHECK(!dm_host_svc(h,0x25,r));return r[1];}
extern void dm_gate(void);
typedef struct{dm_host*host;dm_event*ready,*release;void(*exit_fn)(void);uintptr_t sp,tls;uint32_t handle;uint64_t thread_id;unsigned runs;}payload;
static void guest_entry(uint64_t argument){
    payload*p=(payload*)(uintptr_t)argument;dm_guest_context*c=dm_guest_current();CHECK(c&&c->host==p->host);
    __asm__ volatile("mov %0, sp":"=r"(p->sp));p->tls=c->slots[DM_SLOT_TLS];p->handle=c->thread_handle;
    p->thread_id=id(p->host,0xffff8000u);CHECK(p->thread_id==c->thread_id);
    uint32_t tcb_handle;memcpy(&tcb_handle,(void*)(c->slots[DM_SLOT_TLR]+0x200+0x1b0),4);CHECK(tcb_handle==p->handle);
    CHECK(c->slots[DM_SLOT_X18]==0);c->slots[DM_SLOT_X18]=0x12345678;
    CHECK(!dm_event_signal(p->ready));CHECK(!dm_event_wait(p->release,3000000000ll));p->runs++;
    p->exit_fn();abort();
}
static void put(uint8_t*p,uint32_t w){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(w>>(8*b));}
static dm_image_mapping exit_program(dm_gate_site*site){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image im={.header.image_size=page};
    im.header.segments[0].size=160;im.header.segments[1].memory_offset=(uint32_t)page;im.header.segments[2].memory_offset=(uint32_t)page;
    im.bytes=calloc(1,page);CHECK(im.bytes);put(im.bytes,0xa9bf7bfd);put(im.bytes+8,0xa8c17bfd);put(im.bytes+12,0xd65f03c0);
    dm_patch_request request={.instruction=0xd4000141,.original_offset=4,.stub_offset=128,.host_gate=(uintptr_t)dm_gate};
    dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));put(im.bytes+4,patch.replacement);memcpy(im.bytes+128,patch.stub,sizeof(patch.stub));
    dm_image_mapping mapped={0};CHECK(!dm_image_map(&im,&mapped));*site=patch.site;site->return_address+=(uintptr_t)mapped.base;dm_nso_free(&im);return mapped;
}
static uint64_t create(dm_host*h,payload*p,void*top,uint32_t*out){
    uint64_t r[31]={0,(uintptr_t)guest_entry,(uintptr_t)p,(uintptr_t)top,44,(uint32_t)-2};
    uint64_t result=dm_host_svc(h,8,r);if(!result)*out=(uint32_t)r[1];else CHECK(r[1]==(uintptr_t)guest_entry);return result;
}
typedef struct{dm_host*host;uint32_t handles[2];size_t index;uint64_t result;}wait_worker;
static void*wait_thread(void*arg){wait_worker*w=arg;w->result=wait_any(w->host,w->handles,2,3000000000ll,&w->index);return NULL;}
static void rejects_startup_failure(dm_host*h,payload*p,void*top,dm_gate_table*gates){
    uint32_t handle;CHECK(!create(h,p,top,&handle));const dm_gate_site*sites=gates->sites;
    // 在没有客体执行时故意破坏宿主门配置，确定性注入上下文初始化失败，不依赖内存耗尽时机。
    // Deliberately invalidate host gate configuration with no guest running to inject context-init failure deterministically, without relying on OOM timing.
    gates->sites=NULL;CHECK(call(h,9,handle)==0xce01);gates->sites=sites;
    size_t index;CHECK(wait_any(h,&handle,1,0,&index)==TIMEOUT&&!p->runs);
    CHECK(dm_host_unregister_thread_stack(h,top)==EBUSY);
    CHECK(!call(h,0x16,handle));wait_state(h,0,0);
    CHECK(!dm_host_unregister_thread_stack(h,top));CHECK(!dm_host_register_thread_stack(h,(uint8_t*)top-STACK,STACK));
    CHECK(!create(h,p,top,&handle));gates->sites=NULL;CHECK(call(h,9,handle)==0xce01);gates->sites=sites;
    CHECK(!call(h,9,handle));CHECK(!dm_event_wait(p->ready,3000000000ll));CHECK(!dm_event_signal(p->release));
    CHECK(!wait_any(h,&handle,1,3000000000ll,&index)&&p->runs==1);CHECK(!call(h,0x16,handle));
    CHECK(!dm_event_clear(p->release));p->runs=0;
}
typedef struct{dm_host*host;uint32_t handle;uint64_t result;}start_worker;
static void*start_race(void*arg){start_worker*w=arg;w->result=call(w->host,9,w->handle);return NULL;}
static void concurrent_threads(dm_host*h,payload*p,uint8_t*heap){
    payload group[8];uint32_t handles[8];size_t index;
    for(unsigned i=0;i<8;i++){
        group[i]=*p;group[i].ready=dm_event_create(1);group[i].runs=0;CHECK(group[i].ready);
        CHECK(!create(h,&group[i],heap+(2+i)*STACK,&handles[i]));CHECK(!call(h,9,handles[i]));
    }
    for(unsigned i=0;i<8;i++){
        CHECK(!dm_event_wait(group[i].ready,3000000000ll));CHECK(group[i].handle==handles[i]);
        for(unsigned j=0;j<i;j++)CHECK(group[i].tls!=group[j].tls&&group[i].thread_id!=group[j].thread_id);
    }
    CHECK(!dm_event_signal(p->release));
    for(unsigned i=0;i<8;i++){
        CHECK(!wait_any(h,&handles[i],1,3000000000ll,&index)&&group[i].runs==1);
        CHECK(!call(h,0x16,handles[i]));CHECK(!dm_event_destroy(group[i].ready));
    }
    CHECK(!dm_event_clear(p->release));
}
static void lifecycle(void){
    dm_host*h=dm_host_create(4*UNIT);CHECK(h);uint64_t r[31]={[1]=4*UNIT};CHECK(!dm_host_svc(h,1,r));uint8_t*heap=(uint8_t*)(uintptr_t)r[1];
    dm_gate_site site;dm_image_mapping mapped=exit_program(&site);dm_gate_table gates={&site,1};
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)guest_entry,4));
    CHECK(!dm_host_register_executable(h,mapped.base,160));
    CHECK(dm_host_register_writable(h,mapped.base,4)==EINVAL);
    CHECK(dm_host_register_thread_stack(h,heap,dm_host_thread_stack_minimum()-16)==EINVAL);
    CHECK(dm_host_register_thread_stack(h,heap+1,STACK)==EINVAL);
    CHECK(dm_host_register_thread_stack(h,heap+4*UNIT,STACK)==EINVAL);
    for(unsigned i=0;i<64;i++)CHECK(!dm_host_register_thread_stack(h,heap+STACK+i*STACK,STACK));
    CHECK(dm_host_register_thread_stack(h,heap+STACK,STACK)==EINVAL);
    CHECK(dm_host_register_thread_stack(h,heap+STACK/2,STACK)==EINVAL);
    CHECK(dm_host_register_thread_stack(h,heap+5*UNIT/2,STACK)==ENOSPC);
    CHECK(!dm_guest_attach(h,&gates));dm_guest_context*parent=dm_guest_current();parent->slots[DM_SLOT_X18]=99;
    uint64_t parent_id=id(h,0xffff8000u);CHECK(parent_id==parent->thread_id);
    dm_event*ready=dm_event_create(1),*release=dm_event_create(0);CHECK(ready&&release);
    payload p={.host=h,.ready=ready,.release=release,.exit_fn=(void(*)(void))(void*)mapped.base};
    uint64_t bad[31]={0,(uintptr_t)guest_entry,(uintptr_t)&p,(uintptr_t)(heap+2*STACK),64,(uint32_t)-2};
    CHECK(dm_host_svc(h,8,bad)==0xf001);bad[4]=44;bad[5]=4;CHECK(dm_host_svc(h,8,bad)==0xf001);
    bad[5]=(uint32_t)-2;bad[1]=(uintptr_t)heap;CHECK(dm_host_svc(h,8,bad)==DM_SVC_INVALID_ADDRESS);
    bad[1]=(uintptr_t)guest_entry+1;CHECK(dm_host_svc(h,8,bad)==DM_SVC_INVALID_ADDRESS);
    concurrent_threads(h,&p,heap);
    void*top=heap+2*STACK;rejects_startup_failure(h,&p,top,&gates);
    uint32_t handle=0;CHECK(!create(h,&p,top,&handle));uint64_t child_id=id(h,handle);CHECK(child_id!=parent_id);
    CHECK(dm_host_unregister_thread_stack(h,top)==EBUSY);
    CHECK(create(h,&p,top,&handle)==DM_SVC_BAD_STATE);
    size_t index;CHECK(wait_any(h,&handle,1,0,&index)==TIMEOUT);CHECK(!p.runs);
    CHECK(call(h,0x0a,0)==DM_SVC_BAD_STATE);
    CHECK(call(h,0x11,handle)==BAD_HANDLE&&call(h,0x12,handle)==BAD_HANDLE&&call(h,0x17,handle)==DM_SVC_BAD_STATE);
    r[1]=0;CHECK(dm_host_svc(h,1,r)==DM_SVC_BAD_STATE);
    CHECK(!dm_guest_detach());CHECK(dm_host_destroy(h)==EBUSY);CHECK(!call(h,0x16,handle));CHECK(call(h,9,handle)==BAD_HANDLE);
    CHECK(!dm_guest_attach(h,&gates));parent=dm_guest_current();parent->slots[DM_SLOT_X18]=99;
    uint32_t previous=handle;
    for(unsigned i=0;i<80;i++){CHECK(!create(h,&p,top,&handle)&&handle!=previous);previous=handle;CHECK(!call(h,0x16,handle));}
    CHECK(!create(h,&p,top,&handle));child_id=id(h,handle);CHECK(!call(h,9,handle));
    CHECK(!dm_event_wait(ready,3000000000ll));CHECK(p.handle==handle&&p.thread_id==child_id&&p.tls!=parent->slots[DM_SLOT_TLS]);
    CHECK(p.sp>=(uintptr_t)top-STACK&&p.sp<(uintptr_t)top);CHECK(parent->slots[DM_SLOT_X18]==99);
    CHECK(call(h,9,handle)==DM_SVC_BAD_STATE);CHECK(!dm_event_signal(release));
    CHECK(!wait_any(h,&handle,1,3000000000ll,&index)&&index==0&&p.runs==1);
    CHECK(!wait_any(h,&handle,1,0,&index));CHECK(!call(h,0x16,handle));wait_state(h,0,0);
    CHECK(!dm_event_clear(release));p.runs=0;CHECK(!create(h,&p,top,&handle));CHECK(!call(h,9,handle));CHECK(!dm_event_wait(ready,3000000000ll));
    uint64_t event[31]={0};CHECK(!dm_host_svc(h,0x45,event));uint32_t event_w=event[1],event_r=event[2];
    wait_worker w={.host=h,.handles={handle,event_r}};pthread_t waiter;CHECK(!pthread_create(&waiter,NULL,wait_thread,&w));wait_state(h,1,1);
    CHECK(!call(h,0x16,handle));CHECK(!dm_event_signal(release));CHECK(!pthread_join(waiter,NULL));CHECK(!w.result&&w.index==0&&p.runs==1);
    CHECK(!call(h,0x16,event_w)&&!call(h,0x16,event_r));wait_state(h,0,0);
    top=heap+65*STACK;CHECK(!create(h,&p,top,&handle));CHECK(!call(h,9,handle));CHECK(!dm_event_wait(ready,3000000000ll));
    CHECK(!wait_any(h,&handle,1,3000000000ll,&index));CHECK(!call(h,0x16,handle));
    r[1]=UNIT;CHECK(!dm_host_svc(h,1,r));CHECK(create(h,&p,top,&handle)==DM_SVC_INVALID_ADDRESS);
    CHECK(!dm_host_unregister_thread_stack(h,top));CHECK(dm_host_unregister_thread_stack(h,top)==ENOENT);
    r[1]=4*UNIT;CHECK(!dm_host_svc(h,1,r));top=heap+2*STACK;
    uint32_t completed[64];for(unsigned i=0;i<64;i++){
        CHECK(!create(h,&p,top,&completed[i]));CHECK(!call(h,9,completed[i]));CHECK(!dm_event_wait(ready,3000000000ll));
        CHECK(!wait_any(h,&completed[i],1,3000000000ll,&index));
    }
    CHECK(create(h,&p,top,&handle)==0xce01);
    CHECK(!call(h,0x16,completed[0]));CHECK(!create(h,&p,top,&handle));CHECK(!call(h,0x16,handle));
    for(unsigned i=1;i<64;i++)CHECK(!call(h,0x16,completed[i]));
    for(unsigned i=0;i<100;i++){
        CHECK(!create(h,&p,top,&handle));start_worker race={h,handle,0};pthread_t starter;
        CHECK(!pthread_create(&starter,NULL,start_race,&race));CHECK(!call(h,0x16,handle));CHECK(!pthread_join(starter,NULL));
        CHECK(!race.result||race.result==BAD_HANDLE);wait_state(h,0,0);
    }
    wait_state(h,0,0);CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));
    CHECK(!dm_event_destroy(ready)&&!dm_event_destroy(release));CHECK(!dm_image_unmap(&mapped));
}
#endif
int main(void){
#if defined(__aarch64__)
    lifecycle();puts("PASS: native guest-stack threads, TLS/TCB IDs, SVC ExitThread gates, mixed waits/close pins, stack/shrink guards and capacity reuse");
#else
    puts("SKIP: native guest threads require ARM64");
#endif
    return 0;
}
