// Opt-in virtual-core policy regression: configured GetInfo0, CreateThread and SVC0x0f state.
// Uses the real rewritten gates on ARM64; no game/SDK execution and no physical affinity.
#include "../runtime/internal.h"
#include "../runtime/bridge.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "generated_entry.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); abort(); } } while (0)
#define UNIT (2u*1024u*1024u)
#define STACK 65536u
#define CURRENT 0xffff8001u
#define CURRENT_THREAD 0xffff8000u
#define HIGH_CORE (UINT64_C(1)<<63)

typedef struct { uint64_t result, value; } returned;
typedef struct { uint64_t result, handle; } created;

#if defined(__aarch64__)
extern void dm_gate(void);
static dm_image_mapping program;
static dm_gate_site sites[3];
static dm_gate_table gate_table;
static void word(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static void prepare(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image image={.header.image_size=page};
    image.header.segments[0].size=256;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);
    const unsigned svcs[3]={0x29u,0x08u,0x0fu};
    for(unsigned i=0;i<3;i++){
        unsigned off=i*32;word(image.bytes+off,0xa9bf7bfdu);word(image.bytes+off+4,0xd4000001u|(svcs[i]<<5));
        word(image.bytes+off+8,0xa8c17bfdu);word(image.bytes+off+12,0xd65f03c0u);
        dm_patch_request request={.instruction=0xd4000001u|(svcs[i]<<5),.original_offset=off+4,
            .stub_offset=128+32*i,.host_gate=(uintptr_t)dm_gate};
        dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));
        word(image.bytes+request.original_offset,patch.replacement);memcpy(image.bytes+request.stub_offset,patch.stub,sizeof patch.stub);sites[i]=patch.site;
    }
    CHECK(!dm_image_map(&image,&program));dm_nso_free(&image);
    for(unsigned i=0;i<3;i++)sites[i].return_address+=(uintptr_t)program.base;
    gate_table=(dm_gate_table){sites,3};
}
static returned DM_NO_UBSAN_FUNCTION query(dm_host*h,uint64_t handle,uint64_t subid){
    dm_guest_context*c=dm_guest_current();CHECK(c&&c->host==h);
    typedef returned(*function)(uint64_t,uint64_t,uint64_t,uint64_t);
    return ((function)(void*)program.base)(0,0,handle,subid);
}
static created DM_NO_UBSAN_FUNCTION create(dm_host*h,uintptr_t top,int32_t core){
    dm_guest_context*c=dm_guest_current();CHECK(c&&c->host==h);
    typedef created(*function)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
    return ((function)(void*)((uint8_t*)program.base+32))(0,(uintptr_t)program.base,0,top,44,(uint32_t)core);
}
static uint64_t DM_NO_UBSAN_FUNCTION setmask(dm_host*h,uint32_t handle,int32_t core,uint64_t mask){
    dm_guest_context*c=dm_guest_current();CHECK(c&&c->host==h);
    typedef uint64_t(*function)(uint32_t,int32_t,uint64_t);
    return ((function)(void*)((uint8_t*)program.base+64))(handle,core,mask);
}
static void close_handle(dm_host*h,uint32_t handle){uint64_t r[31]={handle};CHECK(!dm_host_svc(h,0x16,r));}
#endif

static uint8_t *heap_for(dm_host*h){
    uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));return(uint8_t*)(uintptr_t)r[1];
}
#if defined(__aarch64__)
static void check_state(dm_host*h,uint32_t handle,int32_t core,uint64_t mask){
    int32_t actual_core=-1;uint64_t actual_mask=0;
    CHECK(!dm_host_thread_virtual_core_state(h,handle,&actual_core,&actual_mask));
    CHECK(actual_core==core&&actual_mask==mask);
}
#endif

