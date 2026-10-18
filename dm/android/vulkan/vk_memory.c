// Vulkan 主机可见缓冲分配、映射与非一致内存刷新；依据 VkMemoryRequirements 和整分配范围同步约定。
// Vulkan host-visible buffer allocation, mapping and noncoherent memory flushing; based on VkMemoryRequirements / whole-allocation sync rules.
#include "vk_host.h"
#include <string.h>

VkResult dm_vk_buffer_create(dm_vk *vk, const dm_vk_buffer_info *info, dm_vk_buffer *out) {
    if (!info->size) return VK_ERROR_INITIALIZATION_FAILED;
    dm_vk_buffer buffer = {.size = info->size};
    VkBufferCreateInfo create = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = info->size,
        .usage = info->usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkResult result = vkCreateBuffer(vk->device, &create, NULL, &buffer.handle);
    if (result != VK_SUCCESS) return result;
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(vk->device, buffer.handle, &requirements);
    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < vk->memory.memoryTypeCount; i++) {
        VkMemoryPropertyFlags flags = vk->memory.memoryTypes[i].propertyFlags;
        if ((requirements.memoryTypeBits & (1u << i)) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            selected = i;
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
        }
    }
    if (selected == UINT32_MAX) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto failed; }
    buffer.coherent = (vk->memory.memoryTypes[selected].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = selected};
    result = vkAllocateMemory(vk->device, &allocate, NULL, &buffer.memory);
    if (result == VK_SUCCESS) result = vkBindBufferMemory(vk->device, buffer.handle, buffer.memory, 0);
    if (result == VK_SUCCESS) result = vkMapMemory(vk->device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped);
    if (result != VK_SUCCESS) goto failed;
    *out = buffer; return VK_SUCCESS;
failed:
    dm_vk_buffer_destroy(vk, &buffer); return result;
}

void dm_vk_buffer_destroy(dm_vk *vk, dm_vk_buffer *buffer) {
    if (buffer->mapped) vkUnmapMemory(vk->device, buffer->memory);
    if (buffer->handle) vkDestroyBuffer(vk->device, buffer->handle, NULL);
    if (buffer->memory) vkFreeMemory(vk->device, buffer->memory, NULL);
    memset(buffer, 0, sizeof *buffer);
}

VkResult dm_vk_buffer_sync(dm_vk *vk, dm_vk_buffer *buffer, int upload) {
    if (buffer->coherent) return VK_SUCCESS;
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = buffer->memory, .offset = 0, .size = VK_WHOLE_SIZE};
    return upload ? vkFlushMappedMemoryRanges(vk->device, 1, &range) : vkInvalidateMappedMemoryRanges(vk->device, 1, &range);
}
