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

// NVNformat → VkFormat：本层只承诺两种 4 字节 RGBA8 语义（0x25 UNORM、0x38 sRGB），其余一律按不支持拒绝，
// 不做猜测映射。两者存储布局相同（块线性、bpb 4），差别只在采样/附件读取时的 sRGB 解码。
// NVNformat -> VkFormat: this layer only promises the two 4-byte RGBA8 semantics (0x25 UNORM, 0x38 sRGB) and refuses
// everything else as unsupported rather than guessing a mapping. Both share the storage layout (block-linear, bpb 4);
// they differ only in the sRGB decode applied when sampled or read as an attachment.
static int map_format(int nvn_format, VkFormat *out) {
    switch (nvn_format) {
        case 0x25: if (out) *out = VK_FORMAT_R8G8B8A8_UNORM; return 1;
        case 0x38: if (out) *out = VK_FORMAT_R8G8B8A8_SRGB; return 1;
        default: return 0;
    }
}

static int descriptor_valid(const nvn_texdesc *d) {
    if ((d->target != 1 && d->target != 4) || d->flags || d->stride ||
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
    VkFormat format;
    if (!map_format(d.format, &format)) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint32_t layers = d.depth;
    const VkPhysicalDeviceLimits *limits = &vk->properties.limits;
    if (d.width > limits->maxImageDimension2D || d.height > limits->maxImageDimension2D ||
        layers > limits->maxImageArrayLayers) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkFormatProperties features;
    vkGetPhysicalDeviceFormatProperties(vk->physical, format, &features);
    VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((features.optimalTilingFeatures & needed) != needed) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    // 读回走 vkCmdCopyImageToBuffer，源映像必须带 TRANSFER_SRC（VUID-vkCmdCopyImageToBuffer-srcImage-00186）；
    // 能力查询用同一整掩码，缺任何一位都按不支持如实拒绝。
    // Readback goes through vkCmdCopyImageToBuffer, so the source image must carry TRANSFER_SRC
    // (VUID-vkCmdCopyImageToBuffer-srcImage-00186); the capability query uses the same full mask and reports
    // unsupported truthfully when any bit is missing.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageFormatProperties supported;
    VkResult result = vkGetPhysicalDeviceImageFormatProperties(vk->physical, format,
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
    dm_vk_texture texture = {.format = format,
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

// ---------- 线性 RGBA8 区域更新／读回（行距与填充） ----------
// ---------- Linear RGBA8 region update/readback (row stride and padding) ----------
static int region_within(const dm_vk_texture *texture, uint32_t level, uint32_t layer, const dm_vk_region *region) {
    if (!region || !region->width || !region->height) return 0;
    if (level >= texture->levels || layer >= texture->layers) return 0;
    return (uint64_t)region->x + region->width <= mip(texture->width, level) &&
           (uint64_t)region->y + region->height <= mip(texture->height, level);
}

// 行距语义与 nvn_copy_pitch 一致：0 = 紧排；非 0 须不小于区域宽度字节数。extent 是源／目的缓冲至少需要的字节数。
// Row-stride semantics match nvn_copy_pitch: 0 = tight; non-zero must be at least the extent width in bytes. extent is the
// minimum byte count the source/destination buffer must provide.
static VkResult row_layout(const dm_vk_region *region, uint32_t row_bytes, size_t *stride, size_t *extent) {
    size_t tight = (size_t)region->width * 4;
    if (!tight) return VK_ERROR_INITIALIZATION_FAILED;
    if (row_bytes && row_bytes < tight) return VK_ERROR_INITIALIZATION_FAILED;
    size_t selected = row_bytes ? (size_t)row_bytes : tight;
    if ((size_t)(region->height - 1) > (SIZE_MAX - tight) / selected) return VK_ERROR_INITIALIZATION_FAILED;
    *stride = selected;
    *extent = (size_t)(region->height - 1) * selected + tight;
    return VK_SUCCESS;
}

VkResult dm_vk_texture_update_rows(dm_vk *vk, dm_vk_texture *texture, uint32_t level, uint32_t layer,
    const dm_vk_region *region, const void *source, size_t source_size, uint32_t row_bytes) {
    if (!vk || !vk->device || !texture || !texture->image || !source) return VK_ERROR_INITIALIZATION_FAILED;
    // 行操作按原始字节搬运，不做 sRGB 编解码，因此两种 4 字节 RGBA8 语义都接受；采样端的解码差异由使用方承担。
    // Row operations move raw bytes with no sRGB encode/decode, so both 4-byte RGBA8 semantics are accepted; the
    // decode difference belongs to the sampling side.
    if (texture->format != VK_FORMAT_R8G8B8A8_UNORM && texture->format != VK_FORMAT_R8G8B8A8_SRGB)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (!region_within(texture, level, layer, region)) return VK_ERROR_INITIALIZATION_FAILED;
    size_t stride, extent;
    VkResult result = row_layout(region, row_bytes, &stride, &extent);
    if (result != VK_SUCCESS) return result;
    if (source_size < extent) return VK_ERROR_INITIALIZATION_FAILED;
    size_t tight = (size_t)region->width * 4, height = region->height;
    dm_vk_buffer staging = {0};
    dm_vk_buffer_info info = {.size = tight * height, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    result = dm_vk_buffer_create(vk, &info, &staging);
    if (result != VK_SUCCESS) return result;   // 创建失败已自行回收 | create already cleaned up after itself
    VkCommandBuffer command = VK_NULL_HANDLE;
    // 逐行只取区域宽度，行距里的填充留在调用者的源缓冲里。
    // Take only the extent width per row; padding inside the row stride stays in the caller's source buffer.
    for (size_t y = 0; y < height; y++)
        memcpy((uint8_t *)staging.mapped + y * tight, (const uint8_t *)source + y * stride, tight);
    result = dm_vk_buffer_sync(vk, &staging, 1);
    if (result == VK_SUCCESS) result = dm_vk_begin(vk, &command);
    if (result == VK_SUCCESS) {
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, layer, 1};
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image, .subresourceRange = range};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1},
            .imageOffset = {region->x, region->y, 0}, .imageExtent = {region->width, region->height, 1}};
        vkCmdCopyBufferToImage(command, staging.handle, texture->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        result = dm_vk_submit(vk, command);
        command = VK_NULL_HANDLE;   // 提交消费命令缓冲 | submit consumes the command buffer
    }
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    dm_vk_buffer_destroy(vk, &staging);
    return result;
}

VkResult dm_vk_texture_read_rows(dm_vk *vk, const dm_vk_texture *texture, uint32_t level, uint32_t layer,
    const dm_vk_region *region, uint32_t row_bytes, void *dest, size_t dest_size) {
    if (!vk || !vk->device || !texture || !texture->image || !dest) return VK_ERROR_INITIALIZATION_FAILED;
    // 行操作按原始字节搬运，不做 sRGB 编解码，因此两种 4 字节 RGBA8 语义都接受；采样端的解码差异由使用方承担。
    // Row operations move raw bytes with no sRGB encode/decode, so both 4-byte RGBA8 semantics are accepted; the
    // decode difference belongs to the sampling side.
    if (texture->format != VK_FORMAT_R8G8B8A8_UNORM && texture->format != VK_FORMAT_R8G8B8A8_SRGB)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (!region_within(texture, level, layer, region)) return VK_ERROR_INITIALIZATION_FAILED;
    size_t stride, extent;
    VkResult result = row_layout(region, row_bytes, &stride, &extent);
    if (result != VK_SUCCESS) return result;
    if (dest_size < extent) return VK_ERROR_INITIALIZATION_FAILED;
    size_t tight = (size_t)region->width * 4, height = region->height;
    dm_vk_buffer staging = {0};
    dm_vk_buffer_info info = {.size = tight * height, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    result = dm_vk_buffer_create(vk, &info, &staging);
    if (result != VK_SUCCESS) return result;
    VkCommandBuffer command = VK_NULL_HANDLE;
    result = dm_vk_begin(vk, &command);
    if (result == VK_SUCCESS) {
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, layer, 1};
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image, .subresourceRange = range};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1},
            .imageOffset = {region->x, region->y, 0}, .imageExtent = {region->width, region->height, 1}};
        vkCmdCopyImageToBuffer(command, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging.handle, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        result = dm_vk_submit(vk, command);
        command = VK_NULL_HANDLE;
    }
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    if (result == VK_SUCCESS) {
        result = dm_vk_buffer_sync(vk, &staging, 0);   // GPU 已完成后让主机读回 | let the host read back after the GPU finished
        // 逐行写回，目的缓冲行距里的填充保持原样。
        // Write row by row; padding inside the destination row stride stays untouched.
        if (result == VK_SUCCESS)
            for (size_t y = 0; y < height; y++)
                memcpy((uint8_t *)dest + y * stride, (const uint8_t *)staging.mapped + y * tight, tight);
    }
    dm_vk_buffer_destroy(vk, &staging);
    return result;
}
