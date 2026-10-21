// 原生跳转表与独占窗口测试；合成程序覆盖索引边界、CAS 成败、重试与多线程争用，不包含游戏内容。
// Native jump-table / exclusive-window tests; synthetic programs cover index bounds, CAS success/failure, retries and contention, without game content.
#include "../runtime/x18_jump.h"
#include "../runtime/x18_window.h"
#include "../image_map.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "generated_entry.h"
#ifdef DM_TEST_REGIONS
#include "../rewrite/jump_table.h"
#include "../rewrite/window_decode.h"
#endif
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#if defined(__aarch64__)
extern void dm_gate(void);
static void put(uint8_t*p,uint32_t w){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(w>>(8*b));}
static uint32_t branch(uint32_t from,uint32_t to){return 0x14000000u|((uint32_t)(((int64_t)to-from)/4)&0x03ffffffu);}
static dm_nso_image image_create(size_t size){
    dm_nso_image im={.header.image_size=size};im.header.segments[0].size=(uint32_t)size;
    im.header.segments[1].memory_offset=im.header.segments[2].memory_offset=(uint32_t)size;
    im.bytes=calloc(1,size);CHECK(im.bytes);return im;
}
#ifdef DM_TEST_REGIONS
static void verify_window(dm_window_request*request){
    dm_nso_image im=image_create(4096);
    for(unsigned i=0;i<request->count;i++)put(im.bytes+request->original_offset+4*i,request->original[i]);
    dm_window_source source={&im,request->original_offset,request->count};dm_window_request decoded;
    CHECK(!dm_decode_window(&source,&decoded));decoded.stub_offset=request->stub_offset;decoded.host_gate=request->host_gate;
    put(im.bytes,branch(0,request->original_offset+4));dm_window_request sentinel={.count=123};
    CHECK(dm_decode_window(&source,&sentinel)==ENOTSUP&&sentinel.count==123);
    put(im.bytes,branch(0,request->original_offset));CHECK(!dm_decode_window(&source,&sentinel));
    put(im.bytes,0);put(im.bytes+request->original_offset+4,0x910003f2);
    CHECK(dm_decode_window(&source,&sentinel)==ENOTSUP);
    dm_nso_free(&im);*request=decoded;
}
static void verify_jump(dm_jump_request*request){
    dm_nso_image im=image_create((size_t)request->table_offset+4u*request->entries);
    uint32_t delta=request->table_offset-20,imm=delta&0x1fffffu;
    put(im.bytes+20,0x10000012u|(imm&3)<<29|(imm>>2)<<5);
    put(im.bytes+24,request->entries==256?0x38706a20u:0x78707a20u);
    put(im.bytes+28,0x8b000a52);put(im.bytes+32,0xd61f0240);
    dm_jump_table table;CHECK(!dm_recognize_jump(&im,32,&table));
    CHECK(table.table_offset==request->table_offset&&table.entries==request->entries);
    put(im.bytes+28,0xd2800032);CHECK(dm_recognize_jump(&im,32,&table)==ENOTSUP);
    delta=request->table_offset-16;imm=delta&0x1fffffu;
    put(im.bytes+16,0x10000012u|(imm&3)<<29|(imm>>2)<<5);
    put(im.bytes+20,request->entries==256?0x38706a20u:0x78707a20u);
    put(im.bytes+24,0x52800020);put(im.bytes+28,0x8b000a52);
    CHECK(dm_recognize_jump(&im,32,&table)==ENOTSUP);
    dm_nso_free(&im);
}
#endif
static void trap_handler(int sig){(void)sig;_exit(77);}
static void DM_NO_UBSAN_FUNCTION jump_cases(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    for(unsigned variant=0;variant<2;variant++){
        uint32_t count=variant?65536:256,table=(uint32_t)page,tails=table+4*count;
        uint32_t stub=(tails+8*count+(uint32_t)page-1)&~((uint32_t)page-1);
        dm_jump_request request={.original_offset=8,.stub_offset=stub,.table_offset=table,.entries=count,.host_gate=(uintptr_t)dm_gate};
#ifdef DM_TEST_REGIONS
        verify_jump(&request);
#endif
        dm_jump_patch patch={0};CHECK(!dm_patch_x18_jump(&request,&patch));
        dm_nso_image im=image_create(stub+patch.size);
        put(im.bytes,0xa9bf7bfd);put(im.bytes+4,0xeb1f03ff);put(im.bytes+8,patch.replacement);
        put(im.bytes+16,0xa8c17bfd);put(im.bytes+20,0xd65f03c0);
        for(uint32_t i=0;i<count;i++){put(im.bytes+table+4*i,branch(table+4*i,tails+8*i));
            put(im.bytes+tails+8*i,0xd2800000u|i<<5);put(im.bytes+tails+8*i+4,branch(tails+8*i+4,16));}
        memcpy(im.bytes+stub,patch.stub,patch.size);dm_image_mapping mapping={0};CHECK(dm_image_map(&im,&mapping)==DM_IMAGE_OK);
        dm_gate_site site=patch.site;site.return_address+=(uintptr_t)mapping.base;dm_gate_table gates={&site,1};
        dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);CHECK(!dm_guest_attach(host,&gates));
        uint32_t indices[]={0,1,127,255,256,32768,65535};
        for(unsigned i=0;i<7;i++){if(indices[i]>=count)continue;uint64_t target=(uintptr_t)mapping.base+table+4*indices[i];
            dm_guest_current()->slots[DM_SLOT_X18]=target;typedef uint64_t(*fn)(void);
            uint64_t value=((fn)(void*)mapping.base)();uint64_t flags;__asm__ volatile("mrs %0,nzcv":"=r"(flags));
            CHECK(value==indices[i]&&(flags>>28)==6&&dm_guest_current()->slots[DM_SLOT_X18]==target);}
        uint64_t invalid[]={table-4,table+2,(uint64_t)table+4*count};
        for(unsigned i=0;i<3;i++){pid_t child=fork();CHECK(child>=0);if(!child){signal(SIGTRAP,trap_handler);
            dm_guest_current()->slots[DM_SLOT_X18]=(uintptr_t)mapping.base+invalid[i];
            typedef void(*fn)(void);((fn)(void*)mapping.base)();_exit(1);}
            int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);}
        CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));CHECK(!dm_image_unmap(&mapping));dm_nso_free(&im);dm_jump_patch_free(&patch);
    }
}
typedef struct{dm_host*host;const dm_gate_table*gates;void*entry;uint64_t*counter;} worker;
static void* DM_NO_UBSAN_FUNCTION increment_worker(void*opaque){worker*w=opaque;CHECK(!dm_guest_attach(w->host,w->gates));
    typedef void(*fn)(uint64_t*);for(unsigned i=0;i<1000;i++)((fn)w->entry)(w->counter);
    CHECK(!dm_guest_detach());return NULL;}
