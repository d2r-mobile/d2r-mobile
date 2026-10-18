// #103 ... 不声称真实 GPU 像素隔离/回滚；没有具体存储模型，未触碰子资源的像素不变性明确报告为不可用。
// #103 ... No real GPU pixel-isolation/rollback claim; without a concrete storage model, unchanged-byte evidence for
// untouched subresources is explicitly reported unavailable.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define HANDLE(type, value) ((type)(uintptr_t)(value))

static unsigned call, fail_at, buffers, memories, commands, fences, mapped, pending, barriers, copies;
static VkBufferImageCopy seen_copy;
static VkImageSubresourceRange seen_range;
static int write_path;              // 1 = update buffer->image, 0 = read image->buffer
static uint8_t staging[64];
static const VkImage image = HANDLE(VkImage, 1);
static dm_vk_texture texture = {.image = image, .format = VK_FORMAT_R8G8B8A8_UNORM,
    .width = 8, .height = 8, .levels = 2, .layers = 2};

static void reset(void) {
    call = fail_at = buffers = memories = commands = fences = mapped = pending = barriers = copies = 0;
    memset(&seen_copy, 0, sizeof seen_copy); memset(&seen_range, 0, sizeof seen_range);
    memset(staging, 0x7f, sizeof staging);
}
static VkResult step(void) { return ++call == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS; }
static void balanced(void) { CHECK(!buffers && !memories && !commands && !fences && !mapped && !pending); }

static int copy_matches(const VkBufferImageCopy *c, uint32_t level, uint32_t layer, int32_t x, int32_t y,
                        uint32_t w, uint32_t h) {
    return c->imageSubresource.mipLevel == level && c->imageSubresource.baseArrayLayer == layer &&
        c->imageSubresource.layerCount == 1 && c->imageOffset.x == x && c->imageOffset.y == y &&
        c->imageExtent.width == w && c->imageExtent.height == h && c->imageExtent.depth == 1 &&
        !c->bufferRowLength && !c->bufferImageHeight;
}

