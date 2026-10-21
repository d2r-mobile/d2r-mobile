#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uintptr_t VkSampler;
typedef unsigned VkBorderColor;
enum { VK_FILTER_NEAREST=0,VK_FILTER_LINEAR=1,VK_SAMPLER_MIPMAP_MODE_NEAREST=0,VK_SAMPLER_MIPMAP_MODE_LINEAR=1,
       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER=3,VK_COMPARE_OP_ALWAYS=7,
       VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK=0,VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE=4,VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK=2,
       VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO=31 };
typedef struct {uint8_t minf,magf,ws,wt,wr,cmp_mode,cmp_func;float lod_min,lod_max,lod_bias,aniso,border[4];} samp_par;
typedef struct {int valid;samp_par parameters;VkSampler sampler;uint64_t revision;} sampler_descriptor;
typedef struct {int valid;uint64_t texture,revision;} texture_descriptor;
typedef struct {
    unsigned sType,magFilter,minFilter,mipmapMode,addressModeU,addressModeV,addressModeW,compareEnable,compareOp,borderColor;
    float mipLodBias,minLod,maxLod;
} VkSamplerCreateInfo;
static unsigned locks,held,created,logged;
static int descriptor_lock,device=1,sampler_capacity=2,texture_capacity=2;
static sampler_descriptor samplers[2],*sampler_descriptors=samplers;
static texture_descriptor textures[2],*texture_descriptors=textures;
static int pthread_mutex_lock(int *lock){assert(lock==&descriptor_lock&&!held);held=1;locks++;return 0;}
static int pthread_mutex_unlock(int *lock){assert(lock==&descriptor_lock&&held);held=0;return 0;}
static unsigned wrap_mode(unsigned mode){return mode==4?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:0;}
static void test_log(const char *format,...) {(void)format;logged++;}
#define LOG(...) test_log(__VA_ARGS__)
#define die(...) abort()
#define VK_CHECK(call) assert((call)==0)
static int vkCreateSampler(int d,const VkSamplerCreateInfo *info,const void *allocator,VkSampler *sampler) {
    (void)allocator;assert(d==device&&held&&info->sType==VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    *sampler=++created;return 0;
}
#include "../native/nvn_vulkan_sampler.inc"
#include "../native/nvn_vulkan_descriptor_lookup.inc"
int main(void) {
    sampler_descriptors[1]=(sampler_descriptor){.valid=1,.parameters={.minf=1,.magf=1,.lod_max=1000},.revision=1};
    texture_descriptors[1]=(texture_descriptor){.valid=1,.texture=11,.revision=1};
    uint32_t handle=(1u<<20)|1;texture_descriptor out;
    VkSampler first=descriptor_sampled(handle,1,&out);
    assert(first&&created==1&&locks==1&&!held&&out.valid&&out.texture==11);
    for(unsigned i=0;i<100;i++)assert(descriptor_sampled(handle,1,&out)==first);
    assert(locks==101&&created==1&&sampler_reuses==100);
    texture_descriptors[1].texture=12;texture_descriptors[1].revision++;
    assert(descriptor_sampled(handle,1,&out)==first&&out.texture==12&&out.revision==2);
    sampler_descriptors[1].parameters.magf=0;sampler_descriptors[1].sampler=0;sampler_descriptors[1].revision++;
    VkSampler second=descriptor_sampled(handle,1,&out);
    assert(second!=first&&created==2&&out.texture==12);
    assert(sampler_get_for_type(1,1)==second&&!held);
    assert(descriptor_texture(1).texture==12&&!descriptor_texture(-1).valid&&!descriptor_texture(2).valid);
    assert(descriptor_sampled((4095u<<20)|0xfffff,0,&out)&&!out.valid&&!held);
    out=(texture_descriptor){.valid=1,.texture=99};
    assert(descriptor_sampled(handle,1,NULL)==second&&out.texture==99&&!held);
    sampler_descriptors[1].parameters.cmp_mode=1;sampler_descriptors[1].parameters.cmp_func=2;sampler_descriptors[1].sampler=0;
    assert(descriptor_sampled(handle,1,&out)!=second&&logged>0&&!held);
    assert(sampler_creations==created);
    for(unsigned i=0;i<256;i++)while(sampler_cache[i]){sampler_cache_entry *entry=sampler_cache[i];sampler_cache[i]=entry->next;free(entry);}
    puts("descriptor_lookup_test PASS: one lock, cache hits, fresh registration, fallback, sampler-only, legacy callers");
}
