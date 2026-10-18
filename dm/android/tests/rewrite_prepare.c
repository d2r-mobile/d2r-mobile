// 完整合成模块准备与链接测试；依据 MOD0 / RELA 和原生门 ABI，不含游戏数据，不执行初始化器。
// Complete synthetic module preparation / linking test; based on MOD0 / RELA and native gate ABI, no game data or initializer execution.
#include "../rewrite/prepare.h"
#include "../module_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
static void put32(uint8_t*p,uint32_t v){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(v>>(8*b));}
static void put64(uint8_t*p,uint64_t v){for(unsigned b=0;b<8;b++)p[b]=(uint8_t)(v>>(8*b));}
static uint64_t get64(const uint8_t*p){uint64_t v=0;for(unsigned b=0;b<8;b++)v|=(uint64_t)p[b]<<(8*b);return v;}
static uint32_t branch(uint32_t from,uint32_t to){return 0x14000000u|((uint32_t)(((int64_t)to-from)/4)&0x03ffffffu);}
#if defined(__aarch64__)
static void expected_trap(int sig){(void)sig;_exit(77);}
#endif
static void program(dm_nso_image*im,uint32_t off,const uint32_t*code,size_t count){
    for(size_t i=0;i<count;i++)put32(im->bytes+off+4*i,code[i]);
}
static dm_nso_image fixture(int second){
    dm_nso_image im={.header={.image_size=0x10000,.bss_size=0x4000}};
    im.header.segments[0].size=0x1000;
    im.header.segments[1]=(dm_nso_segment){.memory_offset=0x4000,.size=0x4000};
    im.header.segments[2]=(dm_nso_segment){.memory_offset=0x8000,.size=0x4000};
    im.bytes=calloc(1,im.header.image_size);CHECK(im.bytes);
    for(unsigned off=0;off<0x1000;off+=4)put32(im.bytes+off,0xd503201f);
    put32(im.bytes,branch(0,0x40));put32(im.bytes+4,0x4000);
    memcpy(im.bytes+0x4000,"MOD0",4);put32(im.bytes+0x4004,0x4000);
    const uint64_t tags[][2]={{4,0x4040},{5,0x4200},{6,0x4100},{10,14},{11,24},{7,0x4300},{8,24},{9,24},{0,0}};
    for(size_t i=0;i<sizeof(tags)/sizeof(*tags);i++){put64(im.bytes+0x8000+16*i,tags[i][0]);put64(im.bytes+0x8008+16*i,tags[i][1]);}
    put32(im.bytes+0x4040,1);put32(im.bytes+0x4044,3);put32(im.bytes+0x4048,1);put32(im.bytes+0x4050,2);
    memcpy(im.bytes+0x4200,"\0first\0second\0",14);
    uint8_t*s=im.bytes+0x4118;put32(s,second?7:1);s[4]=0x12;s[6]=1;put64(s+8,0x40);put64(s+16,32);
    s+=24;put32(s,second?1:7);s[4]=0x12;
    put64(im.bytes+0x4300,0x8300);put64(im.bytes+0x4308,(2ull<<32)|257);
    const uint32_t caller[]={0xa9bf7bfd,0x90000043,0x910c0063,0xf9400070,0xd63f0200,0xa8c17bfd,0xd65f03c0};
    const uint32_t callee[]={0xa9bf7bfd,0xd28000f2,0x91000652,0xf9000012,0xd53bd041,0xf9000401,0xaa1203e0,0xa8c17bfd,0xd65f03c0};
    if(!second){program(&im,0x40,caller,sizeof(caller)/4);return im;}
    program(&im,0x40,callee,sizeof(callee)/4);
    const uint32_t address[]={0xa9bf7bfd,0x100000f2,0xb4000052,0xaa1203e0,0xa8c17bfd,0xd65f03c0};
    program(&im,0x100,address,sizeof(address)/4);
    const uint32_t conditional[]={0xa9bf7bfd,0xaa0003f2,0xb4000072,0xd2800020,0x14000002,0xd2800040,0xa8c17bfd,0xd65f03c0};
    program(&im,0x140,conditional,sizeof(conditional)/4);
    const uint32_t atomic[]={0xa9bf7bfd,0xd2800032,0xc85f7c02,0x8b020252,0xc8037c12,0x35ffffa3,0xaa1203e0,0xa8c17bfd,0xd65f03c0};
    program(&im,0x180,atomic,sizeof(atomic)/4);
    const uint32_t jump[]={0xa9bf7bfd,0x10000ff2,0x39400002,0x8b020a52,0xd61f0240,0xa8c17bfd,0xd65f03c0};
    program(&im,0x200,jump,sizeof(jump)/4);
    for(unsigned i=0;i<256;i++)put32(im.bytes+0x400+4*i,branch(0x400+4*i,0x800));
    put32(im.bytes+0x800,0xd2800540);put32(im.bytes+0x804,branch(0x804,0x214));
    const uint32_t cache[]={0xa9bf7bfd,0xd53b0020,0xa8c17bfd,0xd65f03c0};
    program(&im,0x280,cache,sizeof(cache)/4);
    put32(im.bytes+0x300,0xe7ffdefe);
    return im;
}
static void failures(dm_nso_image*image){
    dm_rewrite_report r={0};dm_rewrite_output sentinel={.count=123};
    CHECK(dm_rewrite_prepare(image,0x10000,0,&r,&sentinel)==EINVAL&&sentinel.count==123);
    CHECK(dm_rewrite_prepare(image,0x10008,1,&r,&sentinel)==0);
    dm_rewrite_free(&sentinel);sentinel.count=123;
    CHECK(dm_rewrite_prepare(image,8,1,&r,&sentinel)==EINVAL&&sentinel.count==123);
    CHECK(dm_rewrite_prepare(image,0x10001,1,&r,&sentinel)==EINVAL);
    const uint32_t blocked[]={0xffffffff,0xc85f7c02,0xd53be000,0xd61f0240};
    for(unsigned i=0;i<4;i++){
        put32(image->bytes+0xffc,blocked[i]);
        CHECK(dm_rewrite_prepare(image,0x10000,1,&r,&sentinel)==ENOTSUP);
        CHECK(sentinel.count==123&&!sentinel.image.bytes&&!sentinel.code.bytes&&!sentinel.sites);
        CHECK(i!=0||r.undecodable==1);CHECK(i!=1||r.unmatched==1);
        CHECK(i!=2||r.unsupported_system==1);CHECK(i!=3||r.branches==1);
    }
    put32(image->bytes+0xffc,0xd503201f);
    CHECK(dm_rewrite_prepare(image,0x8000040,1,&r,&sentinel)==ENOTSUP&&r.emitter_errors);
    CHECK(sentinel.count==123&&!sentinel.image.bytes);
}
static void referenced_literal_words_are_preserved(void){
    dm_nso_image im=fixture(0);dm_rewrite_report r;dm_rewrite_output out={0};
    const uint32_t pool[]={0x18000060,0xd65f03c0,0xd65f03c0,0xffffffff,0xd503201f};
    program(&im,0x900,pool,5);
    CHECK(!dm_rewrite_prepare(&im,0x10000,1,&r,&out));
    CHECK(r.literal_words==1);
    CHECK(!memcmp(out.image.bytes+0x90c,im.bytes+0x90c,4));
#if defined(__aarch64__)
    dm_image_mapping mapped={0};CHECK(!dm_image_map(&out.image,&mapped));
    CHECK(((uint32_t(*)(void))(void*)(mapped.base+0x900))()==UINT32_MAX);
    CHECK(!dm_image_unmap(&mapped));
#endif
    dm_rewrite_free(&out);
    put32(im.bytes+0x90c,0x91000652);
    CHECK(!dm_rewrite_prepare(&im,0x10000,1,&r,&out)&&r.literal_words==1&&!r.plain);
    CHECK(!memcmp(out.image.bytes+0x90c,im.bytes+0x90c,4));dm_rewrite_free(&out);
    put32(im.bytes+0x940,branch(0x940,0x90c));
    CHECK(dm_rewrite_prepare(&im,0x10000,1,&r,&out)==ENOTSUP&&r.literal_conflicts==1);
    put32(im.bytes+0x90c,UINT32_MAX);
    put32(im.bytes+0x940,0x10fffe60);
    CHECK(dm_rewrite_prepare(&im,0x10000,1,&r,&out)==ENOTSUP);
    CHECK(r.literal_conflicts==1);
    put32(im.bytes+0x940,branch(0x940,0x90c));
    CHECK(dm_rewrite_prepare(&im,0x10000,1,&r,&out)==ENOTSUP);
    put32(im.bytes+0x940,0xd503201f);
    put32(im.bytes+0x900,0x58000060);
    CHECK(dm_rewrite_prepare(&im,0x10000,1,&r,&out)==ENOTSUP);
    put32(im.bytes+0x900,0xd503201f);
    put32(im.bytes+0x90c,0xd503201f);
    const uint32_t relative[]={0x94000002,0xffffffff,0xb94003c0,0x93407c00,0x8b1e001d};
    program(&im,0xa00,relative,5);put32(im.bytes+0xa04,0x8200-0xa04);
    CHECK(!dm_rewrite_prepare(&im,0x10000,1,&r,&out)&&r.literal_words==1);dm_rewrite_free(&out);
    put32(im.bytes+0xa00,0x94000003);
    int status=dm_rewrite_prepare(&im,0x10000,1,&r,&out);
    CHECK((!status||status==ENOTSUP)&&!r.literal_words);dm_rewrite_free(&out);
    put32(im.bytes+0xa00,0x94000002);
    put32(im.bytes+0xa08,0xf94003c0);
    status=dm_rewrite_prepare(&im,0x10000,1,&r,&out);
    CHECK((!status||status==ENOTSUP)&&!r.literal_words);dm_rewrite_free(&out);
    put32(im.bytes+0xa08,0xb94003c0);
    put32(im.bytes+0xa04,0xffffffff);
    CHECK(dm_rewrite_prepare(&im,0x10000,1,&r,&out)==ENOTSUP);
    put32(im.bytes+0xa04,0x8200-0xa04);put32(im.bytes+0xa0c,0x93407c20);
    status=dm_rewrite_prepare(&im,0x10000,1,&r,&out);
    CHECK((!status||status==ENOTSUP)&&r.literal_words==0);dm_rewrite_free(&out);
    dm_nso_free(&im);
}
int main(void){
    referenced_literal_words_are_preserved();
    dm_nso_image images[2]={fixture(0),fixture(1)};failures(&images[1]);
    dm_nso_image metadata=fixture(0);put32(metadata.bytes+4,0x900);memcpy(metadata.bytes+0x900,metadata.bytes+0x4000,28);
    put32(metadata.bytes+0x904,0x8000-0x900);for(unsigned i=2;i<7;i++)put32(metadata.bytes+0x900+4*i,UINT32_MAX);
    dm_rewrite_report classified;dm_rewrite_output metadata_output={0};
    CHECK(!dm_rewrite_prepare(&metadata,0x10000,1,&classified,&metadata_output)&&classified.metadata_words==7);
    dm_rewrite_free(&metadata_output);put32(metadata.bytes,branch(0,0x908));
    CHECK(dm_rewrite_prepare(&metadata,0x10000,1,&classified,&metadata_output)==ENOTSUP&&classified.metadata_entries==1);
    CHECK(!metadata_output.image.bytes);dm_nso_free(&metadata);
    uintptr_t gate=1;
#if defined(__aarch64__)
    extern void dm_gate(void);gate=(uintptr_t)dm_gate;
#endif
    dm_rewrite_output prepared[2]={0};dm_rewrite_report reports[2];
    for(unsigned i=0;i<2;i++){
        dm_rewrite_report audit;
        CHECK(!dm_rewrite_prepare(&images[i],0x10000,gate,&audit,NULL));
        CHECK(!dm_rewrite_prepare(&images[i],0x10000,gate,&reports[i],&prepared[i]));
        CHECK(!memcmp(&audit,&reports[i],sizeof(audit)));
        CHECK(!memcmp(images[i].bytes+0x4000,prepared[i].image.bytes+0x4000,0xc000));
        CHECK(prepared[i].count==reports[i].sites&&prepared[i].code.size==reports[i].stub_bytes);
    }
    CHECK(reports[1].systems==1&&reports[1].jumps==1&&reports[1].rewritten_windows==1);
    CHECK(reports[1].addresses==2&&reports[1].conditionals==2&&reports[1].rewritten_sites==2);
    CHECK(reports[1].retained_traps==1);
    CHECK(!prepared[0].count&&!prepared[0].code.bytes&&!prepared[0].code.offset);
    for(unsigned i=0;i<2;i++){
        uint8_t*bytes=NULL;size_t size=0;dm_rewrite_output decoded={0};
        CHECK(!dm_artifact_encode(&prepared[i],&bytes,&size));CHECK(!dm_artifact_decode(bytes,size,&decoded));
        free(bytes);dm_rewrite_free(&prepared[i]);prepared[i]=decoded;
    }
    dm_artifact_mappings mapped={0};CHECK(!dm_artifacts_map(prepared,2,gate,&mapped));
    CHECK(mapped.modules.relocations==2&&get64(mapped.modules.mappings[0].base+0x8300)==(uintptr_t)mapped.modules.mappings[1].base+0x40);
    CHECK(!get64(images[0].bytes+0x8300)&&!get64(prepared[0].image.bytes+0x8300));
#if defined(__aarch64__)
    dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);CHECK(!dm_guest_attach(host,&mapped.gates));
    uint64_t values[2]={0};typedef uint64_t(*fn)(void*);
    CHECK(((fn)(void*)(mapped.modules.mappings[0].base+0x40))(values)==8);
    CHECK(values[0]==8&&values[1]==dm_guest_current()->slots[DM_SLOT_TLS]);
    CHECK(((fn)(void*)(mapped.modules.mappings[1].base+0x100))(NULL)==(uintptr_t)mapped.modules.mappings[1].base+0x120);
    CHECK(((fn)(void*)(mapped.modules.mappings[1].base+0x140))(NULL)==2);
    CHECK(((fn)(void*)(mapped.modules.mappings[1].base+0x140))((void*)1)==1);
    uint64_t counter=1;CHECK(((fn)(void*)(mapped.modules.mappings[1].base+0x180))(&counter)==2&&counter==2);
    uint8_t index=255;CHECK(((fn)(void*)(mapped.modules.mappings[1].base+0x200))(&index)==42);
    CHECK((uint32_t)get64(prepared[1].image.bytes+0x284)==0xd53b0020);
    CHECK((uint32_t)get64(prepared[1].image.bytes+0x300)==0xe7ffdefe);
    pid_t child=fork();CHECK(child>=0);
    if(!child){signal(SIGILL,expected_trap);signal(SIGTRAP,expected_trap);((fn)(void*)(mapped.modules.mappings[1].base+0x300))(NULL);_exit(0);}
    int trap_status;CHECK(waitpid(child,&trap_status,0)==child);
    if(!WIFEXITED(trap_status)||WEXITSTATUS(trap_status)!=77)fprintf(stderr,"retained trap child status=%#x\n",trap_status);
    CHECK(WIFEXITED(trap_status)&&WEXITSTATUS(trap_status)==77);
#if !defined(__APPLE__)
    uint64_t cache_type;__asm__ volatile("mrs %0, ctr_el0":"=r"(cache_type));
    CHECK(((fn)(void*)(mapped.modules.mappings[1].base+0x280))(NULL)==cache_type);
#endif
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));
#endif
    CHECK(!dm_artifacts_unmap(&mapped));
    for(unsigned i=0;i<2;i++){dm_rewrite_free(&prepared[i]);dm_rewrite_free(&prepared[i]);dm_nso_free(&images[i]);}
    puts("PASS: shared audit/preparation, transactional rejection, native system/x18/address/branch/window/table rewrites, cross-module relocation and gate return");
    return 0;
}
