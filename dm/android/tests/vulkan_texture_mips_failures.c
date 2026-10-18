// #95 多级上传逐阶段失败注入：自有 4x4/2x2/1x1 带行距级别，真实 vk_texture_mips.c + vk_memory.c + vk_host.c，
// 每个可达失败点必须返回错误、out 字节不变、资源平衡且不在提交中销毁；另有非法输入创建前拒绝、填充行距拷贝布局
// oracle 及一个证明 oracle 能抓住破坏的负控。设备丢失路径只按 mock 清理模型断言：pending 由构造清零（表示丢失的
// 工作不在途），仅验证返回 DEVICE_LOST、out 不变、资源平衡；不证明 GPU 工作完成或真实驱动安全。
// #95 per-stage failure injection for the real multi-level upload path with owned padded 4x4/2x2/1x1 levels; every
// reachable failure must return an error, leave out byte-identical, balance resources and never destroy in-flight
// objects. Also invalid-input rejection before allocation, a padded mip copy/layout oracle, a negative control proving
// the oracle catches corruption, and a mock device-loss cleanup model (returned failure, out unchanged, balanced cleanup;
// pending is cleared by construction and does not prove GPU completion or real driver safety).
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define HANDLE(type, value) ((type)(uintptr_t)(value))

static unsigned call, fail_at, images, memories, buffers, views, commands, fences, mapped, pending, flushed, barriers, copies;
static int wait_device_lost;
static unsigned char staging[256];
static VkBufferImageCopy captured[3];
static unsigned captured_count;

static void reset(void) {
    call = fail_at = images = memories = buffers = views = commands = fences =
        mapped = pending = flushed = barriers = copies = captured_count = 0;
    wait_device_lost = 0;
    memset(staging, 0x7f, sizeof staging);
}

static VkResult step(void) { return ++call == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS; }

static const uint32_t exp_w[3] = {4, 2, 1};
static const uint32_t exp_h[3] = {4, 2, 1};
static const uint64_t exp_off[3] = {0, 64, 80};

// 独立布局 oracle：偏移必须是紧排前缀和，维度/子资源必须逐级对应。负控会篡改偏移并证明返回 0。
// Independent layout oracle: offsets must be the tight prefix sum and dimensions/subresources must match per level.
static int regions_ok(const VkBufferImageCopy *r, unsigned count) {
    if (count != 3) return 0;
    for (unsigned l = 0; l < 3; l++) {
        if (r[l].bufferOffset != exp_off[l]) return 0;
        if (r[l].imageExtent.width != exp_w[l] || r[l].imageExtent.height != exp_h[l] ||
            r[l].imageExtent.depth != 1) return 0;
        if (r[l].imageSubresource.mipLevel != l || r[l].imageSubresource.baseArrayLayer != 0 ||
            r[l].imageSubresource.layerCount != 1) return 0;
        if (r[l].bufferRowLength || r[l].bufferImageHeight || r[l].imageOffset.x || r[l].imageOffset.y || r[l].imageOffset.z)
            return 0;
    }
    return 1;
}

static int staging_ok(void) {
    for (unsigned l = 0; l < 3; l++) {
        for (uint32_t y = 0; y < exp_h[l]; y++) {
            unsigned char want = (unsigned char)(0xA0 + l * 0x10 + y);
            for (uint32_t x = 0; x < exp_w[l] * 4; x++)
                if (staging[exp_off[l] + y * exp_w[l] * 4 + x] != want) return 0;
        }
    }
    for (unsigned i = 84; i < sizeof staging; i++)
        if (staging[i] != 0x7f) return 0;   // 不得越过紧排暂存区 | must not write past the tight staging area
    return 1;
}

