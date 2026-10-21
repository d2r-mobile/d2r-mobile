// 独立手算 NVN GOB/mip 布局，经真实 GPU texelFetch 核对全部 RGBA8 像素；不调用共享 swizzle/layout 生成期望值。
// Independently specified NVN GOB/mip fixtures, compared pixel-for-pixel through real GPU texelFetch; no shared swizzle/layout oracle.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define VK_CHECK(call) do{VkResult result=(call);if(result!=VK_SUCCESS){fprintf(stderr,"FAIL %s = %d\n",#call,result);exit(1);}}while(0)
typedef struct{uint32_t width,height,gobs;size_t offset;}mip_spec;
typedef struct{const char*name;nvn_texdesc desc;size_t layer_size;mip_spec mip[7];}fixture;
static const fixture fixtures[]={
    {"1x1",{.target=1,.format=0x25,.width=1,.height=1,.depth=1,.levels=1},512,{{1,1,1,0}}},
    {"13x11 mip chain",{.target=1,.format=0x25,.width=13,.height=11,.depth=1,.levels=4},2560,
        {{13,11,2,0},{6,5,1,1024},{3,2,1,1536},{1,1,1,2048}}},
    {"65x37 three layers",{.target=4,.format=0x25,.width=65,.height=37,.depth=3,.levels=7},28672,
        {{65,37,4,0},{32,18,4,20480},{16,9,2,24576},{8,4,1,25600},{4,2,1,26112},{2,1,1,26624},{1,1,1,27136}}},
    {"37x19 packaged block height",{.target=4,.format=0x25,.width=37,.height=19,.depth=2,.levels=6,.pkg_block_h=1},8704,
        {{37,19,1,0},{18,9,1,4608},{9,4,1,6656},{4,2,1,7168},{2,1,1,7680},{1,1,1,8192}}}
};
static uint32_t expected(uint32_t x,uint32_t y,unsigned level,unsigned layer){
    uint32_t r=(x*17+y*3+level*47+layer*71+11)&255,g=(x*5+y*29+level*13+layer*31+23)&255;
    uint32_t b=(x^(y*7)^(level*53)^(layer*97))&255,a=(x*19+y*11+level*67+layer*43+37)&255;
    return r|(g<<8)|(b<<16)|(a<<24);
}
static size_t address(const mip_spec*m,unsigned byte_x,unsigned y){
    unsigned columns=(m->width*4+63)/64;
    size_t block=((size_t)(y/(8*m->gobs))*columns+byte_x/64)*(512*m->gobs);
    size_t gob=(y/8%m->gobs)*512;
    unsigned bits=(byte_x&15)|((y&1)<<4)|((byte_x&16)<<1)|((y&6)<<5)|((byte_x&32)<<3);
    return m->offset+block+gob+bits;
}
static uint8_t*make_source(const fixture*f,size_t*size){
    *size=f->layer_size*f->desc.depth;uint8_t*bytes=malloc(*size+32),*written=calloc(*size,1);CHECK(bytes&&written);memset(bytes,0xcd,*size+32);
    for(unsigned layer=0;layer<f->desc.depth;layer++)for(unsigned level=0;level<f->desc.levels;level++){
        const mip_spec*m=&f->mip[level];for(unsigned y=0;y<m->height;y++)for(unsigned x=0;x<m->width;x++){
            uint32_t value=expected(x,y,level,layer);for(unsigned c=0;c<4;c++){
                size_t at=layer*f->layer_size+address(m,x*4+c,y);CHECK(at<*size&&!written[at]);written[at]=1;bytes[at]=(uint8_t)(value>>(8*c));}
        }
    }
    free(written);return bytes;
}
// 验证层通过 dm_vk_create_ex 显式请求（#42）；不再改 VK_INSTANCE_LAYERS 环境变量假装启用。
// The layer is requested through dm_vk_create_ex (#42); no VK_INSTANCE_LAYERS environment override pretending to enable it.
static size_t sample(dm_vk*vk,const dm_vk_texture*texture,const fixture*f,const char*shader_path){
    VkShaderModule shader;VK_CHECK(dm_vk_shader_load(vk,shader_path,&shader));
    VkDescriptorSetLayoutBinding bindings[2]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
    VkDescriptorSetLayout descriptor;VK_CHECK(vkCreateDescriptorSetLayout(vk->device,&di,NULL,&descriptor));
    VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=16};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&descriptor,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    VkPipelineLayout layout;VK_CHECK(vkCreatePipelineLayout(vk->device,&li,NULL,&layout));
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"}};
    VkPipeline pipeline;VK_CHECK(vkCreateComputePipelines(vk->device,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool pool;VK_CHECK(vkCreateDescriptorPool(vk->device,&pi,NULL,&pool));
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&descriptor};
    VkDescriptorSet set;VK_CHECK(vkAllocateDescriptorSets(vk->device,&ai,&set));
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=(float)(texture->levels-1)};
    VkSampler sampler;VK_CHECK(vkCreateSampler(vk->device,&si,NULL,&sampler));
    size_t words=(size_t)f->desc.width*f->desc.height+16;dm_vk_buffer buffer={0};dm_vk_buffer_info bi={words*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};VK_CHECK(dm_vk_buffer_create(vk,&bi,&buffer));
    VkDescriptorImageInfo image={.sampler=sampler,.imageView=texture->view,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo storage={.buffer=buffer.handle,.range=words*4};
    VkWriteDescriptorSet writes[2]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&storage}};
    vkUpdateDescriptorSets(vk->device,2,writes,0,NULL);size_t compared=0;
    for(unsigned layer=0;layer<f->desc.depth;layer++)for(unsigned level=0;level<f->desc.levels;level++){
        const mip_spec*m=&f->mip[level];memset(buffer.mapped,0xa6,words*4);VK_CHECK(dm_vk_buffer_sync(vk,&buffer,1));VkCommandBuffer command;VK_CHECK(dm_vk_begin(vk,&command));
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
        uint32_t shape[4]={m->width,m->height,level,layer};vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(shape),shape);vkCmdDispatch(command,(m->width+7)/8,(m->height+7)/8,1);
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer.handle,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);VK_CHECK(dm_vk_submit(vk,command));VK_CHECK(dm_vk_buffer_sync(vk,&buffer,0));
        const uint32_t*actual=buffer.mapped;for(unsigned y=0;y<m->height;y++)for(unsigned x=0;x<m->width;x++){
            uint32_t want=expected(x,y,level,layer),got=actual[(size_t)y*m->width+x];
            if(got!=want){fprintf(stderr,"PIXEL_MISMATCH %s layer=%u mip=%u x=%u y=%u actual=%08x expected=%08x\n",f->name,layer,level,x,y,got,want);exit(1);}compared++;
        }
        for(size_t i=(size_t)m->width*m->height;i<words;i++)CHECK(actual[i]==0xa6a6a6a6);
    }
    dm_vk_buffer_destroy(vk,&buffer);vkDestroySampler(vk->device,sampler,NULL);vkDestroyDescriptorPool(vk->device,pool,NULL);vkDestroyPipeline(vk->device,pipeline,NULL);
    vkDestroyPipelineLayout(vk->device,layout,NULL);vkDestroyDescriptorSetLayout(vk->device,descriptor,NULL);vkDestroyShaderModule(vk->device,shader,NULL);return compared;
}
static void reject(dm_vk*vk,const nvn_texdesc*d,const void*source,size_t size){
    dm_vk_texture out;memset(&out,0x5a,sizeof(out));unsigned char before[sizeof(out)];memcpy(before,&out,sizeof(out));
    CHECK(dm_vk_texture_upload(vk,d,source,size,&out)!=VK_SUCCESS);CHECK(!memcmp(before,&out,sizeof(out)));
}
static void invalid(dm_vk*vk){
    const fixture*f=&fixtures[0];size_t size;uint8_t*source=make_source(f,&size);nvn_texdesc d=f->desc;
    CHECK(dm_vk_texture_upload(vk,&d,source,size,NULL)!=VK_SUCCESS);
    reject(vk,&d,source,size-1);reject(vk,&d,NULL,size);reject(vk,NULL,source,size);
    const int formats[]={0,0x26,0x42,0x44,0x79};for(unsigned i=0;i<5;i++){d=f->desc;d.format=formats[i];reject(vk,&d,source,size);}
    const int targets[]={0,2,3,5,6,8,9,10};for(unsigned i=0;i<8;i++){d=f->desc;d.target=targets[i];reject(vk,&d,source,size);}
    d=f->desc;d.width=0;reject(vk,&d,source,size);d.width=UINT32_MAX;reject(vk,&d,source,size);
    d=f->desc;d.height=UINT32_MAX;reject(vk,&d,source,size);d=f->desc;d.depth=2;reject(vk,&d,source,size);
    d=f->desc;d.target=4;d.depth=UINT32_MAX;reject(vk,&d,source,size);d.depth=0;reject(vk,&d,source,size);
    d=f->desc;d.levels=0;reject(vk,&d,source,size);d.levels=2;reject(vk,&d,source,size);d.levels=UINT32_MAX;reject(vk,&d,source,size);
    d=f->desc;d.pkg_block_h=-1;reject(vk,&d,source,size);d.pkg_block_h=7;reject(vk,&d,source,size);
    d=f->desc;d.flags=0x10;reject(vk,&d,source,size);d.flags=1;reject(vk,&d,source,size);
    d=f->desc;d.stride=4;reject(vk,&d,source,size);d=f->desc;d.samples=2;reject(vk,&d,source,size);d.samples=-1;reject(vk,&d,source,size);
    free(source);puts("PASS: short/null source, invalid format/target/geometry/mips/layout/MSAA/overflow descriptors preserve output");
}
int main(int argc,char**argv){
    CHECK(argc==3);mip_spec gob={16,8,1,0};CHECK(address(&gob,0,0)==0&&address(&gob,32,0)==256&&address(&gob,0,1)==16);
    CHECK(address(&gob,16,0)==32&&address(&gob,0,2)==64&&address(&gob,63,7)==511);
    dm_vk_options vopt = DM_VK_OPTIONS_INIT; vopt.validation = DM_VK_VALIDATION_OPTIONAL;
    dm_vk vk={0};VK_CHECK(dm_vk_create_ex(&vopt,&vk));printf("validation: %s\n",dm_vk_validation_status(&vk));printf("GPU: %s Vulkan %u.%u.%u\n",vk.properties.deviceName,VK_VERSION_MAJOR(vk.properties.apiVersion),VK_VERSION_MINOR(vk.properties.apiVersion),VK_VERSION_PATCH(vk.properties.apiVersion));
    invalid(&vk);size_t total=0;
    for(unsigned i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);i++){
        const fixture*f=&fixtures[i];size_t size;uint8_t*source=make_source(f,&size);reject(&vk,&f->desc,source,size-1);
        dm_vk_texture texture={0};VK_CHECK(dm_vk_texture_upload(&vk,&f->desc,source,size,&texture));
        CHECK(texture.image&&texture.memory&&texture.view&&texture.format==VK_FORMAT_R8G8B8A8_UNORM&&texture.width==f->desc.width&&texture.height==f->desc.height&&texture.levels==f->desc.levels&&texture.layers==f->desc.depth);
        CHECK(texture.view_type==(f->desc.target==1?VK_IMAGE_VIEW_TYPE_2D:VK_IMAGE_VIEW_TYPE_2D_ARRAY));
        size_t pixels=sample(&vk,&texture,f,argv[f->desc.target==1?1:2]);total+=pixels;
        for(size_t j=size;j<size+32;j++)CHECK(source[j]==0xcd);free(source);
        dm_vk_texture_destroy(&vk,&texture);unsigned char zero[sizeof(texture)]={0};CHECK(!memcmp(&texture,zero,sizeof(texture)));dm_vk_texture_destroy(&vk,&texture);CHECK(!memcmp(&texture,zero,sizeof(texture)));
        printf("PASS: %s source=%zuB layers=%u levels=%u every GPU pixel=%zu and readback guards\n",f->name,size,f->desc.depth,f->desc.levels,pixels);
    }
    dm_vk_destroy(&vk);printf("PASS: actual sampled NVN RGBA8 uploads, total=%zu pixels, independent tiled fixtures and repeated destroy\n",total);return 0;
}
