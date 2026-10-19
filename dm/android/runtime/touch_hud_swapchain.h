// Bounded owned HUD presenter: records accepted HUD geometry directly into the
// acquired swapchain render pass/framebuffer. Clear-only path remains default.
#pragma once
#include "touch_hud.h"
#include "vk_swapchain.h"

typedef struct {
    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkRenderPass render_pass;       // borrowed identity from the chain at create
    VkFormat format;                // borrowed identity from the chain at create
    dm_vk_buffer *vertex_buffers;   // one per swapchain image; host-visible
    dm_touch_hud_vertex *vertex_buffer_scratch;
    uint32_t image_count;
    uint32_t vertex_capacity;
    int ready;
} dm_touch_hud_swapchain;

// Creates shader/pipeline resources for this chain. Returns failure without
// retaining partial objects; the clear-only chain API is unchanged.
VkResult dm_touch_hud_swapchain_create(dm_vk *vk, const dm_vk_swapchain *chain,
                                       const char *vertex_spv, const char *fragment_spv,
                                       dm_touch_hud_swapchain *out);

// Rebuilds after chain recreate. Rejected while a frame is acquired/submitted.
VkResult dm_touch_hud_swapchain_reconfigure(dm_vk *vk, const dm_vk_swapchain *chain,
                                            const char *vertex_spv, const char *fragment_spv,
                                            dm_touch_hud_swapchain *presenter);

// Records the actual HUD draw into chain->commands[chain->image_index], using
// the matching acquired framebuffer and a per-image vertex buffer. The caller
// then uses the existing dm_vk_swapchain_submit/present path.
VkResult dm_touch_hud_swapchain_record(dm_vk *vk, const dm_vk_swapchain *chain,
                                       dm_touch_hud_swapchain *presenter,
                                       const dm_touch_hud_snapshot *snapshot, int overlay);

// Call only after the chain has no acquired/submitted frame and its present
// fences have completed (the accepted destroy/recreate path provides that).
void dm_touch_hud_swapchain_destroy(dm_vk *vk, dm_touch_hud_swapchain *presenter);
