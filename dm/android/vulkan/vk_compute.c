// 离屏计算作业：SPIR-V→描述符→push constants→提交/栅栏→读回；依据 Vulkan 1.1 同步与内存约定。
// Offscreen compute job: SPIR-V -> descriptors -> push constants -> submission / fence -> readback; based on Vulkan 1.1 synchronization / memory contracts.
#include "vk_host.h"
#include <string.h>

#define TRY(call) do { result = (call); if (result != VK_SUCCESS) goto done; } while (0)
VkResult dm_vk_compute_pixels(dm_vk *vk, const char *shader_path, dm_vk_pixels *pixels) {
    if (!pixels->bytes || !pixels->width || !pixels->height || pixels->width > 16384 || pixels->height > 16384)
        return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t size = (uint64_t)pixels->width * pixels->height * 4;
    if (size > pixels->capacity || size > vk->properties.limits.maxStorageBufferRange)
        return VK_ERROR_INITIALIZATION_FAILED;
    dm_vk_buffer buffer = {0};
    VkShaderModule shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkResult result;
    dm_vk_buffer_info buffer_info = {.size = size, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    TRY(dm_vk_buffer_create(vk, &buffer_info, &buffer));
    TRY(dm_vk_shader_load(vk,shader_path,&shader));
    VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo descriptor = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    TRY(vkCreateDescriptorSetLayout(vk->device, &descriptor, NULL, &descriptor_layout));
    VkPushConstantRange constants = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 8};
    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &descriptor_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &constants};
    TRY(vkCreatePipelineLayout(vk->device, &layout_info, NULL, &layout));
    VkComputePipelineCreateInfo compute = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = layout, .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"}};
    TRY(vkCreateComputePipelines(vk->device, VK_NULL_HANDLE, 1, &compute, NULL, &pipeline));
    VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
    VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
    TRY(vkCreateDescriptorPool(vk->device, &pool_info, NULL, &pool));
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &descriptor_layout};
    VkDescriptorSet set;
    TRY(vkAllocateDescriptorSets(vk->device, &alloc, &set));
    VkDescriptorBufferInfo storage = {.buffer = buffer.handle, .offset = 0, .range = size};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
        .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &storage};
    vkUpdateDescriptorSets(vk->device, 1, &write, 0, NULL);
    TRY(dm_vk_begin(vk, &command));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    uint32_t dimensions[2] = {pixels->width, pixels->height};
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof dimensions, dimensions);
    vkCmdDispatch(command, (pixels->width + 7) / 8, (pixels->height + 7) / 8, 1);
    VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer.handle, .offset = 0, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &barrier, 0, NULL);
    result = dm_vk_submit(vk, command); command = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) goto done;
    TRY(dm_vk_buffer_sync(vk, &buffer, 0));
    memcpy(pixels->bytes, buffer.mapped, (size_t)size);
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    if (pipeline) vkDestroyPipeline(vk->device, pipeline, NULL);
    if (pool) vkDestroyDescriptorPool(vk->device, pool, NULL);
    if (layout) vkDestroyPipelineLayout(vk->device, layout, NULL);
    if (descriptor_layout) vkDestroyDescriptorSetLayout(vk->device, descriptor_layout, NULL);
    if (shader) vkDestroyShaderModule(vk->device, shader, NULL);
    dm_vk_buffer_destroy(vk, &buffer);
    return result;
}
