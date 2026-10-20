// Execute the production uploader with real Tegra layouts and a CPU copy recorder.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../gfx/nvn/nvn_obj.h"
#include "../../gfx/tex/nvn_deswizzle.h"
typedef unsigned VkImageAspectFlags;
typedef struct { unsigned aspectMask,mipLevel,baseArrayLayer,layerCount; } TestSubresource;
typedef struct { uint32_t width,height,depth; } TestExtent;
typedef struct { uint64_t bufferOffset;uint32_t bufferRowLength,bufferImageHeight;TestSubresource imageSubresource;struct {int x,y,z;} imageOffset;TestExtent imageExtent; } VkBufferImageCopy;
typedef struct { void *mapped;uint64_t size;unsigned buffer; } vk_buffer;
typedef struct { vk_buffer source; } post_op;
typedef struct { tex_t texture;unsigned image;VkImageAspectFlags aspect; } image_entry;
static unsigned command,posts,calls,seen;
static uint8_t *expected[4096];static size_t expected_bytes[4096];
static image_entry current;
static post_op held[4096];
static uint32_t dimension(uint32_t v,unsigned level){v>>=level;return v?v:1;}
#define die(...) abort()
#define LOG(...) do { if(0)printf(__VA_ARGS__); } while(0)
static post_op *post_new(int kind){assert(kind==3&&posts<4096);return &held[posts++];}
static vk_buffer buffer_create(uint64_t bytes){assert(bytes&&bytes<=SIZE_MAX);void *p=malloc((size_t)bytes);assert(p);memset(p,0xa5,(size_t)bytes);return (vk_buffer){p,bytes,posts};}
static void vkCmdCopyBufferToImage(unsigned cmd,unsigned buffer,unsigned image,unsigned layout,unsigned n,const VkBufferImageCopy *copies){
    (void)cmd;(void)image;(void)layout;assert(buffer&&buffer<=posts&&n);calls++;
    vk_buffer *b=&held[buffer-1].source;const tex_t *t=&current.texture;
    for(unsigned i=0;i<n;i++){
        const VkBufferImageCopy *r=&copies[i];unsigned sub=r->imageSubresource.baseArrayLayer*t->d.levels+r->imageSubresource.mipLevel;
        assert(sub==seen++&&sub<4096);int bw,bh;unsigned block=nvn_fmt_bpb(t->d.format,&bw,&bh);
        assert(r->bufferOffset%4==0&&r->bufferOffset%block==0);
        assert(r->imageSubresource.layerCount==1&&r->bufferRowLength==0&&r->bufferImageHeight==0);
        assert(r->bufferOffset<=b->size&&expected_bytes[sub]<=b->size-r->bufferOffset);
        assert(!memcmp((uint8_t*)b->mapped+r->bufferOffset,expected[sub],expected_bytes[sub]));
        assert(r->imageExtent.width==dimension(t->d.width,r->imageSubresource.mipLevel));
    }
}
#define VK_IMAGE_LAYOUT_GENERAL 1
#ifndef DM_UPLOAD_IMPLEMENTATION
#define DM_UPLOAD_IMPLEMENTATION "../native/nvn_vulkan_upload.inc"
#endif
#include DM_UPLOAD_IMPLEMENTATION
static unsigned run(nvn_texdesc desc,int batch){
    tex_t *t=&current.texture;memset(&current,0,sizeof current);t->d=desc;current.aspect=1;
    nvn_tex_layout(&t->d,&t->L);assert(t->L.size&&t->L.size<SIZE_MAX);
    uint8_t *storage=calloc(1,(size_t)t->L.size);assert(storage);posts=calls=seen=0;
    unsigned count=t->L.layers*t->d.levels;assert(count<=4096);
    for(unsigned sub=0;sub<count;sub++){
        unsigned level=sub%t->d.levels,layer=sub/t->d.levels;int bw,bh;unsigned block=nvn_fmt_bpb(t->d.format,&bw,&bh);
        uint32_t w=dimension(t->d.width,level),h=dimension(t->d.height,level),d=t->d.target==NVN_TEXTURE_TARGET_3D?dimension(t->d.depth,level):1;
        size_t pitch=((size_t)w+bw-1)/bw*block,rows=((size_t)h+bh-1)/bh;
        expected_bytes[sub]=pitch*rows*d;expected[sub]=malloc(expected_bytes[sub]);assert(expected[sub]);
        for(size_t i=0;i<expected_bytes[sub];i++)expected[sub][i]=(uint8_t)(sub*37+i*11);
        if(t->d.flags&(NVN_TEXTURE_FLAGS_LINEAR|NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)){
            size_t stride=t->d.stride?t->d.stride:pitch;uint8_t *dst=storage+nvn_tex_level_offset(&t->d,level)+layer*t->L.layer_size;
            for(unsigned z=0;z<d;z++)for(size_t y=0;y<rows;y++)memcpy(dst+(z*rows+y)*stride,expected[sub]+(z*rows+y)*pitch,pitch);
        }else assert(nvn_swizzle(&t->d,level,layer,expected[sub],pitch,storage)==0);
    }
    texture_upload_batch=batch;image_upload_regions(&current,storage);assert(seen==count);
    unsigned result=calls;assert(posts==calls);if(!batch)assert(calls==count);
    for(unsigned i=0;i<posts;i++)free(held[i].source.mapped);
    for(unsigned i=0;i<count;i++)free(expected[i]);free(storage);
    return result;
}
int main(void){
    const int formats[]={0x01,0x05,0x19,0x1d,0x22,0x25,0x41,0x44,0x4d,0x79};
    for(unsigned i=0;i<sizeof formats/sizeof *formats;i++){
        nvn_texdesc d={.target=NVN_TEXTURE_TARGET_2D_ARRAY,.format=formats[i],.width=33,.height=19,.depth=12,.levels=6};
        assert(run(d,0)==72);assert(run(d,1)==2);
        d.target=NVN_TEXTURE_TARGET_3D;d.depth=7;assert(run(d,1)==1);
    }
    assert(run((nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D,.format=0x01,.flags=NVN_TEXTURE_FLAGS_LINEAR,.width=17,.height=11,.depth=1,.levels=1,.stride=32},1)==1);
    assert(run((nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D_ARRAY,.format=0x25,.width=1024,.height=1024,.depth=5,.levels=1},1)==3);
    assert(run((nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D,.format=0x25,.width=2048,.height=2048,.depth=1,.levels=2},1)==2);
    texture_upload_report(60);
    puts("production upload bytes/alignment: R8/RG8/RGB/BC/ASTC, array/mips/3D, padded linear, bounded staging PASS");
}