int main(void){
#if defined(__aarch64__)
    prepare();
#endif
    dm_host *a=dm_host_create(2*UNIT),*b=dm_host_create(2*UNIT),*c=dm_host_create(2*UNIT),*d=dm_host_create(2*UNIT),*e=dm_host_create(2*UNIT);
    CHECK(a&&b&&c&&d&&e);
    uint8_t *ha=heap_for(a),*hb=heap_for(b),*hc=heap_for(c),*hd=heap_for(d);
    (void)ha;(void)hb;(void)hc;(void)hd;

    CHECK(dm_host_configure_virtual_cores(a,0,0)==EINVAL);
    CHECK(dm_host_configure_virtual_cores(a,0x5,1)==EINVAL);
    CHECK(dm_host_configure_virtual_cores(a,0x5,64)==EINVAL);
    CHECK(!dm_host_configure_virtual_cores(a,0x5,2));
    CHECK(!dm_host_configure_virtual_cores(b,0x3,1));
    CHECK(!dm_host_configure_virtual_cores(d,HIGH_CORE,63));

    // A plain attached main thread freezes policy before any managed thread.
    static const dm_gate_table no_gates={0};
    CHECK(!dm_host_configure_virtual_cores(e,0x5,2));
    CHECK(!dm_guest_attach(e,&no_gates));
    CHECK(dm_host_configure_virtual_cores(e,0x7,0)==EBUSY);
    uint64_t er[31]={[1]=0,[2]=CURRENT};
    CHECK(!dm_host_svc(e,0x29,er)&&er[1]==0x5);
    CHECK(!dm_guest_detach());
    CHECK(!dm_host_configure_virtual_cores(e,0x7,0));
    CHECK(!dm_host_destroy(e));

#if defined(__aarch64__)
    CHECK(!dm_host_register_executable(a,program.base,256));
    CHECK(!dm_host_register_executable(b,program.base,256));
    CHECK(!dm_host_register_executable(c,program.base,256));
    CHECK(!dm_host_register_executable(d,program.base,256));
    CHECK(!dm_host_register_thread_stack(a,ha,STACK));
    CHECK(!dm_host_register_thread_stack(b,hb,STACK));
    CHECK(!dm_host_register_thread_stack(c,hc,STACK));
    CHECK(!dm_host_register_thread_stack(d,hd,STACK));

    CHECK(!dm_guest_attach(a,&gate_table));
    returned q=query(a,CURRENT,0);CHECK(!q.result&&q.value==0x5);
    q=query(a,0,0);CHECK(q.result==DM_SVC_INVALID_HANDLE);
    q=query(a,CURRENT,1);CHECK(q.result==DM_SVC_INVALID_COMBINATION);

    created x=create(a,(uintptr_t)ha+STACK,0);CHECK(!x.result&&x.handle);uint32_t ta=(uint32_t)x.handle;
    check_state(a,ta,0,0x5);
    CHECK(!setmask(a,ta,2,0x4));check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,2,0)==DM_SVC_INVALID_COMBINATION);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,2,0x3)==DM_SVC_INVALID_CORE);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,1,0x5)==DM_SVC_INVALID_COMBINATION);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,64,0x5)==DM_SVC_INVALID_CORE);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,2,HIGH_CORE)==DM_SVC_INVALID_CORE);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,-1,0x5)==DM_SVC_NOT_IMPLEMENTED);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,-2,0x5)==DM_SVC_NOT_IMPLEMENTED);check_state(a,ta,2,0x4);
    CHECK(setmask(a,ta,-3,0x5)==DM_SVC_NOT_IMPLEMENTED);check_state(a,ta,2,0x4);
    CHECK(setmask(a,CURRENT_THREAD,2,0x4)==DM_SVC_NOT_IMPLEMENTED);check_state(a,ta,2,0x4);

    uint64_t ev[31]={0};CHECK(!dm_host_svc(a,0x45,ev));uint32_t event_handle=(uint32_t)ev[1];
    CHECK(setmask(a,event_handle,0,0x5)==DM_SVC_INVALID_HANDLE);check_state(a,ta,2,0x4);
    close_handle(a,event_handle);close_handle(a,(uint32_t)ev[2]);
    CHECK(dm_host_configure_virtual_cores(a,0x5,2)==EBUSY);
    CHECK(!dm_guest_detach());

    // Two-host isolation: B has its own state; A keeps its stored state.
    CHECK(!dm_guest_attach(b,&gate_table));
    q=query(b,CURRENT,0);CHECK(!q.result&&q.value==0x3);
    x=create(b,(uintptr_t)hb+STACK,1);CHECK(!x.result&&x.handle);uint32_t tb=(uint32_t)x.handle;
    check_state(b,tb,1,0x3);
    CHECK(!setmask(b,tb,0,0x1));check_state(b,tb,0,0x1);
    CHECK(!dm_guest_detach());
    check_state(a,ta,2,0x4);

    close_handle(a,ta);
    int32_t stale_core=-1;uint64_t stale_mask=0;
    CHECK(dm_host_thread_virtual_core_state(a,ta,&stale_core,&stale_mask)==ESRCH);
    CHECK(!dm_guest_attach(a,&gate_table));
    CHECK(setmask(a,ta,2,0x4)==DM_SVC_INVALID_HANDLE);
    x=create(a,(uintptr_t)ha+STACK,2);CHECK(!x.result&&x.handle);uint32_t ta2=(uint32_t)x.handle;
    check_state(a,ta2,2,0x5);
    close_handle(a,ta2);
    x=create(a,(uintptr_t)ha+STACK,1);CHECK(x.result==DM_SVC_INVALID_CORE);
    x=create(a,(uintptr_t)ha+STACK,3);CHECK(x.result==DM_SVC_INVALID_CORE);
    x=create(a,(uintptr_t)ha+STACK,63);CHECK(x.result==DM_SVC_INVALID_CORE);
    x=create(a,(uintptr_t)ha+STACK,-1);CHECK(x.result==DM_SVC_INVALID_CORE);
    x=create(a,(uintptr_t)ha+STACK,-3);CHECK(x.result==DM_SVC_INVALID_CORE);
    x=create(a,(uintptr_t)ha+STACK,-2);CHECK(!x.result&&x.handle);close_handle(a,(uint32_t)x.handle);
    CHECK(!dm_guest_detach());
    close_handle(b,tb);

    // Unconfigured host: SVC0x0f stays unsupported; previous CreateThread behavior remains.
    CHECK(!dm_guest_attach(c,&gate_table));
    q=query(c,CURRENT,0);CHECK(q.result==DM_SVC_NOT_IMPLEMENTED);
    x=create(c,(uintptr_t)hc+STACK,-2);CHECK(!x.result&&x.handle);uint32_t tc=(uint32_t)x.handle;
    CHECK(dm_host_thread_virtual_core_state(c,tc,&stale_core,&stale_mask)==ENOTSUP);
    CHECK(setmask(c,tc,0,0x2)==DM_SVC_NOT_IMPLEMENTED);
    close_handle(c,tc);CHECK(!dm_guest_detach());

    CHECK(!dm_guest_attach(d,&gate_table));
    q=query(d,CURRENT,0);CHECK(!q.result&&q.value==HIGH_CORE);
    x=create(d,(uintptr_t)hd+STACK,63);CHECK(!x.result&&x.handle);uint32_t td=(uint32_t)x.handle;
    check_state(d,td,63,HIGH_CORE);
    CHECK(!setmask(d,td,63,HIGH_CORE));check_state(d,td,63,HIGH_CORE);
    CHECK(setmask(d,td,62,HIGH_CORE)==DM_SVC_INVALID_COMBINATION);check_state(d,td,63,HIGH_CORE);
    close_handle(d,td);CHECK(!dm_guest_detach());

    CHECK(!dm_host_unregister_thread_stack(a,ha+STACK));
    CHECK(!dm_host_unregister_thread_stack(b,hb+STACK));
    CHECK(!dm_host_unregister_thread_stack(c,hc+STACK));
    CHECK(!dm_host_unregister_thread_stack(d,hd+STACK));
    CHECK(!dm_image_unmap(&program));
    puts("PASS: configured GetInfo0/CreateThread/SVC0x0f virtual state, rollback/stale/wrong-kind, high-bit, two-host");
#else
    uint64_t r[31]={[1]=0,[2]=CURRENT};
    CHECK(!dm_host_svc(a,0x29,r)&&r[1]==0x5);
    r[1]=0;r[2]=0;CHECK(dm_host_svc(a,0x29,r)==DM_SVC_INVALID_HANDLE);
    r[2]=CURRENT;r[3]=1;CHECK(dm_host_svc(a,0x29,r)==DM_SVC_INVALID_COMBINATION);
    r[1]=0;r[2]=CURRENT;r[3]=0;CHECK(dm_host_svc(c,0x29,r)==DM_SVC_NOT_IMPLEMENTED);
    puts("PASS: configured GetInfo0 and unconfigured unsupported (non-ARM64 direct ABI)");
#endif
    CHECK(!dm_host_destroy(a)&&!dm_host_destroy(b)&&!dm_host_destroy(c)&&!dm_host_destroy(d));
    return 0;
}
