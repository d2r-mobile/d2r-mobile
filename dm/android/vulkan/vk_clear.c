// 离屏图像清除与读回：验证最优排布图像、布局转换、transfer 与 host 可见性。
// Offscreen image clear / readback: verify optimal-tiling images, layout transitions, transfers and host visibility.
#include "vk_host.h"
#include <string.h>

#define TRY(call) do { result = (call); if (result != VK_SUCCESS) goto done; } while (0)
VkResult dm_vk_clear_pixels(dm_vk *vk, dm_vk_pixels *pixels) {
    if (!pixels->bytes || !pixels->width || !pixels->height || pixels->width > 16384 || pixels->height > 16384 ||
        pixels->width > vk->properties.limits.maxImageDimension2D || pixels->height > vk->properties.limits.maxImageDimension2D)
        return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t size = (uint64_t)pixels->width * pixels->height * 4;
    if (size > pixels->capacity) return VK_ERROR_INITIALIZATION_FAILED;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    dm_vk_buffer buffer = {0};
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkResult result;
    VkImageCreateInfo create = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {pixels->width, pixels->height, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    TRY(vkCreateImage(vk->device, &create, NULL, &image));
    VkMemoryRequirements requirements; vkGetImageMemoryRequirements(vk->device, image, &requirements);
    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < vk->memory.memoryTypeCount; i++) if (requirements.memoryTypeBits & (1u << i)) {
        selected = i; if (vk->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
    }
    if (selected == UINT32_MAX) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto done; }
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = selected};
    TRY(vkAllocateMemory(vk->device, &allocate, NULL, &memory));
    TRY(vkBindImageMemory(vk->device, image, memory, 0));
    dm_vk_buffer_info info = {.size = size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    TRY(dm_vk_buffer_create(vk, &info, &buffer));
    TRY(dm_vk_begin(vk, &command));
    VkImageMemoryBarrier image_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &image_barrier);
    VkClearColorValue color = {.float32 = {1, 0, 1, 1}};
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &image_barrier.subresourceRange);
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &image_barrier);
    VkBufferImageCopy copy = {.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {pixels->width, pixels->height, 1}};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle, 1, &copy);
    VkBufferMemoryBarrier buffer_barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer.handle, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &buffer_barrier, 0, NULL);
    result = dm_vk_submit(vk, command); command = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) goto done;
    TRY(dm_vk_buffer_sync(vk, &buffer, 0));
    memcpy(pixels->bytes, buffer.mapped, (size_t)size);
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    dm_vk_buffer_destroy(vk, &buffer);
    if (image) vkDestroyImage(vk->device, image, NULL);
    if (memory) vkFreeMemory(vk->device, memory, NULL);
    return result;
}