// 未走上传/映像创建路径的桩，仅满足链接；若被调用立即失败。 | Unused upload/image stubs satisfy the linker only.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d, const VkImageCreateInfo *i, const VkAllocationCallbacks *a, VkImage *o) {
    (void)d; (void)i; (void)a; (void)o; return VK_ERROR_INITIALIZATION_FAILED;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d, VkImage i, const VkAllocationCallbacks *a) { (void)d; (void)i; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *o) {
    (void)d; (void)i; memset(o, 0, sizeof *o);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize o) {
    (void)d; (void)i; (void)m; (void)o; return VK_ERROR_INITIALIZATION_FAILED;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *o) {
    (void)d; (void)i; (void)a; (void)o; return VK_ERROR_INITIALIZATION_FAILED;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d, VkImageView v, const VkAllocationCallbacks *a) { (void)d; (void)v; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice d, VkFormat f, VkFormatProperties *o) {
    (void)d; (void)f; memset(o, 0, sizeof *o);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice d, VkFormat f, VkImageType t,
    VkImageTiling ti, VkImageUsageFlags u, VkImageCreateFlags fl, VkImageFormatProperties *o) {
    (void)d; (void)f; (void)t; (void)ti; (void)u; (void)fl; memset(o, 0, sizeof *o); return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *out) {
    (void)device; (void)allocator;
    CHECK(info->size == 16 && info->sharingMode == VK_SHARING_MODE_EXCLUSIVE);
    CHECK(info->usage == (write_path ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    buffers++; *out = HANDLE(VkBuffer, 2); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *out) {
    (void)device; (void)buffer; *out = (VkMemoryRequirements){.size = 64, .alignment = 16, .memoryTypeBits = 2};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    (void)device; (void)allocator; CHECK(info->memoryTypeIndex == 1 && info->allocationSize == 64);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    memories++; *out = HANDLE(VkDeviceMemory, 3); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    (void)device; (void)buffer; (void)memory; CHECK(!offset); return step();
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize size, VkMemoryMapFlags flags, void **out) {
    (void)device; (void)memory; CHECK(!offset && size == VK_WHOLE_SIZE && !flags);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    mapped = 1; *out = staging; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t count, const VkMappedMemoryRange *ranges) {
    (void)device; CHECK(count == 1 && !ranges->offset && ranges->size == VK_WHOLE_SIZE && mapped == 1);
    VkResult r = step(); if (r != VK_SUCCESS) return r; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t count, const VkMappedMemoryRange *ranges) {
    (void)device; CHECK(count == 1 && !ranges->offset && ranges->size == VK_WHOLE_SIZE && mapped == 1);
    return step();
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out) {
    (void)device; CHECK(info->commandBufferCount == 1 && info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    commands++; *out = HANDLE(VkCommandBuffer, 6); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer command, const VkCommandBufferBeginInfo *info) {
    (void)command; CHECK(info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); return step();
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command, VkPipelineStageFlags src, VkPipelineStageFlags dst,
    VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
    const VkBufferMemoryBarrier *buffer, uint32_t image_count, const VkImageMemoryBarrier *image_barrier) {
    (void)command; (void)memory; (void)buffer;
    CHECK(!flags && !memory_count && !buffer_count && image_count == 1 && image_barrier->image == image);
    CHECK(image_barrier->subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
        image_barrier->subresourceRange.baseMipLevel == 1 && image_barrier->subresourceRange.levelCount == 1 &&
        image_barrier->subresourceRange.baseArrayLayer == 1 && image_barrier->subresourceRange.layerCount == 1);
    CHECK(image_barrier->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
        image_barrier->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    if (!barriers) {
        CHECK(src == (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) &&
            dst == VK_PIPELINE_STAGE_TRANSFER_BIT);
        CHECK(image_barrier->oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            image_barrier->newLayout == (write_path ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL :
                                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
    } else {
        CHECK(src == VK_PIPELINE_STAGE_TRANSFER_BIT &&
            dst == (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
        CHECK(image_barrier->oldLayout == (write_path ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL :
                                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) &&
            image_barrier->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    seen_range = image_barrier->subresourceRange; barriers++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer command, VkBuffer buffer, VkImage dst_image,
    VkImageLayout layout, uint32_t count, const VkBufferImageCopy *regions) {
    (void)command; CHECK(buffer == HANDLE(VkBuffer, 2) && dst_image == image &&
        layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1 && barriers == 1);
    CHECK(copy_matches(&regions[0], 1, 1, 2, 1, 2, 2));
    // 源行距 12、区域宽 8：紧排暂存仅前 8 字节/行，填充 0xEE 不得进入。 | Only tight 8-byte rows enter staging.
    CHECK(staging[0] == 0x11 && staging[7] == 0x11 && staging[8] == 0x22 && staging[15] == 0x22);
    CHECK(staging[16] == 0x7f);
    seen_copy = regions[0]; copies++; write_path = 1;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer command, VkImage src_image, VkImageLayout layout,
    VkBuffer buffer, uint32_t count, const VkBufferImageCopy *regions) {
    (void)command; CHECK(src_image == image && buffer == HANDLE(VkBuffer, 2) &&
        layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && count == 1 && barriers == 1);
    CHECK(copy_matches(&regions[0], 1, 1, 2, 1, 2, 2));
    seen_copy = regions[0]; copies++; write_path = 0;
    for (unsigned y = 0; y < 2; y++)
        for (unsigned x = 0; x < 8; x++) staging[y * 8 + x] = (uint8_t)(0x40 + y * 8 + x);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer command) { (void)command; return step(); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *out) {
    (void)device; (void)info; (void)allocator;
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    fences++; *out = HANDLE(VkFence, 7); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence) {
    (void)queue; (void)info; (void)fence; CHECK(count == 1 && copies == 1 && barriers == 2);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    pending = 1; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t count, const VkFence *fence,
    VkBool32 all, uint64_t timeout) {
    (void)device; (void)fence; CHECK(count == 1 && all && timeout == UINT64_MAX && pending);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    pending = 0; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { (void)device; pending = 0; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    (void)device; (void)fence; (void)allocator; CHECK(fences == 1 && !pending); fences--;
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer *command) {
    (void)device; (void)pool; (void)command; CHECK(commands == 1 && count == 1 && !pending); commands--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device; (void)buffer; (void)allocator; CHECK(buffers == 1 && !mapped); buffers--;
}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory) { (void)device; (void)memory; CHECK(mapped == 1); mapped = 0; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) {
    (void)device; (void)memory; (void)allocator; CHECK(memories == 1); memories--;
}

static void fill_source(uint8_t *src) {
    memset(src, 0xEE, 24);
    for (unsigned y = 0; y < 2; y++)
        for (unsigned x = 0; x < 8; x++) src[y * 12 + x] = (uint8_t)((y ? 0x22 : 0x11));
}

int main(void) {
    dm_vk vk = {.device = HANDLE(VkDevice, 10)};
    vk.memory.memoryTypeCount = 2;
    vk.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vk.memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    dm_vk_region region = {.x = 2, .y = 1, .width = 2, .height = 2};
    uint8_t source[24], before[24];
    fill_source(source); memcpy(before, source, sizeof before);
    dm_vk_texture snapshot = texture;

    // 1. 非零 mip+layer 更新成功：barrier/copy 只选 (1,1)，填充不进入暂存，descriptor 不变。
    reset(); write_path = 1;
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 1, &region, source, sizeof source, 12) == VK_SUCCESS);
    CHECK(copies == 1 && barriers == 2 && seen_range.baseMipLevel == 1 && seen_range.baseArrayLayer == 1);
    CHECK(copy_matches(&seen_copy, 1, 1, 2, 1, 2, 2));
    CHECK(!memcmp(source, before, sizeof source));
    CHECK(!memcmp(&texture, &snapshot, sizeof texture));
    balanced();

    // 2. 非零 mip+layer 读回成功：目的行距填充保持，区域/子资源精确。
    reset(); write_path = 0;
    uint8_t dest[24]; memset(dest, 0x5C, sizeof dest);
    CHECK(dm_vk_texture_read_rows(&vk, &texture, 1, 1, &region, 12, dest, sizeof dest) == VK_SUCCESS);
    CHECK(copies == 1 && barriers == 2 && copy_matches(&seen_copy, 1, 1, 2, 1, 2, 2));
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned x = 0; x < 8; x++) CHECK(dest[y * 12 + x] == (uint8_t)(0x40 + y * 8 + x));
        for (unsigned x = 8; x < 12; x++) CHECK(dest[y * 12 + x] == 0x5C);
    }

    // 3. 非法 level/layer/region/短缓冲在分配前拒绝；descriptor 不变。
    reset();
    uint8_t dest_before[24]; memcpy(dest_before, dest, sizeof dest);
    dm_vk_region bad = region;
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 2, 1, &bad, source, sizeof source, 12) != VK_SUCCESS);
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 2, &bad, source, sizeof source, 12) != VK_SUCCESS);
    bad.x = 3; CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 1, &bad, source, sizeof source, 12) != VK_SUCCESS);
    bad = region; bad.y = 3;
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 1, &bad, source, sizeof source, 12) != VK_SUCCESS);
    bad = region; bad.width = 0;
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 1, &bad, source, sizeof source, 12) != VK_SUCCESS);
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 1, &region, source, 15, 12) != VK_SUCCESS);
    CHECK(dm_vk_texture_update_rows(&vk, &texture, 1, 1, &region, source, sizeof source, 7) != VK_SUCCESS);
    CHECK(dm_vk_texture_read_rows(&vk, &texture, 1, 1, &bad, 12, dest, sizeof dest) != VK_SUCCESS);
    CHECK(dm_vk_texture_read_rows(&vk, &texture, 1, 1, &region, 12, dest, 15) != VK_SUCCESS);
    CHECK(dm_vk_texture_read_rows(&vk, &texture, 2, 1, &region, 12, dest, sizeof dest) != VK_SUCCESS);
    CHECK(dm_vk_texture_read_rows(&vk, &texture, 1, 2, &region, 12, dest, sizeof dest) != VK_SUCCESS);
    CHECK(call == 0 && copies == 0 && barriers == 0);   // 未分配/未发命令 | no allocation or command emitted
    CHECK(!buffers && !memories && !commands && !fences);
    CHECK(!memcmp(dest, dest_before, sizeof dest));
    CHECK(!memcmp(&texture, &snapshot, sizeof texture));

    // 4. 每个可达失败点：错误、资源平衡、descriptor/source/dest 不变。
    reset(); write_path = 1;
    CHECK(dm_vk_texture_update_rows(&vk, (dm_vk_texture *)&texture, 1, 1, &region, source, sizeof source, 12) == VK_SUCCESS);
    unsigned total = call;
    for (unsigned f = 1; f <= total; f++) {
        reset(); fail_at = f; write_path = 1;
        uint8_t s[24]; fill_source(s); uint8_t s_before[24]; memcpy(s_before, s, sizeof s);
        VkResult r = dm_vk_texture_update_rows(&vk, (dm_vk_texture *)&texture, 1, 1, &region, s, sizeof s, 12);
        CHECK(r == VK_ERROR_OUT_OF_HOST_MEMORY);
        CHECK(!memcmp(s, s_before, sizeof s));
        CHECK(!memcmp(&texture, &snapshot, sizeof texture));
        balanced();
    }
    reset(); write_path = 0;
    CHECK(dm_vk_texture_read_rows(&vk, &texture, 1, 1, &region, 12, dest, sizeof dest) == VK_SUCCESS);
    total = call;
    for (unsigned f = 1; f <= total; f++) {
        reset(); fail_at = f; write_path = 0;
        memset(dest, 0x5C, sizeof dest);
        VkResult r = dm_vk_texture_read_rows(&vk, &texture, 1, 1, &region, 12, dest, sizeof dest);
        CHECK(r == VK_ERROR_OUT_OF_HOST_MEMORY);
        for (unsigned i = 0; i < sizeof dest; i++) CHECK(dest[i] == 0x5C);
        CHECK(!memcmp(&texture, &snapshot, sizeof texture));
        balanced();
    }

    // 5. 负控：错误 level/layer/y 必须被 oracle 拒绝，证明不是恒真。
    VkBufferImageCopy wrong = seen_copy;
    CHECK(copy_matches(&wrong, 1, 1, 2, 1, 2, 2));
    wrong.imageSubresource.mipLevel = 0;
    CHECK(!copy_matches(&wrong, 1, 1, 2, 1, 2, 2));
    wrong = seen_copy; wrong.imageSubresource.baseArrayLayer = 0;
    CHECK(!copy_matches(&wrong, 1, 1, 2, 1, 2, 2));
    wrong = seen_copy; wrong.imageOffset.y = 0;
    CHECK(!copy_matches(&wrong, 1, 1, 2, 1, 2, 2));

    printf("vulkan_texture_rows_subresource: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
