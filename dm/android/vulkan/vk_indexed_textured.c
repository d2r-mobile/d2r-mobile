// #115 自有 indexed+texture 离屏绘制：预检 → 自有纹理/采样器 → 索引绘制 → RGBA8 读回。
// #115 owned indexed+textured offscreen draw: preflight -> owned texture/sampler -> indexed draw -> RGBA8 readback.
#include "vk_indexed_textured.h"
#include "vk_sampler.h"
#include <math.h>
#include <string.h>

#define TRY(call) do { result = (call); if (result != VK_SUCCESS) goto done; } while (0)

static int finite_range(float v, float lo, float hi) { return isfinite(v) && v >= lo && v <= hi; }

static VkResult draw_indexed_textured_impl(dm_vk *vk, const dm_vk_indexed_textured_draw *draw, dm_vk_pixels *pixels,
                                           const dm_vk_graphics_plan *plan) {
    if (!vk || !vk->device || !draw || !pixels || !draw->vertices || !draw->indices ||
        !draw->texture_pixels)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!plan && (!draw->vertex_spv || !draw->fragment_spv))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (draw->vertex_stride != DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE ||
        draw->vertex_count < 3 || draw->vertex_count > DM_VK_INDEXED_TEXTURED_MAX_VERTICES ||
        draw->index_count < 3 || draw->index_count > DM_VK_INDEXED_TEXTURED_MAX_INDICES || draw->index_count % 3)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!pixels->bytes || !pixels->width || !pixels->height || pixels->width > 16384 || pixels->height > 16384)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!draw->texture_width || !draw->texture_height) return VK_ERROR_INITIALIZATION_FAILED;
    if (draw->texture_width > 16384 || draw->texture_height > 16384) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t out_bytes = (uint64_t)pixels->width * pixels->height * 4u;
    uint64_t vertex_bytes = (uint64_t)draw->vertex_count * DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE;
    uint64_t index_bytes = (uint64_t)draw->index_count * 2u;
    uint64_t texture_bytes = (uint64_t)draw->texture_width * draw->texture_height * 4u;
    if (texture_bytes > SIZE_MAX || (uint64_t)draw->texture_size != texture_bytes)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (out_bytes > pixels->capacity) return VK_ERROR_INITIALIZATION_FAILED;
    if (out_bytes > vk->properties.limits.maxStorageBufferRange ||
        vertex_bytes > vk->properties.limits.maxStorageBufferRange ||
        index_bytes > vk->properties.limits.maxStorageBufferRange ||
        texture_bytes > vk->properties.limits.maxStorageBufferRange)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pixels->width > vk->properties.limits.maxImageDimension2D ||
        pixels->height > vk->properties.limits.maxImageDimension2D ||
        draw->texture_width > vk->properties.limits.maxImageDimension2D ||
        draw->texture_height > vk->properties.limits.maxImageDimension2D ||
        pixels->width > vk->properties.limits.maxViewportDimensions[0] ||
        pixels->height > vk->properties.limits.maxViewportDimensions[1] ||
        pixels->width > vk->properties.limits.maxFramebufferWidth ||
        pixels->height > vk->properties.limits.maxFramebufferHeight ||
        vk->properties.limits.maxVertexInputBindings < 1 ||
        vk->properties.limits.maxVertexInputAttributes < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(vk->physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
    if ((format.optimalTilingFeatures & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) !=
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((format.optimalTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) !=
        (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    for (uint32_t i = 0; i < draw->vertex_count; i++) {
        const dm_vk_indexed_textured_vertex *v = &draw->vertices[i];
        if (!finite_range(v->position[0], -1.0f, 1.0f) || !finite_range(v->position[1], -1.0f, 1.0f) ||
            !finite_range(v->uv[0], 0.0f, 1.0f) || !finite_range(v->uv[1], 0.0f, 1.0f))
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0; i < draw->index_count; i++)
        if (draw->indices[i] >= draw->vertex_count) return VK_ERROR_INITIALIZATION_FAILED;

    dm_vk_texture texture = {0};
    dm_vk_sampler sampler = {0};
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass render = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkShaderModule vertex_shader = VK_NULL_HANDLE, fragment_shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    dm_vk_buffer vertex_buffer = {0}, index_buffer = {0}, readback = {0};
    VkResult result = VK_SUCCESS;

    dm_vk_mip_level level = {.bytes = draw->texture_pixels, .size = draw->texture_size, .row_bytes = draw->texture_width * 4u};
    TRY(dm_vk_texture_upload_mips(vk, draw->texture_width, draw->texture_height, 1, &level, &texture));
    dm_vk_sampler_desc sd = {.min_filter = DM_VK_FILTER_NEAREST, .mag_filter = DM_VK_FILTER_NEAREST,
        .mipmap = DM_VK_MIP_NEAREST, .address_u = DM_VK_ADDRESS_CLAMP_TO_EDGE,
        .address_v = DM_VK_ADDRESS_CLAMP_TO_EDGE, .address_w = DM_VK_ADDRESS_CLAMP_TO_EDGE,
        .lod_min = 0.0f, .lod_max = 0.0f, .lod_bias = 0.0f, .max_aniso = 1.0f, .compare_enable = 0};
    TRY(dm_vk_sampler_create(vk, &sd, &sampler));
    dm_vk_buffer_info vbi = {.size = vertex_bytes, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
    dm_vk_buffer_info ibi = {.size = index_bytes, .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    dm_vk_buffer_info rbi = {.size = out_bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    TRY(dm_vk_buffer_create(vk, &vbi, &vertex_buffer));
    TRY(dm_vk_buffer_create(vk, &ibi, &index_buffer));
    memcpy(vertex_buffer.mapped, draw->vertices, (size_t)vertex_bytes);
    memcpy(index_buffer.mapped, draw->indices, (size_t)index_bytes);
    TRY(dm_vk_buffer_sync(vk, &vertex_buffer, 1));
    TRY(dm_vk_buffer_sync(vk, &index_buffer, 1));
    if (plan) {
        VkShaderModuleCreateInfo vsci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = plan->vs_code_bytes, .pCode = (const uint32_t *)(const void *)plan->vs_code};
        VkShaderModuleCreateInfo psci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = plan->ps_code_bytes, .pCode = (const uint32_t *)(const void *)plan->ps_code};
        TRY(vkCreateShaderModule(vk->device, &vsci, NULL, &vertex_shader));
        TRY(vkCreateShaderModule(vk->device, &psci, NULL, &fragment_shader));
    } else {
        TRY(dm_vk_shader_load(vk, draw->vertex_spv, &vertex_shader));
        TRY(dm_vk_shader_load(vk, draw->fragment_spv, &fragment_shader));
    }
    TRY(dm_vk_buffer_create(vk, &rbi, &readback));

    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {pixels->width, pixels->height, 1}, .mipLevels = 1,
        .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    TRY(vkCreateImage(vk->device, &image_info, NULL, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vk->device, image, &requirements);
    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < vk->memory.memoryTypeCount; i++)
        if (requirements.memoryTypeBits & (1u << i)) {
            selected = i;
            if (vk->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
        }
    if (selected == UINT32_MAX) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto done; }
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = selected};
    TRY(vkAllocateMemory(vk->device, &allocate, NULL, &image_memory));
    TRY(vkBindImageMemory(vk->device, image, image_memory, 0));
    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    TRY(vkCreateImageView(vk->device, &view_info, NULL, &view));

    VkAttachmentDescription attachment = {.format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference reference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &reference};
    VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
        .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};
    TRY(vkCreateRenderPass(vk->device, &pass, NULL, &render));
    VkFramebufferCreateInfo fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = render,
        .attachmentCount = 1, .pAttachments = &view, .width = pixels->width, .height = pixels->height, .layers = 1};
    TRY(vkCreateFramebuffer(vk->device, &fb, NULL, &framebuffer));
    VkDescriptorSetLayoutBinding binding = {.binding = plan ? plan->sampler_binding : 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    TRY(vkCreateDescriptorSetLayout(vk->device, &dci, NULL, &dsl));
    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl};
    TRY(vkCreatePipelineLayout(vk->device, &layout_info, NULL, &layout));
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment_shader, .pName = "main"}};
    VkVertexInputBindingDescription vb = {.binding = 0, .stride = DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {.location = plan ? plan->position_location : 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
        {.location = plan ? plan->uv_location : 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 8}};
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.width = (float)pixels->width, .height = (float)pixels->height, .maxDepth = 1};
    VkRect2D scissor = {.extent = {pixels->width, pixels->height}};
    VkPipelineViewportStateCreateInfo viewport_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend};
    VkGraphicsPipelineCreateInfo graphics = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_info, .pRasterizationState = &raster, .pMultisampleState = &samples,
        .pColorBlendState = &blending, .layout = layout, .renderPass = render, .basePipelineIndex = -1};
    TRY(vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &graphics, NULL, &pipeline));
    VkDescriptorPoolSize psz = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
        .poolSizeCount = 1, .pPoolSizes = &psz};
    TRY(vkCreateDescriptorPool(vk->device, &pci, NULL, &pool));
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl};
    TRY(vkAllocateDescriptorSets(vk->device, &ai, &set));
    VkDescriptorImageInfo dii = {.sampler = sampler.handle, .imageView = texture.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wds = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
        .dstBinding = plan ? plan->sampler_binding : 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii};
    vkUpdateDescriptorSets(vk->device, 1, &wds, 0, NULL);
    TRY(dm_vk_begin(vk, &command));
    VkClearValue clear = {.color = {.float32 = {0, 0, 0, 1}}};
    VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = render,
        .framebuffer = framebuffer, .renderArea = {.extent = {pixels->width, pixels->height}},
        .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer.handle, &offset);
    vkCmdBindIndexBuffer(command, index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(command, draw->index_count, 1, 0, 0, 0);
    vkCmdEndRenderPass(command);
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {pixels->width, pixels->height, 1}};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
    VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = readback.handle, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host, 0, NULL);
    result = dm_vk_submit(vk, command);
    command = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) goto done;
    TRY(dm_vk_buffer_sync(vk, &readback, 0));
    memcpy(pixels->bytes, readback.mapped, (size_t)out_bytes);
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    dm_vk_buffer_destroy(vk, &readback);
    dm_vk_buffer_destroy(vk, &index_buffer);
    dm_vk_buffer_destroy(vk, &vertex_buffer);
    if (pool) vkDestroyDescriptorPool(vk->device, pool, NULL);
    if (pipeline) vkDestroyPipeline(vk->device, pipeline, NULL);
    if (layout) vkDestroyPipelineLayout(vk->device, layout, NULL);
    if (dsl) vkDestroyDescriptorSetLayout(vk->device, dsl, NULL);
    if (framebuffer) vkDestroyFramebuffer(vk->device, framebuffer, NULL);
    if (render) vkDestroyRenderPass(vk->device, render, NULL);
    if (vertex_shader) vkDestroyShaderModule(vk->device, vertex_shader, NULL);
    if (fragment_shader) vkDestroyShaderModule(vk->device, fragment_shader, NULL);
    dm_vk_sampler_destroy(vk, &sampler);
    dm_vk_texture_destroy(vk, &texture);
    if (view) vkDestroyImageView(vk->device, view, NULL);
    if (image) vkDestroyImage(vk->device, image, NULL);
    if (image_memory) vkFreeMemory(vk->device, image_memory, NULL);
    return result;
}

VkResult dm_vk_draw_indexed_textured_offscreen(dm_vk *vk, const dm_vk_indexed_textured_draw *draw, dm_vk_pixels *pixels) {
    return draw_indexed_textured_impl(vk, draw, pixels, NULL);
}

VkResult dm_vk_draw_indexed_textured_plan_offscreen(dm_vk *vk, const dm_vk_indexed_textured_draw *draw,
                                                     const dm_vk_graphics_plan *plan, dm_vk_pixels *pixels) {
    char why[DM_VK_GRAPHICS_WHY] = {0};
    if (!plan || plan->kind != DM_VK_PLAN_TEXTURED || !dm_vk_graphics_plan_check(plan, why, sizeof why))
        return VK_ERROR_INITIALIZATION_FAILED;
    return draw_indexed_textured_impl(vk, draw, pixels, plan);
}