static void balanced(void) {
    CHECK(!images && !memories && !buffers && !views && !commands && !fences && !mapped && !pending);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice device, VkFormat format,
    VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *out) {
    (void)device;
    CHECK(format == VK_FORMAT_R8G8B8A8_UNORM && type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL && !flags);
    CHECK(usage == (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    *out = (VkImageFormatProperties){.maxExtent = {16384, 16384, 1}, .maxMipLevels = 15, .maxArrayLayers = 1,
        .sampleCounts = VK_SAMPLE_COUNT_1_BIT, .maxResourceSize = UINT64_MAX};
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImage *out) {
    (void)device; (void)allocator;
    CHECK(info->imageType == VK_IMAGE_TYPE_2D && info->format == VK_FORMAT_R8G8B8A8_UNORM && !info->flags);
    CHECK(info->mipLevels == 3 && info->arrayLayers == 1 && info->extent.width == 4 && info->extent.height == 4 &&
        info->extent.depth == 1 && info->samples == VK_SAMPLE_COUNT_1_BIT && info->tiling == VK_IMAGE_TILING_OPTIMAL);
    CHECK(info->usage == (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    images++; *out = HANDLE(VkImage, 1); return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *out) {
    (void)device; (void)image;
    *out = (VkMemoryRequirements){.size = 4096, .alignment = 16, .memoryTypeBits = 3};
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    (void)device; (void)allocator;
    CHECK(info->memoryTypeIndex < 2 && info->allocationSize);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    memories++; *out = HANDLE(VkDeviceMemory, 3 + memories); return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    (void)device; (void)image; (void)memory; CHECK(!offset);
    return step();
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *out) {
    (void)device; (void)allocator;
    CHECK(info->size == 84 && info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT && info->sharingMode == VK_SHARING_MODE_EXCLUSIVE);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    buffers++; *out = HANDLE(VkBuffer, 2); return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *out) {
    (void)device; (void)buffer;
    *out = (VkMemoryRequirements){.size = 256, .alignment = 16, .memoryTypeBits = 2};
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    (void)device; (void)buffer; (void)memory; CHECK(!offset);
    return step();
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize size, VkMemoryMapFlags flags, void **out) {
    (void)device; (void)memory; CHECK(!offset && size == VK_WHOLE_SIZE && !flags);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    mapped = 1; *out = staging; return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t count, const VkMappedMemoryRange *ranges) {
    (void)device; CHECK(count == 1 && !ranges->offset && ranges->size == VK_WHOLE_SIZE && mapped == 1);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    flushed++; return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImageView *out) {
    (void)device; (void)allocator;
    CHECK(info->image == HANDLE(VkImage, 1) && info->viewType == VK_IMAGE_VIEW_TYPE_2D &&
        info->format == VK_FORMAT_R8G8B8A8_UNORM && info->subresourceRange.levelCount == 3 &&
        info->subresourceRange.baseMipLevel == 0 && info->subresourceRange.layerCount == 1);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    views++; *out = HANDLE(VkImageView, 5); return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out) {
    (void)device; CHECK(info->commandBufferCount == 1 && info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkResult r = step(); if (r != VK_SUCCESS) return r;
    commands++; *out = HANDLE(VkCommandBuffer, 6); return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer command, const VkCommandBufferBeginInfo *info) {
    (void)command; CHECK(info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return step();
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command, VkPipelineStageFlags src, VkPipelineStageFlags dst,
    VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
    const VkBufferMemoryBarrier *buffer, uint32_t image_count, const VkImageMemoryBarrier *image) {
    (void)command; (void)memory; (void)buffer;
    CHECK(!flags && !memory_count && !buffer_count && image_count == 1 && image->image == HANDLE(VkImage, 1));
    CHECK(image->subresourceRange.levelCount == 3 && image->subresourceRange.layerCount == 1);
    if (barriers == 0) {
        CHECK(src == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && dst == VK_PIPELINE_STAGE_TRANSFER_BIT &&
            image->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && image->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    } else {
        CHECK(barriers == 1 && src == VK_PIPELINE_STAGE_TRANSFER_BIT &&
            dst == (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) &&
            image->oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            image->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    barriers++;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer command, VkBuffer buffer, VkImage image,
    VkImageLayout layout, uint32_t count, const VkBufferImageCopy *regions) {
    (void)command;
    CHECK(buffer == HANDLE(VkBuffer, 2) && image == HANDLE(VkImage, 1) &&
        layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && flushed == 1 && barriers == 1);
    CHECK(regions_ok(regions, count) && staging_ok());
    for (unsigned i = 0; i < count && i < 3; i++) captured[i] = regions[i];
    captured_count = count;
    copies++;
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
    call++;
    if (wait_device_lost) { pending = 0; return VK_ERROR_DEVICE_LOST; }   // 丢失后无在途工作可等待 | lost work is not in flight
    if (call == fail_at) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pending = 0; return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { (void)device; pending = 0; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    (void)device; (void)fence; (void)allocator; CHECK(fences == 1 && !pending); fences--;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer *command) {
    (void)device; (void)pool; (void)command; CHECK(commands == 1 && count == 1 && !pending); commands--;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView view, const VkAllocationCallbacks *allocator) {
    (void)device; (void)view; (void)allocator; CHECK(views == 1); views--;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) {
    (void)device; (void)image; (void)allocator; CHECK(images == 1 && !views); images--;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device; (void)buffer; (void)allocator; CHECK(buffers == 1 && !mapped); buffers--;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory) { (void)device; (void)memory; CHECK(mapped == 1); mapped = 0; }

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) {
    (void)device; (void)memory; (void)allocator; CHECK(memories > 0); memories--;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    dm_vk vk = {.device = HANDLE(VkDevice, 10), .physical = HANDLE(VkPhysicalDevice, 11)};
    vk.properties.limits.maxImageDimension2D = 16384;
    vk.properties.limits.maxStorageBufferRange = 1u << 20;
    vk.memory.memoryTypeCount = 2;
    vk.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vk.memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;   // 非一致 → 必须 flush | noncoherent → flush

    unsigned char l0[4 * 20], l1[2 * 12], l2[4];
    for (unsigned y = 0; y < 4; y++) { memset(l0 + y * 20, 0xEE, 20); memset(l0 + y * 20, 0xA0 + y, 16); }
    for (unsigned y = 0; y < 2; y++) { memset(l1 + y * 12, 0xEE, 12); memset(l1 + y * 12, 0xB0 + y, 8); }
    memset(l2, 0xC0, sizeof l2);
    dm_vk_mip_level levels[3] = {{l0, sizeof l0, 20}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}};

    // 1. 非法输入必须在创建任何 GPU 对象前拒绝，out 不变。
    // 1. Invalid input is rejected before any GPU object exists, out unchanged.
    struct { uint32_t w, h, n; dm_vk_mip_level lv[3]; } bad[] = {
        {0, 4, 3, {{l0, sizeof l0, 20}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
        {4, 4, 0, {{l0, sizeof l0, 20}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
        {4, 4, 4, {{l0, sizeof l0, 20}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
        {4, 4, 3, {{NULL, sizeof l0, 20}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
        {4, 4, 3, {{l0, 63, 20}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
        {4, 4, 3, {{l0, sizeof l0, 12}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
        {4, 4, 3, {{l0, sizeof l0, 18}, {l1, sizeof l1, 12}, {l2, sizeof l2, 4}}},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        reset();
        dm_vk_texture out; memset(&out, 0x5a, sizeof out); dm_vk_texture before = out;
        VkResult r = dm_vk_texture_upload_mips(&vk, bad[i].w, bad[i].h, bad[i].n, bad[i].lv, &out);
        CHECK(r != VK_SUCCESS);
        CHECK(!memcmp(&out, &before, sizeof out));
        CHECK(!images && !memories && !buffers && !views && !commands && !fences && !mapped);
    }
    puts("PASS: invalid inputs rejected before creation");

    // 2. 成功路径：三级填充行距被逐行摊平，偏移/维度/字节数正确，flush 一次、拷贝一次、两个屏障。
    // 2. Success: padded rows flattened, offsets/dimensions/byte counts correct, one flush, one copy, two barriers.
    reset();
    dm_vk_texture texture; memset(&texture, 0x5a, sizeof texture);
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, levels, &texture) == VK_SUCCESS);
    unsigned total = call;
    CHECK(total == 16 && copies == 1 && barriers == 2 && flushed == 1 && captured_count == 3);
    CHECK(staging_ok() && regions_ok(captured, 3));
    CHECK(texture.image == HANDLE(VkImage, 1) && texture.view == HANDLE(VkImageView, 5) && texture.levels == 3);
    dm_vk_texture_destroy(&vk, &texture);
    balanced();
    puts("PASS: success path padded layout/flush/barriers and cleanup");

    // 3. 每个可达失败点：返回错误、out 不变、资源平衡、无在途销毁。
    // 3. Every reachable failure: error, out unchanged, balanced resources, no in-flight destruction.
    for (unsigned f = 1; f <= total; f++) {
        reset(); fail_at = f;
        dm_vk_texture out; memset(&out, 0x5a, sizeof out); dm_vk_texture before = out;
        VkResult r = dm_vk_texture_upload_mips(&vk, 4, 4, 3, levels, &out);
        CHECK(r == VK_ERROR_OUT_OF_HOST_MEMORY);
        CHECK(call == f);
        CHECK(!memcmp(&out, &before, sizeof out));
        balanced();
    }
    puts("PASS: every multi-level failure point returns error, balances resources and preserves out");

    // 4. 等待时设备丢失的 mock 清理模型：pending 由构造清零（丢失工作不在途），只断言返回 DEVICE_LOST、out 不变、
    //    资源平衡；不证明真实 GPU 完成或驱动安全。
    // 4. Mock device-loss cleanup model: pending is cleared by construction (lost work is not in flight); only the
    //    returned DEVICE_LOST, unchanged out and balanced cleanup are asserted, not real GPU completion/driver safety.
    reset(); fail_at = total; wait_device_lost = 1;
    { dm_vk_texture out; memset(&out, 0x5a, sizeof out); dm_vk_texture before = out;
      CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, levels, &out) == VK_ERROR_DEVICE_LOST);
      CHECK(call == total && !memcmp(&out, &before, sizeof out)); balanced(); }
    puts("PASS: device-loss mock cleanup model (returned DEVICE_LOST, out unchanged, resources balanced)");

    // 5. 负控：篡改一个拷贝偏移后，布局 oracle 必须拒绝，证明它不是恒真检查。
    // 5. Negative control: a corrupted copy offset must be rejected by the layout oracle.
    { VkBufferImageCopy bad_copy[3]; memcpy(bad_copy, captured, sizeof bad_copy);
      CHECK(regions_ok(bad_copy, 3) == 1);
      bad_copy[1].bufferOffset += 4;
      CHECK(regions_ok(bad_copy, 3) == 0); }
    puts("PASS: negative control catches a corrupted copy offset");

    printf("vulkan_texture_mips_failures: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
