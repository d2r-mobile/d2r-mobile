// 独立 RX 跳板区测试；依据 image_map / 原生门 ABI，以合成 ARM64 程序验证装载、返回和页面隔离。
// Separate RX stub-region test; based on image_map / native gate ABI, verify loading, return and page isolation with synthetic ARM64.
#include "../image_map.h"
#include "../runtime/patch.h"
#include "../runtime/x18_patch.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); abort(); } } while (0)
static void fault(int sig) { (void)sig; _exit(77); }
static void inaccessible(uint8_t *p, int operation) {
    pid_t child=fork(); CHECK(child>=0);
    if(!child){
        signal(SIGBUS,fault);signal(SIGSEGV,fault);
        if(operation==0){volatile uint8_t value=*(volatile uint8_t*)p;(void)value;}
        else if(operation==1)*(volatile uint8_t*)p=0;
        else ((void(*)(void))(void*)p)();
        _exit(0);
    }
    int status;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==77);
}
static void put(uint8_t*p,uint32_t word){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(word>>(8*b));}

int main(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);CHECK(page>0&&page<DM_NSO_MAX_IMAGE/8);
    dm_nso_image image={.header={.image_size=2*page+46,.bss_size=33}};
    image.header.segments[0].size=36;
    image.header.segments[1]=(dm_nso_segment){.memory_offset=(uint32_t)page,.size=16};
    image.header.segments[2]=(dm_nso_segment){.memory_offset=(uint32_t)(2*page),.size=13};
    image.bytes=calloc(1,image.header.image_size);CHECK(image.bytes);image.bytes[page]=17;
    uint8_t stubs[160]={0};
    dm_image_code code={.offset=4*page,.size=sizeof(stubs)};
    dm_image_mapping mapping={0},sentinel={.base=(void*)1,.size=42};
    dm_image_code bad=code;bad.offset=2*page;
    CHECK(dm_image_reserve_with_code(&image,&bad,&sentinel)==DM_IMAGE_LAYOUT);
    CHECK(sentinel.base==(void*)1&&sentinel.size==42);
    bad.offset=4*page+1;CHECK(dm_image_reserve_with_code(&image,&bad,&sentinel)==DM_IMAGE_LAYOUT);
    bad=code;bad.size=0;CHECK(dm_image_reserve_with_code(&image,&bad,&sentinel)==DM_IMAGE_LAYOUT);
    bad=code;bad.size=SIZE_MAX;CHECK(dm_image_reserve_with_code(&image,&bad,&sentinel)==DM_IMAGE_LAYOUT);
    bad=code;bad.offset=SIZE_MAX;CHECK(dm_image_reserve_with_code(&image,&bad,&sentinel)==DM_IMAGE_LAYOUT);
    CHECK(dm_image_reserve_with_code(&image,&code,&mapping)==DM_IMAGE_OK);
    CHECK(mapping.size==5*page);
    inaccessible(mapping.base+code.offset,0);
    CHECK(dm_image_commit_with_code(&image,&code,&mapping)==DM_IMAGE_LAYOUT);
    inaccessible(mapping.base+code.offset,0);
    code.bytes=stubs;
    bad=code;bad.size=page+1;
    CHECK(dm_image_commit_with_code(&image,&bad,&mapping)==DM_IMAGE_LAYOUT);
    CHECK(dm_image_commit(&image,&mapping)==DM_IMAGE_LAYOUT);
    inaccessible(mapping.base,0);

#if defined(__aarch64__)
    extern void dm_gate(void);
    const uint32_t program[]={0xa9bf7bfd,0xd28000f2,0xaa1203e0,0xd53bd041,
        0xf9000040,0xf9000441,0xa8c17bfd,0xd65f03c0};
    for(unsigned i=0;i<8;i++)put(image.bytes+4*i,program[i]);
    dm_gate_site sites[3];
    for(unsigned i=0;i<2;i++){
        dm_x18_request request={.original_offset=4+4*i,.stub_offset=(uint32_t)code.offset+64*i,
            .translated=i?0xaa0c03e0:0xd28000ec,.scratch={12,13,14},.host_gate=(uintptr_t)dm_gate};
        dm_x18_patch patch;CHECK(!dm_patch_x18(&request,&patch));
        put(image.bytes+request.original_offset,patch.replacement);memcpy(stubs+64*i,patch.stub,64);sites[i]=patch.site;
    }
    dm_patch_request request={.instruction=program[3],.original_offset=12,
        .stub_offset=(uint32_t)code.offset+128,.host_gate=(uintptr_t)dm_gate};
    dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));
    put(image.bytes+12,patch.replacement);memcpy(stubs+128,patch.stub,32);sites[2]=patch.site;
#else
    put(image.bytes,0);
#endif
    CHECK(dm_image_commit_with_code(&image,&code,&mapping)==DM_IMAGE_OK);
    CHECK(mapping.base[page]==17&&mapping.base[2*page+45]==0);
    CHECK(!memcmp(mapping.base+code.offset,stubs,sizeof(stubs)));
    CHECK(mapping.base[code.offset+sizeof(stubs)]==0);
    inaccessible(mapping.base,1);inaccessible(mapping.base+code.offset,1);
    inaccessible(mapping.base+page,1);inaccessible(mapping.base+3*page,0);
    put(mapping.base+2*page,0xd65f03c0);
#if defined(__aarch64__)
    inaccessible(mapping.base+2*page,2);
    for(unsigned i=0;i<3;i++)sites[i].return_address+=(uintptr_t)mapping.base;
    dm_gate_table table={.sites=sites,.count=3};
    dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);CHECK(!dm_guest_attach(host,&table));
    uint64_t values[2]={0};uintptr_t before,after;
    __asm__ volatile("mov %0, x18":"=r"(before));
    typedef uint64_t(*entry_fn)(uint64_t,uint64_t,uint64_t*);
    CHECK(((entry_fn)(void*)mapping.base)(0,0,values)==7);
    __asm__ volatile("mov %0, x18":"=r"(after));
    CHECK(before==after&&values[0]==7&&values[1]==dm_guest_current()->slots[DM_SLOT_TLS]);
    CHECK(dm_guest_current()->slots[DM_SLOT_X18]==7);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));
#endif
    CHECK(!dm_image_unmap(&mapping));CHECK(!mapping.base&&!mapping.size);
    dm_nso_free(&image);
    puts("PASS: separate RX stubs, reserve/commit validation, gaps, RO/RW/NX isolation and native x18/TLS gate return");
    return 0;
}
