// Bounded owned HUD→swapchain presenter. It records into the acquired chain's
// existing render pass/framebuffer and command buffer; it does not own WSI
// state or replace the clear-only default.
#include "touch_hud_swapchain.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef DM_TOUCH_HUD_TEST_CALLOC
#define DM_TOUCH_HUD_TEST_CALLOC calloc
#else
void *DM_TOUCH_HUD_TEST_CALLOC(size_t count, size_t size);
#endif

static int hud_chain_matches(const dm_vk_swapchain *chain, const dm_touch_hud_swapchain *p)
{
    return chain && p && chain->render_pass && chain->format &&
           chain->render_pass == p->render_pass && chain->format == p->format &&
           chain->image_count && chain->image_count == p->image_count;
}

static void reset_out(dm_touch_hud_swapchain *p)
{
    if (p) memset(p, 0, sizeof *p);
}

static void destroy_owned(dm_vk *vk, dm_touch_hud_swapchain *p)
{
    if (!p) return;
    if (vk && vk->device) {
        for (uint32_t i = 0; p->vertex_buffers && i < p->image_count; i++)
            dm_vk_buffer_destroy(vk, &p->vertex_buffers[i]);
        if (p->pipeline) vkDestroyPipeline(vk->device, p->pipeline, NULL);
        if (p->pipeline_layout) vkDestroyPipelineLayout(vk->device, p->pipeline_layout, NULL);
        if (p->vertex_shader) vkDestroyShaderModule(vk->device, p->vertex_shader, NULL);
        if (p->fragment_shader) vkDestroyShaderModule(vk->device, p->fragment_shader, NULL);
    }
    free(p->vertex_buffers);
    free(p->vertex_buffer_scratch);
    memset(p, 0, sizeof *p);
}

VkResult dm_touch_hud_swapchain_create(dm_vk *vk, const dm_vk_swapchain *chain,
                                       const char *vertex_spv, const char *fragment_spv,
                                       dm_touch_hud_swapchain *out)
{
    if (!vk || !vk->device || !chain || !chain->handle || !chain->render_pass ||
        !chain->image_count || !chain->extent.width || !chain->extent.height ||
        !vertex_spv || !fragment_spv || !out) return VK_ERROR_INITIALIZATION_FAILED;
    reset_out(out);
    out->render_pass = chain->render_pass;
    out->format = chain->format;
    out->image_count = chain->image_count;
    out->vertex_capacity = DM_TOUCH_HUD_VERTEX_CAP;

    VkResult result = dm_vk_shader_load(vk, vertex_spv, &out->vertex_shader);
    if (result != VK_SUCCESS) goto fail;
    result = dm_vk_shader_load(vk, fragment_spv, &out->fragment_shader);
    if (result != VK_SUCCESS) goto fail;

    VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = 8};
    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
    result = vkCreatePipelineLayout(vk->device, &layout_info, NULL, &out->pipeline_layout);
    if (result != VK_SUCCESS) goto fail;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = out->vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = out->fragment_shader, .pName = "main"}};
    VkVertexInputBindingDescription binding = {.binding = 0,
        .stride = sizeof(dm_touch_hud_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = (uint32_t)offsetof(dm_touch_hud_vertex, x)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (uint32_t)offsetof(dm_touch_hud_vertex, r)}};
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
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
        .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend};
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states};
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport_state,
        .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend, .pDynamicState = &dynamic,
        .layout = out->pipeline_layout, .renderPass = chain->render_pass,
        .basePipelineIndex = -1};
    result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipeline_info,
                                        NULL, &out->pipeline);
    if (result != VK_SUCCESS) goto fail;

    out->vertex_buffers = DM_TOUCH_HUD_TEST_CALLOC(out->image_count, sizeof(*out->vertex_buffers));
    if (!out->vertex_buffers) { result = VK_ERROR_OUT_OF_HOST_MEMORY; goto fail; }
    dm_vk_buffer_info info = {.size = (VkDeviceSize)out->vertex_capacity * sizeof(dm_touch_hud_vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
    for (uint32_t i = 0; i < out->image_count; i++) {
        result = dm_vk_buffer_create(vk, &info, &out->vertex_buffers[i]);
        if (result != VK_SUCCESS) goto fail;
    }
    out->ready = 1;
    return VK_SUCCESS;

fail:
    destroy_owned(vk, out);
    return result;
}

VkResult dm_touch_hud_swapchain_reconfigure(dm_vk *vk, const dm_vk_swapchain *chain,
                                            const char *vertex_spv, const char *fragment_spv,
                                            dm_touch_hud_swapchain *presenter)
{
    if (!vk || !chain || !presenter) return VK_ERROR_INITIALIZATION_FAILED;
    if (chain->acquired || chain->submitted || chain->acquire_pending || chain->fence_pending)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < chain->image_count; i++)
        if (chain->present_pending && chain->present_pending[i])
            return VK_ERROR_INITIALIZATION_FAILED;
    destroy_owned(vk, presenter);
    return dm_touch_hud_swapchain_create(vk, chain, vertex_spv, fragment_spv, presenter);
}

