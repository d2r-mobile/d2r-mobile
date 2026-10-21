// 故障注入运行真实上传/缓冲/提交代码，检查失败时资源平衡、输出不变及非一致内存刷新。
// 另含两段新增覆盖：映像创建/能力查询的 TRANSFER_SRC 用法回归（修复前 create-image 断言即失败），
// 以及 update/read 行操作每个失败点（缓冲、命令、提交）与「读回失败时目的缓冲保持哨兵值」。
// Fault injection runs real upload/buffer/submit code, checking balanced resources, unchanged failure outputs and
// noncoherent flush. Two blocks are added: the TRANSFER_SRC usage regression on image creation and the capability query
// (the create-image assertion fails before the fix), and per-failure-point coverage of the row update/readback
// operations (buffer, command, submit), including "a failed readback leaves the destination sentinel untouched".
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define HANDLE(type,value) ((type)(uintptr_t)(value))
static unsigned call, fail_at, images, buffers, memories, views, commands, fences, mapped, pending, flushed, barriers, copies, invalidates;
static unsigned char staging[512];
static VkResult step(void) { return ++call == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS; }
#define FAIL_POINT() do { VkResult r = step(); if (r != VK_SUCCESS) return r; } while (0)

// 被测路径模式：上传（整块 NVN 存储）、行更新、行读回；包装函数按模式校验各自的用法与屏障。
// Path modes: upload (full NVN storage), row update, row readback; the wrappers validate each mode's usage and barriers.
enum { M_UPLOAD, M_UPDATE, M_READ };
static int mode = M_UPLOAD;
static int deny_transfer_src;
static const VkImageUsageFlags image_usage =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
static const VkImage row_image = HANDLE(VkImage, 12);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice device,VkFormat format,VkFormatProperties *out) {
    (void)device; CHECK(format==VK_FORMAT_R8G8B8A8_UNORM); memset(out,0,sizeof *out);
    out->optimalTilingFeatures=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (!deny_transfer_src) out->optimalTilingFeatures|=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice device,VkFormat format,VkImageType type,
    VkImageTiling tiling,VkImageUsageFlags usage,VkImageCreateFlags flags,VkImageFormatProperties *out) {
    (void)device; CHECK(format==VK_FORMAT_R8G8B8A8_UNORM&&type==VK_IMAGE_TYPE_2D&&tiling==VK_IMAGE_TILING_OPTIMAL);
    // 能力查询必须与创建用同一整掩码（含 TRANSFER_SRC）。 | The capability query must use the same full mask as creation (TRANSFER_SRC included).
    CHECK(usage==image_usage&&!flags); FAIL_POINT();
    *out=(VkImageFormatProperties){.maxExtent={16384,16384,1},.maxMipLevels=15,.maxArrayLayers=2048,
        .sampleCounts=VK_SAMPLE_COUNT_1_BIT,.maxResourceSize=UINT64_MAX}; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,const VkImageCreateInfo *info,const VkAllocationCallbacks *allocator,VkImage *out) {
    (void)device;(void)allocator;CHECK(info->mipLevels==1&&info->arrayLayers==1);
    // 实际创建的映像必须带 TRANSFER_SRC，否则 vkCmdCopyImageToBuffer 非法（VUID-srcImage-00186）。修复前这条断言失败。
    // The created image must carry TRANSFER_SRC or vkCmdCopyImageToBuffer is illegal (VUID-srcImage-00186). This
    // assertion fails before the fix.
    CHECK(info->usage==image_usage);
    FAIL_POINT();images++;*out=HANDLE(VkImage,1);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device,VkImage image,VkMemoryRequirements *out) {
    (void)device;(void)image;*out=(VkMemoryRequirements){.size=512,.alignment=16,.memoryTypeBits=3};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *allocator,VkDeviceMemory *out) {
    (void)device;(void)allocator;CHECK(info->memoryTypeIndex==1&&info->allocationSize==512);
    FAIL_POINT();*out=HANDLE(VkDeviceMemory,3+memories);memories++;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device,VkImage image,VkDeviceMemory memory,VkDeviceSize offset) {
    (void)device;(void)image;(void)memory;CHECK(!offset);FAIL_POINT();return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device,const VkBufferCreateInfo *info,const VkAllocationCallbacks *allocator,VkBuffer *out) {
    (void)device;(void)allocator;
    if (mode==M_UPLOAD) CHECK(info->size==4&&info->usage==VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    else if (mode==M_UPDATE) CHECK(info->size==32&&info->usage==VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    else CHECK(info->size==32&&info->usage==VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    FAIL_POINT();buffers++;*out=HANDLE(VkBuffer,2);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device,VkBuffer buffer,VkMemoryRequirements *out) {
    (void)device;(void)buffer;*out=(VkMemoryRequirements){.size=512,.alignment=16,.memoryTypeBits=3};
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset) {
    (void)device;(void)buffer;(void)memory;CHECK(!offset);FAIL_POINT();return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void **out) {
    (void)device;(void)memory;CHECK(!offset&&size==VK_WHOLE_SIZE&&!flags);FAIL_POINT();mapped++;*out=staging;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device,uint32_t count,const VkMappedMemoryRange *ranges) {
    (void)device;CHECK(count==1&&!ranges->offset&&ranges->size==VK_WHOLE_SIZE&&mapped==1);FAIL_POINT();flushed++;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device,uint32_t count,const VkMappedMemoryRange *ranges) {
    (void)device;CHECK(count==1&&!ranges->offset&&ranges->size==VK_WHOLE_SIZE&&mapped==1);FAIL_POINT();invalidates++;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *allocator,VkImageView *out) {
    (void)device;(void)allocator;CHECK(info->viewType==VK_IMAGE_VIEW_TYPE_2D&&info->subresourceRange.levelCount==1);FAIL_POINT();views++;*out=HANDLE(VkImageView,5);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device,const VkCommandBufferAllocateInfo *info,VkCommandBuffer *out) {
    (void)device;CHECK(info->commandBufferCount==1);FAIL_POINT();commands++;*out=HANDLE(VkCommandBuffer,6);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer command,const VkCommandBufferBeginInfo *info) {
    (void)command;CHECK(info->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);FAIL_POINT();return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,
    uint32_t memory_count,const VkMemoryBarrier *memory,uint32_t buffer_count,const VkBufferMemoryBarrier *buffer,uint32_t image_count,const VkImageMemoryBarrier *image) {
    (void)command;(void)memory;(void)buffer;CHECK(!flags&&!memory_count&&!buffer_count&&image_count==1);
    CHECK(image->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&image->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    const VkPipelineStageFlags both=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (mode==M_UPLOAD) {
        CHECK(image->image==HANDLE(VkImage,1));
        if (!barriers) CHECK(src==VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&
            image->oldLayout==VK_IMAGE_LAYOUT_UNDEFINED&&image->newLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&image->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
        else CHECK(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==both&&
            image->oldLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&image->newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&
            image->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&image->dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
    } else if (mode==M_UPDATE) {
        CHECK(image->image==row_image);
        if (!barriers) CHECK(src==both&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&
            image->oldLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&image->newLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&
            image->srcAccessMask==VK_ACCESS_SHADER_READ_BIT&&image->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
        else CHECK(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==both&&
            image->oldLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&image->newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&
            image->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&image->dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
    } else {
        CHECK(image->image==row_image);
        if (!barriers) CHECK(src==both&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&
            image->oldLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&image->newLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&
            image->srcAccessMask==VK_ACCESS_SHADER_READ_BIT&&image->dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT);
        else CHECK(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==both&&
            image->oldLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&image->newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&
            image->srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&image->dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
    }
    barriers++;
}
// update 源与 read 目的共用的一条独立公式（不调被测代码）。 | One independent formula shared by the update source and read destination (not the code under test).
static void row_pattern(unsigned x, unsigned y, unsigned char *out) {
    out[0]=(unsigned char)(0x10+x); out[1]=(unsigned char)(0x20+y); out[2]=0x30; out[3]=0x40;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer command,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy *regions) {
    (void)command;(void)buffer;(void)image;
    if (mode==M_UPLOAD) {
        CHECK(flushed==1&&barriers==1&&count==1&&layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CHECK(!regions->bufferOffset&&regions->imageExtent.width==1&&regions->imageExtent.height==1&&regions->imageSubresource.layerCount==1);
        CHECK(staging[0]==11&&staging[1]==22&&staging[2]==33&&staging[3]==44);copies++;
        return;
    }
    // update：区域 (2,1) 4x2，缓冲里必须是紧排两行（行距 40 的源已被逐行摊平，填充没被搬进来）。
    // update: region (2,1) 4x2; the buffer must hold the two tight rows (the row-stride-40 source was flattened, padding not copied).
    CHECK(mode==M_UPDATE&&count==1&&layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&barriers==1);
    CHECK(regions->imageExtent.width==4&&regions->imageExtent.height==2&&regions->imageOffset.x==2&&regions->imageOffset.y==1&&
        regions->imageSubresource.layerCount==1&&regions->imageSubresource.mipLevel==0&&regions->imageSubresource.baseArrayLayer==0);
    for (unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++){unsigned char want[4];row_pattern(2+x,1+y,want);
        CHECK(!memcmp(staging+(y*4+x)*4,want,4));}
    CHECK(staging[32]==0&&staging[33]==0&&staging[34]==0&&staging[35]==0);   // 暂存只有 32 字节 | staging holds exactly 32 bytes
    copies++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer command,VkImage image,VkImageLayout layout,VkBuffer buffer,uint32_t count,const VkBufferImageCopy *regions) {
    (void)command;(void)buffer;
    CHECK(mode==M_READ&&image==row_image&&count==1&&layout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&barriers==1);
    CHECK(regions->imageExtent.width==4&&regions->imageExtent.height==2&&regions->imageOffset.x==2&&regions->imageOffset.y==1&&
        regions->imageSubresource.layerCount==1);
    for (unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++){unsigned char want[4];row_pattern(2+x,1+y,want);
        memcpy(staging+(y*4+x)*4,want,4);}
    copies++;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer command) { (void)command;FAIL_POINT();return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device,const VkFenceCreateInfo *info,const VkAllocationCallbacks *allocator,VkFence *out) {
    (void)device;(void)info;(void)allocator;FAIL_POINT();fences++;*out=HANDLE(VkFence,7);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue,uint32_t count,const VkSubmitInfo *info,VkFence fence) {
    (void)queue;(void)info;(void)fence;CHECK(count==1&&copies==1&&barriers==2);FAIL_POINT();pending=1;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device,uint32_t count,const VkFence *fence,VkBool32 all,uint64_t timeout) {
    (void)device;(void)fence;CHECK(count==1&&all&&timeout==UINT64_MAX&&pending);FAIL_POINT();pending=0;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { (void)device;CHECK(pending);pending=0;return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device,VkFence fence,const VkAllocationCallbacks *allocator) {
    (void)device;(void)fence;(void)allocator;CHECK(!pending&&fences==1);fences--;
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device,VkCommandPool pool,uint32_t count,const VkCommandBuffer *command) {
    (void)device;(void)pool;(void)command;CHECK(!pending&&commands==1&&count==1);commands--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device,VkImageView view,const VkAllocationCallbacks *allocator) {
    (void)device;(void)view;(void)allocator;CHECK(!pending&&views==1);views--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device,VkImage image,const VkAllocationCallbacks *allocator) {
    (void)device;(void)image;(void)allocator;CHECK(!pending&&!views&&images==1);images--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device,VkBuffer buffer,const VkAllocationCallbacks *allocator) {
    (void)device;(void)buffer;(void)allocator;CHECK(!pending&&!mapped&&buffers==1);buffers--;
}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device,VkDeviceMemory memory) { (void)device;(void)memory;CHECK(!pending&&mapped==1);mapped--; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device,VkDeviceMemory memory,const VkAllocationCallbacks *allocator) {
    (void)device;(void)allocator;CHECK(!pending&&memories);if(memory==HANDLE(VkDeviceMemory,3))CHECK(!images);else CHECK(!buffers);memories--;
}
static void balanced(void) { CHECK(!images&&!buffers&&!memories&&!views&&!commands&&!fences&&!mapped&&!pending); }

int main(void) {
    dm_vk vk={.device=HANDLE(VkDevice,10),.physical=HANDLE(VkPhysicalDevice,11)};
    vk.properties.limits.maxImageDimension2D=16384;vk.properties.limits.maxImageArrayLayers=2048;
    vk.memory.memoryTypeCount=2;
    vk.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT;
    vk.memory.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    nvn_texdesc desc={.target=1,.format=0x25,.width=1,.height=1,.depth=1,.levels=1};
    unsigned char source[512]={11,22,33,44};
    const char *names[]={"success","format query","image create","image allocate","image bind","buffer create","buffer allocate",
        "buffer bind","map","flush","view create","command allocate","command begin","command end","fence create","submit","wait"};

    // 1. 上传路径的失败点循环（原有覆盖不变：调用次数、输出不变、资源平衡、非一致刷新）。
    // 1. Upload failure-point loop (original coverage unchanged: call count, unchanged output, balanced resources, flush).
    mode=M_UPLOAD;
    for (unsigned failure=0;failure<=16;failure++) {
        call=flushed=barriers=copies=invalidates=0;fail_at=failure;
        dm_vk_texture out;memset(&out,0x5a,sizeof out);dm_vk_texture before=out;
        VkResult result=dm_vk_texture_upload(&vk,&desc,source,sizeof source,&out);
        if(failure) { CHECK(result==VK_ERROR_OUT_OF_HOST_MEMORY&&!memcmp(&out,&before,sizeof out));CHECK(call==failure); }
        else { CHECK(result==VK_SUCCESS&&call==16&&flushed==1&&copies==1);dm_vk_texture_destroy(&vk,&out); }
        balanced();
        printf("PASS: %s, output/resource/flush/completion invariants\n",names[failure]);
    }

    // 2. 能力查询如实：缺 TRANSFER_SRC 时必须按不支持拒绝，且不分配任何资源。
    // 2. Truthful capability: without TRANSFER_SRC the upload must be refused as unsupported, allocating nothing.
    deny_transfer_src=1;call=flushed=barriers=copies=0;fail_at=0;
    { dm_vk_texture out;memset(&out,0x5a,sizeof out);dm_vk_texture before=out;
      CHECK(dm_vk_texture_upload(&vk,&desc,source,sizeof source,&out)==VK_ERROR_FORMAT_NOT_SUPPORTED);
      CHECK(!memcmp(&out,&before,sizeof out));balanced(); }
    deny_transfer_src=0;
    puts("PASS: capability without TRANSFER_SRC is refused truthfully");

    // 3. 行更新：逐个失败点必须返回错误且资源平衡；成功时拷贝/屏障计数正确。
    // 3. Row update: every failure point returns an error with balanced resources; success checks copy/barrier counts.
    mode=M_UPDATE;
    const dm_vk_region region={2,1,4,2};
    unsigned char update_source[2*40]; memset(update_source,0xE7,sizeof update_source);
    for (unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++){unsigned char p[4];row_pattern(2+x,1+y,p);
        memcpy(update_source+y*40+x*4,p,4);}
    dm_vk_texture rowtex={.image=row_image,.format=VK_FORMAT_R8G8B8A8_UNORM,.width=8,.height=4,.levels=1,.layers=1};
    call=flushed=barriers=copies=invalidates=0;fail_at=0;
    CHECK(dm_vk_texture_update_rows(&vk,&rowtex,0,0,&region,update_source,sizeof update_source,40)==VK_SUCCESS);
    { unsigned total=call;CHECK(total>0&&copies==1&&barriers==2);balanced();
      for (unsigned failure=1;failure<=total;failure++) {
          call=flushed=barriers=copies=invalidates=0;fail_at=failure;
          CHECK(dm_vk_texture_update_rows(&vk,&rowtex,0,0,&region,update_source,sizeof update_source,40)!=VK_SUCCESS);
          balanced();
      }
      printf("PASS: row update %u failure points, resources balanced\n",total); }

    // 4. 行读回：失败时目的缓冲的哨兵值不得被写；成功时按行距落进目的且填充不动。
    // 4. Row readback: a failed read must not write the destination sentinel; success lands rows at the stride, padding untouched.
    mode=M_READ;
    unsigned char dest[2*24];
    call=flushed=barriers=copies=invalidates=0;fail_at=0;
    memset(dest,0x5C,sizeof dest);
    CHECK(dm_vk_texture_read_rows(&vk,&rowtex,0,0,&region,24,dest,sizeof dest)==VK_SUCCESS);
    { unsigned total=call;CHECK(total>0&&copies==1&&barriers==2&&invalidates==1);balanced();
      for (unsigned y=0;y<2;y++){for(unsigned x=0;x<4;x++){unsigned char want[4];row_pattern(2+x,1+y,want);
              CHECK(!memcmp(dest+y*24+x*4,want,4));}
          for (unsigned i=16;i<24;i++) CHECK(dest[y*24+i]==0x5C);}
      for (unsigned failure=1;failure<=total;failure++) {
          call=flushed=barriers=copies=invalidates=0;fail_at=failure;
          memset(dest,0x5C,sizeof dest);
          CHECK(dm_vk_texture_read_rows(&vk,&rowtex,0,0,&region,24,dest,sizeof dest)!=VK_SUCCESS);
          for (unsigned i=0;i<sizeof dest;i++) CHECK(dest[i]==0x5C);   // 失败读回不改目的 | a failed readback leaves the destination untouched
          balanced();
      }
      printf("PASS: row readback %u failure points, destination sentinel preserved\n",total); }
    fail_at=0;
    puts("PASS: TRANSFER_SRC usage, capability truthfulness and row-operation failure cleanup");
    return 0;
}
