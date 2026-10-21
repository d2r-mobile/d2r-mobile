#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uintptr_t VkDescriptorSetLayout, VkDescriptorSet, VkBufferView;
typedef unsigned VkDescriptorType;
typedef struct { uintptr_t buffer; uint64_t offset,range; } VkDescriptorBufferInfo;
typedef struct { uintptr_t sampler,imageView; unsigned imageLayout,padding; } VkDescriptorImageInfo;
typedef struct {
    unsigned descriptorType,dstBinding,descriptorCount,ignored;
    VkDescriptorSet dstSet;
    const VkDescriptorBufferInfo *pBufferInfo;
    const VkDescriptorImageInfo *pImageInfo;
    const VkBufferView *pTexelBufferView;
} VkWriteDescriptorSet;
enum { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER=4, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER=5,
       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER=7,
       DM_CPU_PHASE_UBO=0, DM_CPU_PHASE_DESCRIPTOR=1, VK_NULL_HANDLE=0 };
#define die(...) abort()
static int descriptor_type_uniform(unsigned t) { return t==6||t==8; }
static uint64_t dm_cpu_phase_begin(void) { return 0; }
static void dm_cpu_phase_end(unsigned phase,uint64_t start) { (void)phase;(void)start; }
static int async_read_known_full_copy(uintptr_t p,void *out,uint32_t n) { (void)p;(void)out;(void)n;return 0; }
static int snapshot_heap_api_available(void) { return 0; }
static int dm_android_guest_heap_copy_owned(uintptr_t p,void *out,uint32_t n) { (void)p;(void)out;(void)n;abort(); }
static int dm_android_read_memory(uintptr_t p,void *out,uint32_t n) { memcpy(out,(void *)p,n);return 0; }
static void async_overlay_copies(uintptr_t p,void *out,uint32_t n) { (void)p;(void)out;(void)n; }
static void snapshot_read_account(uint32_t n) { (void)n; }
static unsigned read_bucket(uintptr_t p,uint32_t n) { return (unsigned)((p>>4)^n); }
static uint8_t arena[65536];
static size_t arena_used;
static VkDescriptorBufferInfo uniform_allocate_overwrite(uint32_t n,uint8_t **mapped) {
    assert(n<=sizeof arena-arena_used);*mapped=arena+arena_used;
    VkDescriptorBufferInfo out={1,arena_used,n};arena_used+=n;return out;
}
#include "../native/nvn_vulkan_binding_cache.inc"

static uint64_t reference_hash(const uint64_t *words,size_t count) {
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<count;i++){h^=words[i];h*=1099511628211ull;}
    h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;return h;
}
static void check_payload(const VkWriteDescriptorSet *writes,unsigned count,const uint64_t *expected,size_t words) {
    descriptor_payload_build(writes,count);
    assert(descriptor_payload_words==words);
    assert(!words||!memcmp(descriptor_payload,expected,words*sizeof *expected));
    assert(descriptor_payload_hash==reference_hash(expected,words));
}
static void cleanup(void) {
    while(uniform_version_chunks){uniform_version_chunk *c=uniform_version_chunks;uniform_version_chunks=c->next;free(c);}
    while(descriptor_version_chunks){descriptor_version_chunk *c=descriptor_version_chunks;descriptor_version_chunks=c->next;
        for(unsigned i=0;i<64;i++)free(c->entries[i].payload);free(c);}
    free(descriptor_payload);
}
int main(int argc,char **argv) {
    VkDescriptorBufferInfo buffers[]={{11,64,256},{12,128,512}};
    VkDescriptorImageInfo image={21,22,1,0xdeadbeef};VkBufferView texels[]={31,32};
    VkWriteDescriptorSet writes[]={
        {.descriptorType=6,.dstBinding=0,.descriptorCount=2,.pBufferInfo=buffers},
        {.descriptorType=1,.dstBinding=1,.descriptorCount=1,.pImageInfo=&image},
        {.descriptorType=4,.dstBinding=2,.descriptorCount=2,.pTexelBufferView=texels}};
    const uint64_t expected[]={6ull<<32,2,11,64,256,12,128,512,(1ull<<32)|1,1,21,22,1,(4ull<<32)|2,2,31,32};
    check_payload(writes,3,expected,sizeof expected/sizeof *expected);
    image.padding=0;writes[0].dstSet=999;writes[0].ignored=17;
    check_payload(writes,3,expected,sizeof expected/sizeof *expected);
    const unsigned types[]={0,1,2,3,4,5,6,7,8,10};
    for(unsigned i=0;i<sizeof types/sizeof *types;i++){
        unsigned type=types[i];
        VkWriteDescriptorSet w={.descriptorType=type,.dstBinding=7,.descriptorCount=1,
            .pBufferInfo=buffers,.pImageInfo=&image,.pTexelBufferView=texels};
        uint64_t e[5]={((uint64_t)type<<32)|7,1,0,0,0};size_t n=5;
        if(type==6||type==7||type==8){e[2]=11;e[3]=64;e[4]=256;}
        else if(type==4||type==5){e[2]=31;n=3;}
        else{e[2]=21;e[3]=22;e[4]=1;}
        check_payload(&w,1,e,n);
    }
    assert(!descriptor_cache_find(1,writes,3));descriptor_cache_store(1,101);
    assert(descriptor_cache_find(1,writes,3)==101);
    buffers[0].offset++;assert(!descriptor_cache_find(1,writes,3));descriptor_cache_store(1,102);
    buffers[0].offset--;assert(descriptor_cache_find(1,writes,3)==101);
    assert(!descriptor_cache_find(2,writes,3));
    assert(!descriptor_cache_find(1,writes,2));
    check_payload(NULL,0,NULL,0);
    uint32_t guest=17;VkDescriptorBufferInfo a=uniform_read_cached((uintptr_t)&guest,sizeof guest);
    assert(uniform_read_cached((uintptr_t)&guest,sizeof guest).offset==a.offset);
    guest=18;VkDescriptorBufferInfo b=uniform_read_cached((uintptr_t)&guest,sizeof guest);
    assert(b.offset!=a.offset&&*(uint32_t *)(arena+a.offset)==17&&*(uint32_t *)(arena+b.offset)==18);
    binding_cache_reset();assert(!descriptor_cache_find(1,writes,3));
    if(argc==2&&!strcmp(argv[1],"--bench")){
        clock_t start=clock();volatile uint64_t sum=0;
        for(unsigned i=0;i<2000000;i++){buffers[0].offset=i;descriptor_payload_build(writes,3);sum^=descriptor_payload_hash;}
        printf("descriptor encode+hash: %.3f ms checksum=%llu\n",1000.*(clock()-start)/CLOCKS_PER_SEC,(unsigned long long)sum);
    }
    cleanup();puts("binding_cache_test PASS: payload/hash, arrays, types, padding, history, fresh uniforms, reset");
}
