// 可移植产物格式与宿主重绑定回归；依据 artifact.h，使用合成 MOD0 与 TLS 跳板，不包含游戏数据。
// Portable artifact format / host rebinding regressions; based on artifact.h, use synthetic MOD0 and TLS stubs without game data.
#include "../artifact.h"
#include "../runtime/patch.h"
#include <errno.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t*p,uint64_t v){put(p,(uint32_t)v);put(p+4,(uint32_t)(v>>32));}
static dm_rewrite_output fixture(void){
    dm_rewrite_output a={.image.header={.image_size=0xc000},.code={.offset=0xc000,.size=32},.count=1};
    a.image.header.segments[0].size=64;
    a.image.header.segments[1]=(dm_nso_segment){.memory_offset=0x4000,.size=0x1000};
    a.image.header.segments[2]=(dm_nso_segment){.memory_offset=0x8000,.size=0x1000};
    a.image.bytes=calloc(1,0xc000);a.code.bytes=calloc(1,32);a.sites=calloc(1,sizeof(*a.sites));a.gate_literals=calloc(1,4);
    CHECK(a.image.bytes&&a.code.bytes&&a.sites&&a.gate_literals);
    put(a.image.bytes+4,0x4000);memcpy(a.image.bytes+0x4000,"MOD0",4);put(a.image.bytes+0x4004,0x4000);
    const uint64_t tags[][2]={{4,0x4100},{5,0x4300},{6,0x4200},{10,1},{11,24},{0,0}};
    for(unsigned i=0;i<6;i++){put64(a.image.bytes+0x8000+16*i,tags[i][0]);put64(a.image.bytes+0x8008+16*i,tags[i][1]);}
    put(a.image.bytes+0x4100,1);put(a.image.bytes+0x4104,1);
    put(a.image.bytes+0x20,0xa9bf7bfd);put(a.image.bytes+0x28,0xa8c17bfd);put(a.image.bytes+0x2c,0xd65f03c0);
    dm_patch_request req={.instruction=0xd53bd040,.original_offset=0x24,.stub_offset=0xc000,.host_gate=0x123456789abcdef0ull};
    dm_patch_output patch;CHECK(!dm_patch_system(&req,&patch));put(a.image.bytes+0x24,patch.replacement);
    memcpy((void*)a.code.bytes,patch.stub,32);a.sites[0]=patch.site;a.gate_literals[0]=24;return a;
}
static void resign(uint8_t*p,size_t n){uint8_t h[32];memset(p+96,0,32);CHECK(!mbedtls_sha256(p,n,h,0));memcpy(p+96,h,32);}
int main(int argc,char**argv){
    dm_rewrite_output a=fixture();
    if(argc==3&&!strcmp(argv[1],"--export")){int rc=dm_artifact_write(argv[2],&a);dm_rewrite_free(&a);return rc?1:0;}
    CHECK(argc==1);uint8_t*bytes=NULL;size_t size=0;CHECK(!dm_artifact_encode(&a,&bytes,&size));
    dm_rewrite_output decoded={0};CHECK(!dm_artifact_decode(bytes,size,&decoded));
    CHECK(decoded.count==1&&decoded.gate_literals[0]==24);
    for(unsigned i=0;i<8;i++)CHECK(decoded.code.bytes[24+i]==0);
    CHECK(a.code.bytes[24]!=0);
    dm_rewrite_output sentinel={.count=123};
    CHECK(dm_artifact_decode(bytes,159,&sentinel)==EINVAL&&sentinel.count==123);
    CHECK(dm_artifact_decode(bytes,size-1,&sentinel)==EINVAL);
    bytes[160]^=1;CHECK(dm_artifact_decode(bytes,size,&sentinel)==EBADMSG);bytes[160]^=1;
    size_t record=160+0xc000+32;
    const size_t offsets[]={8,12,16,24,32,36,40,56,128,record,record+4,record+8,record+12};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++){
        uint8_t*copy=malloc(size);CHECK(copy);memcpy(copy,bytes,size);put(copy+offsets[i],UINT32_MAX);resign(copy,size);
        CHECK(dm_artifact_decode(copy,size,&sentinel)!=0&&sentinel.count==123&&!sentinel.image.bytes);free(copy);
    }
    bytes[160+0xc000+24]=1;resign(bytes,size);CHECK(dm_artifact_decode(bytes,size,&sentinel)==EINVAL);
    bytes[160+0xc000+24]=0;resign(bytes,size);
    uintptr_t gate=1;
#if defined(__aarch64__)
    extern void dm_gate(void);gate=(uintptr_t)dm_gate;
#endif
    dm_artifact_mappings m={0};CHECK(!dm_artifacts_map(&decoded,1,gate,&m));CHECK(m.gates.count==1);
#if defined(__aarch64__)
    dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);CHECK(!dm_guest_attach(host,&m.gates));
    typedef uint64_t(*fn)(void);CHECK(((fn)(void*)(m.modules.mappings[0].base+0x20))()==dm_guest_current()->slots[DM_SLOT_TLS]);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));
#endif
    CHECK(decoded.code.bytes[24]==0);CHECK(!dm_artifacts_unmap(&m));
    m.gates.count=123;put(decoded.image.bytes+4,UINT32_MAX);
    CHECK(dm_artifacts_map(&decoded,1,gate,&m)!=0&&m.gates.count==123&&!m.modules.mappings);put(decoded.image.bytes+4,0x4000);
    const char*tmp=getenv("TMPDIR");CHECK(tmp);size_t length=strlen(tmp)+64;char*path=malloc(length);CHECK(path);
    snprintf(path,length,"%s/dm-artifact.XXXXXX",tmp);CHECK(mkdtemp(path));
    size_t used=strlen(path);strcat(path,"/fixture.dma");
    CHECK(!dm_artifact_write(path,&a));CHECK(dm_artifact_write(path,&a)==EEXIST);
    dm_rewrite_output from_file={0};CHECK(!dm_artifact_read(path,&from_file));dm_rewrite_free(&from_file);
    CHECK(!unlink(path));path[used]=0;CHECK(!rmdir(path));free(path);
    free(bytes);dm_rewrite_free(&decoded);dm_rewrite_free(&a);
    puts("PASS: portable checksummed artifacts, no host-pointer persistence, hostile metadata rejection, no-clobber files, native gate rebinding and rollback");
    return 0;
}