VkResult dm_touch_hud_swapchain_record(dm_vk *vk, const dm_vk_swapchain *chain,
                                       dm_touch_hud_swapchain *presenter,
                                       const dm_touch_hud_snapshot *snapshot, int overlay)
{
    if (!vk || !vk->device || !chain || !presenter || !snapshot || !presenter->ready ||
        !chain->acquired || chain->submitted || !chain->commands || !chain->framebuffers ||
        chain->image_index >= chain->image_count || !hud_chain_matches(chain, presenter))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (snapshot->pixel_width != chain->extent.width ||
        snapshot->pixel_height != chain->extent.height ||
        !dm_touch_hud_validate(snapshot)) return VK_ERROR_INITIALIZATION_FAILED;
    if (overlay < 0 || overlay > 1) return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t count = 0;
    dm_touch_hud_vertex *vertices = presenter->vertex_buffer_scratch;
    if (!vertices) {
        vertices = calloc(presenter->vertex_capacity, sizeof(*vertices));
        if (!vertices) return VK_ERROR_OUT_OF_HOST_MEMORY;
        presenter->vertex_buffer_scratch = vertices;
    }
    if (!dm_touch_hud_build_vertices(snapshot, overlay, vertices, presenter->vertex_capacity, &count) ||
        !count) return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t image = chain->image_index;
    dm_vk_buffer *buffer = &presenter->vertex_buffers[image];
    if (!buffer->mapped || count * sizeof(*vertices) > buffer->size)
        return VK_ERROR_INITIALIZATION_FAILED;
    memcpy(buffer->mapped, vertices, (size_t)count * sizeof(*vertices));
    VkResult result = dm_vk_buffer_sync(vk, buffer, 1);
    if (result != VK_SUCCESS) return result;

    VkCommandBuffer command = chain->commands[image];
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    result = vkBeginCommandBuffer(command, &begin);
    if (result != VK_SUCCESS) return result;
    VkClearValue clear = {.color = chain->clear};
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = chain->render_pass, .framebuffer = chain->framebuffers[image],
        .renderArea = {{0, 0}, chain->extent}, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, presenter->pipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer->handle, &offset);
    VkViewport viewport = {.width = (float)chain->extent.width,
        .height = (float)chain->extent.height, .maxDepth = 1.0f};
    VkRect2D scissor = {.extent = chain->extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    float dims[2] = {(float)chain->extent.width, (float)chain->extent.height};
    vkCmdPushConstants(command, presenter->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof dims, dims);
    vkCmdDraw(command, count, 1, 0, 0);
    vkCmdEndRenderPass(command);
    return vkEndCommandBuffer(command);
}

void dm_touch_hud_swapchain_destroy(dm_vk *vk, dm_touch_hud_swapchain *presenter)
{
    destroy_owned(vk, presenter);
}
