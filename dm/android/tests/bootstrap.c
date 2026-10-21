// 无游戏引导／SDK 式栈回归；依据公开线程 ABI，以自有字段布局和入口验证自动租约与初始 SP。
// Game-free bootstrap / SDK-like stack regression; use owned layouts/entries with the public thread ABI to verify automatic leases and initial SP.
#include "../runtime/internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define STACK 65536u
static const dm_gate_table gates={0};
static uint64_t call(dm_host*h,unsigned n,uint32_t handle){uint64_t r[31]={handle};return dm_host_svc(h,n,r);}
static uint64_t wait_thread(dm_host*h,uint32_t handle,int64_t ns){uint64_t r[31]={0,(uintptr_t)&handle,1,(uint64_t)ns};return dm_host_svc(h,0x18,r);}
typedef struct{dm_host*host;uint32_t handle;}native_thread;
static void*destructor_thread(void*arg){
    native_thread*t=arg;CHECK(!dm_guest_bootstrap_main(t->host,&gates,&t->handle));return NULL;
}
static void main_bootstrap(void){
    dm_host*h=dm_host_create(UNIT);CHECK(h);uint32_t handle=123;
    CHECK(dm_guest_register_current_thread(44,&handle)==ENOENT&&handle==123);
    CHECK(dm_guest_bootstrap_main(h,&gates,NULL)==EINVAL&&!dm_guest_current());
    CHECK(!dm_guest_bootstrap_main(h,&gates,&handle));dm_guest_context*c=dm_guest_current();
    CHECK(c&&c->thread_handle==handle&&c->thread_state);uint64_t id=c->thread_id;
    uint32_t tc_handle;uint64_t tc_id;memcpy(&tc_handle,(void*)(c->slots[DM_SLOT_TLR]+0x3b0),4);
    memcpy(&tc_id,(void*)(c->slots[DM_SLOT_TLR]+0x3b8),8);CHECK(tc_handle==handle&&tc_id==id);
    uint64_t r[31]={[1]=handle};CHECK(!dm_host_svc(h,0x25,r)&&r[1]==id);
    CHECK(dm_guest_register_current_thread(44,&handle)==EALREADY);
    CHECK(dm_guest_bootstrap_main(h,&gates,&handle)==EALREADY&&dm_guest_current()==c);
    CHECK(call(h,9,handle)==DM_SVC_BAD_STATE&&call(h,0x0a,0)==DM_SVC_BAD_STATE);
    CHECK(wait_thread(h,handle,0)==0xea01);CHECK(dm_host_destroy(h)==EBUSY);
    CHECK(!dm_guest_detach());CHECK(!wait_thread(h,handle,0));CHECK(!call(h,0x16,handle));
    native_thread native={.host=h};pthread_t thread;CHECK(!pthread_create(&thread,NULL,destructor_thread,&native));
    CHECK(!pthread_join(thread,NULL));CHECK(!wait_thread(h,native.handle,0));CHECK(!call(h,0x16,native.handle));
    uint32_t handles[64];for(unsigned i=0;i<64;i++){
        CHECK(!dm_guest_bootstrap_main(h,&gates,&handles[i]));CHECK(!dm_guest_detach());
    }
    handle=123;CHECK(dm_guest_bootstrap_main(h,&gates,&handle)==ENOSPC&&handle==123&&!dm_guest_current());
    for(unsigned i=0;i<64;i++)CHECK(!call(h,0x16,handles[i]));
    CHECK(!dm_guest_bootstrap_main(h,&gates,&handle));CHECK(!call(h,0x16,handle));
    CHECK(dm_guest_current()->thread_handle==handle);CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));
}
#if defined(__aarch64__)
extern void dm_test_sdk_entry(void*);
typedef struct{dm_host*host;uintptr_t expected_top;unsigned runs;}payload;
static void callback(void*tt){
    uintptr_t sp;payload*p;memcpy(&sp,(uint8_t*)tt+0x28,8);memcpy(&p,(uint8_t*)tt+0x30,8);
    CHECK(sp==p->expected_top);CHECK(dm_guest_current()->thread_handle);p->runs++;
    uint64_t r[31]={0};dm_host_svc(p->host,0x0a,r);abort();
}
static void field(void*tt,size_t offset,uint64_t value){memcpy((uint8_t*)tt+offset,&value,8);}
static uint64_t create(dm_host*h,void*tt,uintptr_t top,uint32_t*out){
    uint64_t r[31]={0,(uintptr_t)dm_test_sdk_entry,(uintptr_t)tt,top,44,(uint32_t)-2};
    uint64_t rc=dm_host_svc(h,8,r);if(!rc)*out=(uint32_t)r[1];else CHECK(r[1]==(uintptr_t)dm_test_sdk_entry);return rc;
}
static void sdk_stacks(void){
    dm_host*h=dm_host_create(UNIT);CHECK(h);uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));uint8_t*heap=(void*)(uintptr_t)r[1];
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)dm_test_sdk_entry,16));
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)callback,4));
    dm_sdk_thread_abi abi={.entry=(uintptr_t)dm_test_sdk_entry,.thread_type_size=0x80,.stack_base_offset=0x10,.stack_size_offset=0x18,.top_reserve=32};
    dm_sdk_thread_abi bad=abi;bad.stack_base_offset=0x80;CHECK(dm_host_set_sdk_thread_abi(h,&bad)==EINVAL);
    bad=abi;bad.entry=(uintptr_t)heap;CHECK(dm_host_set_sdk_thread_abi(h,&bad)==EINVAL);
    uint8_t*base=heap+STACK;uintptr_t top=(uintptr_t)base+STACK-32;void*tt=heap;
    payload p={.host=h,.expected_top=top};field(tt,0x10,(uintptr_t)base);field(tt,0x18,STACK);field(tt,0x30,(uintptr_t)&p);
    field((void*)top,0,(uintptr_t)callback);uint32_t main_handle,handle=0;
    CHECK(!dm_guest_bootstrap_main(h,&gates,&main_handle));CHECK(create(h,tt,top,&handle)==DM_SVC_INVALID_ADDRESS);
    CHECK(dm_host_set_sdk_thread_abi(h,&abi)==EBUSY);CHECK(!dm_guest_detach());CHECK(!call(h,0x16,main_handle));
    CHECK(!dm_host_set_sdk_thread_abi(h,&abi));CHECK(!dm_guest_bootstrap_main(h,&gates,&main_handle));
    CHECK(create(h,tt,top+16,&handle)==DM_SVC_INVALID_ADDRESS);
    CHECK(create(h,heap+UNIT,top,&handle)==DM_SVC_INVALID_ADDRESS);
    field(tt,0x18,dm_host_thread_stack_minimum()-16);CHECK(create(h,tt,top-STACK+dm_host_thread_stack_minimum()-16,&handle)==DM_SVC_INVALID_ADDRESS);field(tt,0x18,STACK);
    field(tt,0x10,UINT64_MAX-15);CHECK(create(h,tt,top,&handle)==DM_SVC_INVALID_ADDRESS);field(tt,0x10,(uintptr_t)base);
    field((void*)top,0,(uintptr_t)heap);CHECK(create(h,tt,top,&handle)==DM_SVC_INVALID_ADDRESS);field((void*)top,0,(uintptr_t)callback);
    CHECK(!dm_host_register_thread_stack(h,base,STACK));CHECK(create(h,tt,top,&handle)==DM_SVC_INVALID_ADDRESS);
    CHECK(!dm_host_unregister_thread_stack(h,base+STACK));
    for(unsigned i=0;i<80;i++){CHECK(!create(h,tt,top,&handle));CHECK(!call(h,0x16,handle));}
    CHECK(!create(h,tt,top,&handle));CHECK(dm_host_register_thread_stack(h,base,STACK)==EINVAL);
    r[1]=0;CHECK(dm_host_svc(h,1,r)==DM_SVC_BAD_STATE);
    CHECK(!call(h,9,handle));CHECK(!wait_thread(h,handle,3000000000ll));CHECK(p.runs==1);
    CHECK(!call(h,0x16,handle));CHECK(!dm_host_register_thread_stack(h,base,STACK));CHECK(!dm_host_unregister_thread_stack(h,base+STACK));
    for(unsigned i=0;i<80;i++){
        CHECK(!create(h,tt,top,&handle));CHECK(!call(h,9,handle));CHECK(!wait_thread(h,handle,3000000000ll));CHECK(!call(h,0x16,handle));
    }
    CHECK(p.runs==81);CHECK(!dm_guest_detach());CHECK(!call(h,0x16,main_handle));CHECK(!dm_host_destroy(h));
}
#endif
int main(void){main_bootstrap();
#if defined(__aarch64__)
    sdk_stacks();
#endif
    puts("PASS: main registration/rollback, TCB identity, SDK-style stack leases, reserved top and exact entry SP");return 0;
}
