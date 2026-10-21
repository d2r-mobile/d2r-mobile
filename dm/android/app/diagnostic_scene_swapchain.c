// #213 scene+HUD in the acquired swapchain frame. The scene data and shaders
// are the accepted owned diagnostic ones; this module only adapts them to the
// chain render pass and owns the GPU resources for their lifetime.
#include "diagnostic_scene_swapchain.h"
#include "vk_sampler.h"

#include <stddef.h>
#include <string.h>

static void reset_out(dm_diag_scene_swapchain *s)
{
    if (s) memset(s, 0, sizeof *s);
}

static void destroy_owned(dm_vk *vk, dm_diag_scene_swapchain *s)
{
    if (!s) return;
    if (vk && vk->device) {
        dm_vk_buffer_destroy(vk, &s->indices);
        dm_vk_buffer_destroy(vk, &s->vertices);
        if (s->descriptor_pool) vkDestroyDescriptorPool(vk->device, s->descriptor_pool, NULL);
        if (s->pipeline) vkDestroyPipeline(vk->device, s->pipeline, NULL);
        if (s->pipeline_layout) vkDestroyPipelineLayout(vk->device, s->pipeline_layout, NULL);
        if (s->set_layout) vkDestroyDescriptorSetLayout(vk->device, s->set_layout, NULL);
        if (s->vertex_shader) vkDestroyShaderModule(vk->device, s->vertex_shader, NULL);
        if (s->fragment_shader) vkDestroyShaderModule(vk->device, s->fragment_shader, NULL);
    }
    dm_vk_sampler_destroy(vk, &s->sampler);
    dm_vk_texture_destroy(vk, &s->texture);
    memset(s, 0, sizeof *s);
}

static int chain_matches(const dm_vk_swapchain *chain, const dm_diag_scene_swapchain *s)
{
    return chain && s && chain->render_pass && chain->format &&
           chain->render_pass == s->render_pass && chain->format == s->format &&
           chain->image_count && chain->image_count == s->image_count;
}

VkResult dm_diag_scene_swapchain_create(dm_vk *vk, const dm_vk_swapchain *chain,
                                        const char *vertex_spv, const char *fragment_spv,
                                        dm_diag_scene_swapchain *out)
{
    const dm_diag_scene *scene = dm_diag_scene_owned();
    if (!vk || !vk->device || !chain || !chain->handle || !chain->render_pass ||
        !chain->image_count || !chain->extent.width || !chain->extent.height ||
        !scene || !vertex_spv || !fragment_spv || !out)
        return VK_ERROR_INITIALIZATION_FAILED;
    reset_out(out);
    out->render_pass = chain->render_pass;
    out->format = chain->format;
    out->image_count = chain->image_count;

    VkResult result = dm_vk_shader_load(vk, vertex_spv, &out->vertex_shader);
    if (result != VK_SUCCESS) goto fail;
    result = dm_vk_shader_load(vk, fragment_spv, &out->fragment_shader);
    if (result != VK_SUCCESS) goto fail;

    dm_vk_mip_level level = {.bytes = scene->texture, .size = scene->texture_size,
        .row_bytes = scene->texture_width * 4u};
    result = dm_vk_texture_upload_mips(vk, scene->texture_width, scene->texture_height,
                                       1, &level, &out->texture);
    if (result != VK_SUCCESS) goto fail;
    dm_vk_sampler_desc sampler_desc = {
        .min_filter = DM_VK_FILTER_NEAREST, .mag_filter = DM_VK_FILTER_NEAREST,
        .mipmap = DM_VK_MIP_NEAREST, .address_u = DM_VK_ADDRESS_CLAMP_TO_EDGE,
        .address_v = DM_VK_ADDRESS_CLAMP_TO_EDGE, .address_w = DM_VK_ADDRESS_CLAMP_TO_EDGE,
        .lod_min = 0.0f, .lod_max = 0.0f, .lod_bias = 0.0f, .max_aniso = 1.0f,
        .compare_enable = 0};
    result = dm_vk_sampler_create(vk, &sampler_desc, &out->sampler);
    if (result != VK_SUCCESS) goto fail;

    dm_vk_buffer_info vbi = {.size = (VkDeviceSize)scene->vertex_count * sizeof(*scene->vertices),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
    dm_vk_buffer_info ibi = {.size = (VkDeviceSize)scene->index_count * sizeof(*scene->indices),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    result = dm_vk_buffer_create(vk, &vbi, &out->vertices);
    if (result != VK_SUCCESS) goto fail;
    result = dm_vk_buffer_create(vk, &ibi, &out->indices);
    if (result != VK_SUCCESS) goto fail;
    if (!out->vertices.mapped || !out->indices.mapped) { result = VK_ERROR_OUT_OF_HOST_MEMORY; goto fail; }
    memcpy(out->vertices.mapped, scene->vertices, vbi.size);
    memcpy(out->indices.mapped, scene->indices, ibi.size);
    result = dm_vk_buffer_sync(vk, &out->vertices, 1);
    if (result != VK_SUCCESS) goto fail;
    result = dm_vk_buffer_sync(vk, &out->indices, 1);
    if (result != VK_SUCCESS) goto fail;

    VkDescriptorSetLayoutBinding binding = {.binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dsl_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    result = vkCreateDescriptorSetLayout(vk->device, &dsl_info, NULL, &out->set_layout);
    if (result != VK_SUCCESS) goto fail;
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &out->set_layout};
    result = vkCreatePipelineLayout(vk->device, &layout_info, NULL, &out->pipeline_layout);
    if (result != VK_SUCCESS) goto fail;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = out->vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = out->fragment_shader, .pName = "main"}};
    VkVertexInputBindingDescription vb = {.binding = 0,
        .stride = sizeof(dm_diag_scene_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = (uint32_t)offsetof(dm_diag_scene_vertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = (uint32_t)offsetof(dm_diag_scene_vertex, uv)}};
    VkPipelineVertexInputStateCreateInfo input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs};
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.width = (float)chain->extent.width,
        .height = (float)chain->extent.height, .maxDepth = 1.0f};
    VkRect2D scissor = {.extent = chain->extent};
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend};
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states};
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport_state,
        .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend, .pDynamicState = &dynamic,
        .layout = out->pipeline_layout, .renderPass = chain->render_pass,
        .basePipelineIndex = -1};
    result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeline_info,
                                       NULL, &out->pipeline);
    if (result != VK_SUCCESS) goto fail;

    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
    result = vkCreateDescriptorPool(vk->device, &pool_info, NULL, &out->descriptor_pool);
    if (result != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = out->descriptor_pool, .descriptorSetCount = 1,
        .pSetLayouts = &out->set_layout};
    result = vkAllocateDescriptorSets(vk->device, &alloc_info, &out->descriptor_set);
    if (result != VK_SUCCESS) goto fail;
    VkDescriptorImageInfo image_info = {.sampler = out->sampler.handle,
        .imageView = out->texture.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = out->descriptor_set, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info};
    vkUpdateDescriptorSets(vk->device, 1, &write, 0, NULL);
    out->ready = 1;
    return VK_SUCCESS;

