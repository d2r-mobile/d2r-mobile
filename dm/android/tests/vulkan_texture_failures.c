// 故障注入运行真实上传/缓冲/提交代码，检查失败时资源平衡、输出不变及非一致内存刷新。
// Fault injection runs real upload/buffer/submit code, checking balanced resources, unchanged failure outputs and noncoherent flush.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define HANDLE(type,value) ((type)(uintptr_t)(value))
static unsigned call, fail_at, images, buffers, memories, views, commands, fences, mapped, pending, flushed, barriers, copies;
static unsigned char staging[512];
static VkResult step(void) { return ++call == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS; }
#define FAIL_POINT() do { VkResult r = step(); if (r != VK_SUCCESS) return r; } while (0)
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice device,VkFormat format,VkFormatProperties *out) {
    (void)device; CHECK(format==VK_FORMAT_R8G8B8A8_UNORM); memset(out,0,sizeof *out);
    out->optimalTilingFeatures=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice device,VkFormat format,VkImageType type,
    VkImageTiling tiling,VkImageUsageFlags usage,VkImageCreateFlags flags,VkImageFormatProperties *out) {
    (void)device; CHECK(format==VK_FORMAT_R8G8B8A8_UNORM&&type==VK_IMAGE_TYPE_2D&&tiling==VK_IMAGE_TILING_OPTIMAL);
    CHECK(usage==(VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT)&&!flags); FAIL_POINT();
    *out=(VkImageFormatProperties){.maxExtent={16384,16384,1},.maxMipLevels=15,.maxArrayLayers=2048,
        .sampleCounts=VK_SAMPLE_COUNT_1_BIT,.maxResourceSize=UINT64_MAX}; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,const VkImageCreateInfo *info,const VkAllocationCallbacks *allocator,VkImage *out) {
    (void)device;(void)allocator;CHECK(info->mipLevels==1&&info->arrayLayers==1);FAIL_POINT();images++;*out=HANDLE(VkImage,1);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device,VkImage image,VkMemoryRequirements *out) {
    (void)device;(void)image;*out=(VkMemoryRequirements){.size=512,.alignment=16,.memoryTypeBits=3};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *allocator,VkDeviceMemory *out) {
    (void)device;(void)allocator;CHECK(info->memoryTypeIndex==1&&info->allocationSize==512);FAIL_POINT();*out=HANDLE(VkDeviceMemory,3+memories);memories++;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device,VkImage image,VkDeviceMemory memory,VkDeviceSize offset) {
    (void)device;(void)image;(void)memory;CHECK(!offset);FAIL_POINT();return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device,const VkBufferCreateInfo *info,const VkAllocationCallbacks *allocator,VkBuffer *out) {
    (void)device;(void)allocator;CHECK(info->size==4&&info->usage==VK_BUFFER_USAGE_TRANSFER_SRC_BIT);FAIL_POINT();buffers++;*out=HANDLE(VkBuffer,2);return VK_SUCCESS;
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
    if (!barriers) CHECK(src==VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&
        image->oldLayout==VK_IMAGE_LAYOUT_UNDEFINED&&image->newLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&image->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
    else CHECK(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)&&
        image->oldLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&image->newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&
        image->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&image->dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
    barriers++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer command,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy *regions) {
    (void)command;(void)buffer;(void)image;CHECK(flushed==1&&barriers==1&&count==1&&layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CHECK(!regions->bufferOffset&&regions->imageExtent.width==1&&regions->imageExtent.height==1&&regions->imageSubresource.layerCount==1);
    CHECK(staging[0]==11&&staging[1]==22&&staging[2]==33&&staging[3]==44);copies++;
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
    for (unsigned failure=0;failure<=16;failure++) {
        call=flushed=barriers=copies=0;fail_at=failure;
        dm_vk_texture out;memset(&out,0x5a,sizeof out);dm_vk_texture before=out;
        VkResult result=dm_vk_texture_upload(&vk,&desc,source,sizeof source,&out);
        if(failure) { CHECK(result==VK_ERROR_OUT_OF_HOST_MEMORY&&!memcmp(&out,&before,sizeof out));CHECK(call==failure); }
        else { CHECK(result==VK_SUCCESS&&call==16&&flushed==1&&copies==1);dm_vk_texture_destroy(&vk,&out); }
        CHECK(!images&&!buffers&&!memories&&!views&&!commands&&!fences&&!mapped&&!pending);
        printf("PASS: %s, output/resource/flush/completion invariants\n",names[failure]);
    }
    return 0;
}
