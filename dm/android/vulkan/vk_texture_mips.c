// #13 显式自有 RGBA8 UNORM 2D 单层多级上传：每级线性数据 + 行距，全部校验在创建 GPU 对象之前完成。
// #13 explicit owned RGBA8 UNORM 2D single-layer multi-mip upload: linear per-level data + row stride, all validation
// completed before any GPU object is created.
#include "vk_host.h"
#include <stdlib.h>
#include <string.h>

static uint32_t mip_dim(uint32_t size, uint32_t level) {
    uint32_t v = size >> level;
    return v ? v : 1;
}

static uint32_t full_mip_count(uint32_t width, uint32_t height) {
    uint32_t largest = width > height ? width : height, count = 1;
    while (largest > 1) { largest >>= 1; count++; }
    return count;
}

VkResult dm_vk_texture_upload_mips(dm_vk *vk, uint32_t width, uint32_t height, uint32_t levels,
                                   const dm_vk_mip_level *level, dm_vk_texture *out) {
    if (!vk || !vk->device || !out || !level) return VK_ERROR_INITIALIZATION_FAILED;
    if (!width || !height || !levels) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t max_levels = full_mip_count(width, height);
    if (levels > max_levels) return VK_ERROR_INITIALIZATION_FAILED;
    if (width > vk->properties.limits.maxImageDimension2D || height > vk->properties.limits.maxImageDimension2D)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageFormatProperties supported;
    VkResult result = vkGetPhysicalDeviceImageFormatProperties(vk->physical, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported);
    if (result != VK_SUCCESS) return result;
    if (levels > supported.maxMipLevels || width > supported.maxExtent.width || height > supported.maxExtent.height ||
        !(supported.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) return VK_ERROR_FORMAT_NOT_SUPPORTED;

    // 每级边界：行距、尺寸、覆盖字节；全部用 64 位算术，任何溢出/截断都在分配前拒绝。
    // Per-level bounds: stride, dimensions, covered bytes, all in 64-bit arithmetic; any overflow/truncation is rejected
    // before allocation.
    uint64_t staging_size = 0;
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t w = mip_dim(width, l), h = mip_dim(height, l);
        uint32_t row = level[l].row_bytes ? level[l].row_bytes : w * 4u;
        if (row < (uint64_t)w * 4u || row % 4u) return VK_ERROR_INITIALIZATION_FAILED;
        uint64_t need = (uint64_t)row * (h - 1u) + (uint64_t)w * 4u;
        if (!level[l].bytes || level[l].size < need) return VK_ERROR_INITIALIZATION_FAILED;
        uint64_t tight = (uint64_t)w * h * 4u;
        if (tight > (UINT64_MAX - staging_size)) return VK_ERROR_INITIALIZATION_FAILED;
        staging_size += tight;
    }
    if (staging_size > SIZE_MAX || staging_size > vk->properties.limits.maxStorageBufferRange ||
        staging_size > supported.maxResourceSize) return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    dm_vk_texture texture = {.format = VK_FORMAT_R8G8B8A8_UNORM, .view_type = VK_IMAGE_VIEW_TYPE_2D,
        .width = width, .height = height, .levels = levels, .layers = 1};
    dm_vk_buffer staging = {0};
    VkBufferImageCopy *copies = calloc(levels, sizeof *copies);
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (!copies) return VK_ERROR_OUT_OF_HOST_MEMORY;

    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = texture.format, .extent = {width, height, 1}, .mipLevels = levels, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    result = vkCreateImage(vk->device, &image, NULL, &texture.image);
    if (result != VK_SUCCESS) goto done;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vk->device, texture.image, &requirements);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < vk->memory.memoryTypeCount; i++) {
        if (!(requirements.memoryTypeBits & (1u << i)) ||
            (vk->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_PROTECTED_BIT)) continue;
        if (memory_type == UINT32_MAX) memory_type = i;
        if (vk->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { memory_type = i; break; }
    }
    if (memory_type == UINT32_MAX) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto done; }
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
    result = vkAllocateMemory(vk->device, &alloc, NULL, &texture.memory);
    if (result != VK_SUCCESS) goto done;
    result = vkBindImageMemory(vk->device, texture.image, texture.memory, 0);
    if (result != VK_SUCCESS) goto done;

    dm_vk_buffer_info buffer = {.size = staging_size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    result = dm_vk_buffer_create(vk, &buffer, &staging);
    if (result != VK_SUCCESS) goto done;
    uint64_t offset = 0;
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t w = mip_dim(width, l), h = mip_dim(height, l);
        uint32_t row = level[l].row_bytes ? level[l].row_bytes : w * 4u;
        const uint8_t *src = level[l].bytes;
        for (uint32_t y = 0; y < h; y++)
            memcpy((uint8_t *)staging.mapped + offset + (uint64_t)y * w * 4u, src + (uint64_t)y * row, (size_t)w * 4u);
        copies[l] = (VkBufferImageCopy){.bufferOffset = offset,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1}, .imageExtent = {w, h, 1}};
        offset += (uint64_t)w * h * 4u;
    }
    result = dm_vk_buffer_sync(vk, &staging, 1);
    if (result != VK_SUCCESS) goto done;
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = texture.format, .subresourceRange = range};
    result = vkCreateImageView(vk->device, &view, NULL, &texture.view);
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_begin(vk, &command);
    if (result != VK_SUCCESS) goto done;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture.image, .subresourceRange = range};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    vkCmdCopyBufferToImage(command, staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, copies);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    result = dm_vk_submit(vk, command);
    command = VK_NULL_HANDLE;
// 校验失败时本地回收已建对象（图像/视图/内存），成功时才写入 out。
// Release locally created objects on failure (image/view/memory); publish out only on success.
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    dm_vk_buffer_destroy(vk, &staging);
    free(copies);
    if (result == VK_SUCCESS) {
        *out = texture;
    } else {
        if (texture.view) vkDestroyImageView(vk->device, texture.view, NULL);
        if (texture.image) vkDestroyImage(vk->device, texture.image, NULL);
        if (texture.memory) vkFreeMemory(vk->device, texture.memory, NULL);
    }
    return result;
}