fail:
    destroy_owned(vk, out);
    return result;
}

VkResult dm_diag_scene_swapchain_record(dm_vk *vk, const dm_vk_swapchain *chain,
                                        dm_diag_scene_swapchain *scene,
                                        const dm_touch_hud_snapshot *hud_snapshot,
                                        dm_touch_hud_swapchain *hud, int hud_overlay)
{
    const dm_diag_scene *data = dm_diag_scene_owned();
    if (!vk || !vk->device || !chain || !scene || !data || !scene->ready ||
        !chain->acquired || chain->submitted || !chain->commands || !chain->framebuffers ||
        chain->image_index >= chain->image_count || !chain_matches(chain, scene))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (hud && (!hud_snapshot || hud_overlay < 0 || hud_overlay > 1 ||
        hud_snapshot->pixel_width != chain->extent.width ||
        hud_snapshot->pixel_height != chain->extent.height))
        return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t image = chain->image_index;
    VkCommandBuffer command = chain->commands[image];
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VkResult result = vkBeginCommandBuffer(command, &begin);
    if (result != VK_SUCCESS) return result;
    VkClearValue clear = {.color = chain->clear};
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = chain->render_pass, .framebuffer = chain->framebuffers[image],
        .renderArea = {{0, 0}, chain->extent}, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipeline_layout,
                            0, 1, &scene->descriptor_set, 0, NULL);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &scene->vertices.handle, &offset);
    vkCmdBindIndexBuffer(command, scene->indices.handle, 0, VK_INDEX_TYPE_UINT16);
    VkViewport viewport = {.width = (float)chain->extent.width,
        .height = (float)chain->extent.height, .maxDepth = 1.0f};
    VkRect2D scissor = {.extent = chain->extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdDrawIndexed(command, data->index_count, 1, 0, 0, 0);
    if (hud) {
        result = dm_touch_hud_swapchain_record_in_pass(vk, chain, hud, hud_snapshot, hud_overlay);
        if (result != VK_SUCCESS) {
            vkCmdEndRenderPass(command);
            (void)vkEndCommandBuffer(command);
            return result;
        }
    }
    vkCmdEndRenderPass(command);
    return vkEndCommandBuffer(command);
}

void dm_diag_scene_swapchain_destroy(dm_vk *vk, dm_diag_scene_swapchain *scene)
{
    destroy_owned(vk, scene);
}
