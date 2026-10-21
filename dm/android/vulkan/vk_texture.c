// 有界 NVN RGBA8 纹理上传：复用布局与解块代码，宿主串行同步提交。
// Bounded NVN RGBA8 texture upload: reuse layout/deswizzle code and serialized synchronous host submission.
#include "vk_host.h"
#include "../../gfx/tex/nvn_deswizzle.h"
#include <stdlib.h>
#include <string.h>

static uint32_t mip(uint32_t size, uint32_t level) {
    uint32_t value = size >> level;
    return value ? value : 1;
}

static int descriptor_valid(const nvn_texdesc *d) {
    if ((d->target != 1 && d->target != 4) || d->format != 0x25 || d->flags || d->stride ||
        d->samples < 0 || d->samples > 1 || d->pkg_block_h < 0 || d->pkg_block_h > 6 ||
        !d->width || d->width > 16384 || !d->height || d->height > 16384 ||
        !d->depth || d->depth > 2048 || (d->target == 1 && d->depth != 1) || !d->levels) return 0;
    uint32_t count = 1, largest = d->width > d->height ? d->width : d->height;
    while (largest >>= 1) count++;
    return d->levels <= count;
}

void dm_vk_texture_destroy(dm_vk *vk, dm_vk_texture *texture) {
    if (texture->view) vkDestroyImageView(vk->device, texture->view, NULL);
    if (texture->image) vkDestroyImage(vk->device, texture->image, NULL);
    if (texture->memory) vkFreeMemory(vk->device, texture->memory, NULL);
    memset(texture, 0, sizeof *texture);
}

VkResult dm_vk_texture_upload(dm_vk *vk, const nvn_texdesc *desc,
    const void *source, size_t capacity, dm_vk_texture *out) {
    if (!vk || !desc || !source || !out || !vk->device || !descriptor_valid(desc))
        return VK_ERROR_INITIALIZATION_FAILED;
    nvn_texdesc d = *desc;
    uint32_t layers = d.depth;
    const VkPhysicalDeviceLimits *limits = &vk->properties.limits;
    if (d.width > limits->maxImageDimension2D || d.height > limits->maxImageDimension2D ||
        layers > limits->maxImageArrayLayers) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkFormatProperties features;
    vkGetPhysicalDeviceFormatProperties(vk->physical, VK_FORMAT_R8G8B8A8_UNORM, &features);
    VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((features.optimalTilingFeatures & needed) != needed) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageFormatProperties supported;
    VkResult result = vkGetPhysicalDeviceImageFormatProperties(vk->physical, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported);
    if (result != VK_SUCCESS) return result;
    if (d.width > supported.maxExtent.width || d.height > supported.maxExtent.height ||
        !supported.maxExtent.depth || d.levels > supported.maxMipLevels || layers > supported.maxArrayLayers ||
        !(supported.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) return VK_ERROR_FORMAT_NOT_SUPPORTED;

    // 先限制全部移位和上游 32 位算术，再调用允许缺省值的布局函数。
    // Bound all shifts and upstream 32-bit arithmetic before calling layout helpers that accept defaults.
    nvn_texlayout layout;
    nvn_tex_layout(&d, &layout);
    if (!layout.layer_size || layout.layers != layers || layout.bpb != 4 ||
        layout.layer_size > UINT64_MAX / layers || layout.size != layout.layer_size * layers ||
        layout.size > SIZE_MAX || layout.size > capacity) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t linear_size = 0;
    for (uint32_t level = 0; level < d.levels; level++) {
        uint64_t bytes = (uint64_t)mip(d.width, level) * mip(d.height, level) * 4;
        if (bytes > (UINT64_MAX - linear_size) / layers) return VK_ERROR_INITIALIZATION_FAILED;
        linear_size += bytes * layers;
        uint64_t begin = nvn_tex_level_offset(&d, level), end = nvn_tex_level_offset(&d, level + 1);
        if (end <= begin || end > layout.layer_size) return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (linear_size > SIZE_MAX) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t count = d.levels * layers;
    VkBufferImageCopy *copies = calloc(count, sizeof *copies);
    if (!copies) return VK_ERROR_OUT_OF_HOST_MEMORY;
    dm_vk_texture texture = {.format = VK_FORMAT_R8G8B8A8_UNORM,
        .view_type = d.target == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .width = d.width, .height = d.height, .levels = d.levels, .layers = layers};
    dm_vk_buffer staging = {0};
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = texture.format, .extent = {d.width, d.height, 1},
        .mipLevels = d.levels, .arrayLayers = layers, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    result = vkCreateImage(vk->device, &image, NULL, &texture.image);
    if (result != VK_SUCCESS) goto done;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vk->device, texture.image, &requirements);
    if (requirements.size > supported.maxResourceSize) { result = VK_ERROR_OUT_OF_DEVICE_MEMORY; goto done; }
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
    dm_vk_buffer_info buffer = {.size = linear_size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    result = dm_vk_buffer_create(vk, &buffer, &staging);
    if (result != VK_SUCCESS) goto done;
    uint64_t offset = 0;
    uint32_t index = 0;
    for (uint32_t level = 0; level < d.levels; level++) {
        uint32_t width = mip(d.width, level), height = mip(d.height, level);
        for (uint32_t layer = 0; layer < layers; layer++) {
            if (nvn_deswizzle(&d, level, layer, source, (uint8_t *)staging.mapped + offset, (size_t)width * 4)) {
                result = VK_ERROR_INITIALIZATION_FAILED; goto done;
            }
            copies[index++] = (VkBufferImageCopy){.bufferOffset = offset,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1}, .imageExtent = {width, height, 1}};
            offset += (uint64_t)width * height * 4;
        }
    }
    result = dm_vk_buffer_sync(vk, &staging, 1);
    if (result != VK_SUCCESS) goto done;
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, d.levels, 0, layers};
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image, .viewType = texture.view_type, .format = texture.format, .subresourceRange = range};
    result = vkCreateImageView(vk->device, &view, NULL, &texture.view);
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_begin(vk, &command);
    if (result != VK_SUCCESS) goto done;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = texture.image, .subresourceRange = range};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    vkCmdCopyBufferToImage(command, staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, copies);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    result = dm_vk_submit(vk, command);
    command = VK_NULL_HANDLE;
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    dm_vk_buffer_destroy(vk, &staging);
    free(copies);
    if (result == VK_SUCCESS) *out = texture;
    else dm_vk_texture_destroy(vk, &texture);
    return result;
}
