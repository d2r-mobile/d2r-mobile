// #213 owned indexed-textured diagnostic scene recorded directly into the
// acquired WSI render pass, followed by the accepted HUD draw in the same pass.
#pragma once
#include "diagnostic_scene.h"
#include "touch_hud_swapchain.h"
#include "vk_sampler.h"
#include "vk_swapchain.h"

typedef struct {
    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    dm_vk_texture texture;
    dm_vk_sampler sampler;
    dm_vk_buffer vertices;
    dm_vk_buffer indices;
    VkRenderPass render_pass;   // borrowed chain identity
    VkFormat format;            // borrowed chain identity
    uint32_t image_count;
    int ready;
} dm_diag_scene_swapchain;

// Creates the scene pipeline/texture/buffers once for the current chain. All
// resources are owned here; partial failure is rolled back and the clear-only
// chain API is unchanged.
VkResult dm_diag_scene_swapchain_create(dm_vk *vk, const dm_vk_swapchain *chain,
                                        const char *vertex_spv, const char *fragment_spv,
                                        dm_diag_scene_swapchain *out);

// Begins one acquired command buffer, clears once, records the actual owned
// indexed scene, then appends the HUD draw into the same render pass. No
// readback, CPU copy, or per-frame device wait. If HUD is non-NULL, its snapshot
// must match the current extent; a record failure leaves the frame unsubmitted.
VkResult dm_diag_scene_swapchain_record(dm_vk *vk, const dm_vk_swapchain *chain,
                                        dm_diag_scene_swapchain *scene,
                                        const dm_touch_hud_snapshot *hud_snapshot,
                                        dm_touch_hud_swapchain *hud, int hud_overlay);

// Call only after the chain has no in-flight frame and its presentation fences
// have completed.
void dm_diag_scene_swapchain_destroy(dm_vk *vk, dm_diag_scene_swapchain *scene);