static void DM_NO_UBSAN_FUNCTION window_increment(void){
    dm_window_request request={.original_offset=4,.stub_offset=128,.count=4,.scratch={2,3,4},.host_gate=(uintptr_t)dm_gate,
        .original={0xc85ffc12,0x91000652,0xc801fc12,0x35ffffa1},
        .translated={0xc85ffc02,0x91000442,0xc801fc02,0x35ffffa1}};
#ifdef DM_TEST_REGIONS
    verify_window(&request);
#endif
    dm_window_patch patch;CHECK(!dm_patch_x18_window(&request,&patch));
    dm_nso_image im=image_create(4096);put(im.bytes,0xa9bf7bfd);put(im.bytes+4,patch.replacement);
    put(im.bytes+20,0xa8c17bfd);put(im.bytes+24,0xd65f03c0);memcpy(im.bytes+128,patch.stub,patch.size);
    dm_image_mapping mapping={0};CHECK(dm_image_map(&im,&mapping)==DM_IMAGE_OK);
    dm_gate_site site=patch.site;site.return_address+=(uintptr_t)mapping.base;dm_gate_table gates={&site,1};
    dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);uint64_t counter=0;
    worker w={host,&gates,mapping.base,&counter};pthread_t threads[4];
    for(unsigned i=0;i<4;i++)CHECK(!pthread_create(&threads[i],NULL,increment_worker,&w));
    for(unsigned i=0;i<4;i++)CHECK(!pthread_join(threads[i],NULL));
    CHECK(counter==4000);CHECK(!dm_host_destroy(host));CHECK(!dm_image_unmap(&mapping));dm_nso_free(&im);
}
static void DM_NO_UBSAN_FUNCTION window_compare(void){
    dm_window_request request={.original_offset=4,.stub_offset=128,.count=5,.scratch={5,6,7},.host_gate=(uintptr_t)dm_gate,
        .original={0xc87f8c12,0xeb01025f,0x540000c1,0xc8248c02,0x35ffff84},
        .translated={0xc87f8c05,0xeb0100bf,0x540000c1,0xc8248c02,0x35ffff84}};
#ifdef DM_TEST_REGIONS
    verify_window(&request);
#endif
    dm_window_patch patch;CHECK(!dm_patch_x18_window(&request,&patch));dm_nso_image im=image_create(4096);
    put(im.bytes,0xa9bf7bfd);put(im.bytes+4,patch.replacement);put(im.bytes+24,0xd2800020);
    put(im.bytes+28,0xa8c17bfd);put(im.bytes+32,0xd65f03c0);put(im.bytes+36,0xd5033f5f);
    put(im.bytes+40,0xd2800000);put(im.bytes+44,0xa8c17bfd);put(im.bytes+48,0xd65f03c0);
    memcpy(im.bytes+128,patch.stub,patch.size);dm_image_mapping mapping={0};CHECK(dm_image_map(&im,&mapping)==DM_IMAGE_OK);
    dm_gate_site site=patch.site;site.return_address+=(uintptr_t)mapping.base;dm_gate_table gates={&site,1};
    dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);CHECK(!dm_guest_attach(host,&gates));
    alignas(16)uint64_t pair[2]={10,20};typedef uint64_t(*fn)(uint64_t*,uint64_t,uint64_t);
    CHECK(((fn)(void*)mapping.base)(pair,9,30)==0&&pair[0]==10&&pair[1]==20);
    CHECK(((fn)(void*)mapping.base)(pair,10,30)==1&&pair[0]==30&&pair[1]==20);
    CHECK(dm_guest_current()->slots[DM_SLOT_X18]==10);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));CHECK(!dm_image_unmap(&mapping));dm_nso_free(&im);
}
static void DM_NO_UBSAN_FUNCTION window_forced_retry(void){
    dm_window_request request={.original_offset=8,.stub_offset=128,.count=8,.scratch={4,5,6},.host_gate=(uintptr_t)dm_gate,
        .original={0xc85ffc12,0x91000463,0x34000061,0x52800001,0xd5033f5f,0x91000652,0xc802fc12,0x35ffff22},
        .translated={0xc85ffc04,0x91000463,0x34000061,0x52800001,0xd5033f5f,0x91000484,0xc802fc04,0x35ffff22}};
#ifdef DM_TEST_REGIONS
    verify_window(&request);
#endif
    dm_window_patch patch;CHECK(!dm_patch_x18_window(&request,&patch));dm_nso_image im=image_create(4096);
    put(im.bytes,0xa9bf7bfd);put(im.bytes+4,0xd2800003);put(im.bytes+8,patch.replacement);put(im.bytes+40,0xaa0303e0);
    put(im.bytes+44,0xa8c17bfd);put(im.bytes+48,0xd65f03c0);memcpy(im.bytes+128,patch.stub,patch.size);
    dm_image_mapping mapping={0};CHECK(dm_image_map(&im,&mapping)==DM_IMAGE_OK);dm_gate_site site=patch.site;
    site.return_address+=(uintptr_t)mapping.base;dm_gate_table gates={&site,1};dm_host*host=dm_host_create(4u*1024u*1024u);
    CHECK(host);CHECK(!dm_guest_attach(host,&gates));uint64_t counter=0;typedef uint64_t(*fn)(uint64_t*,uint64_t);
    CHECK(((fn)(void*)mapping.base)(&counter,1)==2&&counter==1&&dm_guest_current()->slots[DM_SLOT_X18]==1);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));CHECK(!dm_image_unmap(&mapping));dm_nso_free(&im);
}
#endif
int main(void){
    dm_jump_request jump={.entries=7,.host_gate=1};dm_jump_patch jp={0};CHECK(dm_patch_x18_jump(&jump,&jp)==EINVAL&&!jp.stub);
    dm_window_request window={.count=18,.host_gate=1};dm_window_patch wp={.replacement=123};
    CHECK(dm_patch_x18_window(&window,&wp)==EINVAL&&wp.replacement==123);
#if defined(__aarch64__)
    jump_cases();window_increment();window_compare();window_forced_retry();
#endif
    puts("PASS: byte/halfword jump-table bounds and landing restoration; exclusive retry/contention and paired CAS success/failure exits");return 0;
}
