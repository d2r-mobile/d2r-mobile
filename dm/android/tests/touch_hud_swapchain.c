// #199 HUD-to-swapchain actual renderer/swapchain seam. The existing
// swapchain Vulkan table from tests/vulkan_swapchain.c is included so both
// modules use the same narrow loader boundary; HUD geometry is a test double
// while the production builder remains covered by the offscreen HUD tests.
#define main dm_swapchain_existing_main
#include "vulkan_swapchain.c"
#undef main
#include "../runtime/touch_hud_swapchain.h"
#include <stdlib.h>

static int g_hud_draws, g_hud_binds, g_hud_pipeline_creates, g_hud_buffer_creates;
static int g_hud_shader_loads, g_hud_shader_created, g_hud_shader_destroys;
static int g_hud_pipeline_success, g_hud_pipeline_destroys;
static int g_hud_layout_created, g_hud_layout_destroys, g_hud_buffer_destroys;
static int g_fail_shader_at, g_fail_pipeline, g_fail_array, g_fail_buffer_at;
static int g_hud_array_calls;
static dm_vk_buffer g_hud_buffers[8];
static int g_hud_buffer_count;
static VkRenderPass g_hud_pass;
static VkFramebuffer g_hud_fb;
static VkCommandBuffer g_hud_command;

VKAPI_ATTR VkResult VKAPI_CALL test_vkCreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *i,
                                                          const VkAllocationCallbacks *a, VkPipelineLayout *o) {
    (void)d; (void)i; (void)a; *o = H(VkPipelineLayout, 0x9001); g_hud_layout_created++; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout o, const VkAllocationCallbacks *a) {
    (void)d; (void)o; (void)a; g_hud_layout_destroys++;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateGraphicsPipelines(VkDevice d, VkPipelineCache c, uint32_t n,
    const VkGraphicsPipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *o) {
    (void)d; (void)c; (void)a; g_hud_pipeline_creates++;
    if (g_fail_pipeline) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!n || !i || !i->pViewportState || i->pViewportState->viewportCount != 1 ||
        i->renderPass != g_hud_pass) return VK_ERROR_INITIALIZATION_FAILED;
    *o = H(VkPipeline, 0x9002); g_hud_pipeline_success++; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a) {
    (void)d; (void)p; (void)a; g_hud_pipeline_destroys++;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyShaderModule(VkDevice d, VkShaderModule m, const VkAllocationCallbacks *a) {
    (void)d; (void)m; (void)a; g_hud_shader_destroys++;
}
VKAPI_ATTR void VKAPI_CALL test_vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) {
    (void)c; (void)b; (void)p; g_hud_binds++;
}
VKAPI_ATTR void VKAPI_CALL test_vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t first, uint32_t n,
                                                       const VkBuffer *b, const VkDeviceSize *o) {
    (void)c; (void)first; (void)n; (void)b; (void)o; g_hud_binds++;
}
VKAPI_ATTR void VKAPI_CALL test_vkCmdSetViewport(VkCommandBuffer c, uint32_t first, uint32_t n,
                                                 const VkViewport *v) { (void)c; (void)first; (void)n; (void)v; }
VKAPI_ATTR void VKAPI_CALL test_vkCmdSetScissor(VkCommandBuffer c, uint32_t first, uint32_t n,
                                                const VkRect2D *r) { (void)c; (void)first; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL test_vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l,
    VkShaderStageFlags s, uint32_t o, uint32_t size, const void *d) {
    (void)c; (void)l; (void)s; (void)o; (void)size; (void)d;
}
VKAPI_ATTR void VKAPI_CALL test_vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i,
                                          uint32_t f, uint32_t ii) {
    (void)c; (void)v; (void)i; (void)f; (void)ii; g_hud_draws++;
}
#ifdef DM_HUD_TEST_MUTANT_NO_DRAW
VKAPI_ATTR void VKAPI_CALL mutant_vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i,
                                            uint32_t f, uint32_t ii) {
    (void)c; (void)v; (void)i; (void)f; (void)ii;
}
#endif

VkResult dm_vk_shader_load(dm_vk *vk, const char *path, VkShaderModule *out) {
    (void)vk; (void)path; g_hud_shader_loads++;
    if (g_fail_shader_at && g_hud_shader_loads == g_fail_shader_at)
        return VK_ERROR_INITIALIZATION_FAILED;
    *out = H(VkShaderModule, 0x9100); g_hud_shader_created++; return VK_SUCCESS;
}
VkResult dm_vk_buffer_create(dm_vk *vk, const dm_vk_buffer_info *info, dm_vk_buffer *out) {
    (void)vk; if (!info || !info->size || g_hud_buffer_count >= 8) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (g_fail_buffer_at && g_hud_buffer_creates == g_fail_buffer_at)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    out->handle = H(VkBuffer, 0x9200 + g_hud_buffer_count);
    out->memory = H(VkDeviceMemory, 0x9300 + g_hud_buffer_count);
    out->mapped = calloc(1, (size_t)info->size); out->size = info->size; out->coherent = 1;
    if (!out->mapped) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_hud_buffers[g_hud_buffer_count++] = *out; g_hud_buffer_creates++;
    return VK_SUCCESS;
}
void dm_vk_buffer_destroy(dm_vk *vk, dm_vk_buffer *buffer) {
    (void)vk; if (!buffer || !buffer->handle) return;
    g_hud_buffer_destroys++; free(buffer->mapped); memset(buffer, 0, sizeof *buffer);
}
VkResult dm_vk_buffer_sync(dm_vk *vk, dm_vk_buffer *buffer, int upload) {
    (void)vk; (void)buffer; (void)upload; return VK_SUCCESS;
}
void *hud_test_calloc(size_t count, size_t size) {
    g_hud_array_calls++;
    if (g_fail_array && g_hud_array_calls == 1) return NULL;
    void *p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

static void hud_reset(void) {
    for (int i = 0; i < 8; i++) memset(&g_hud_buffers[i], 0, sizeof g_hud_buffers[i]);
    g_hud_buffer_count = 0; g_hud_draws = g_hud_binds = g_hud_pipeline_creates = 0;
    g_hud_buffer_creates = 0; g_hud_pass = VK_NULL_HANDLE; g_hud_fb = VK_NULL_HANDLE;
    g_hud_command = VK_NULL_HANDLE;
    g_hud_shader_loads = g_hud_shader_created = g_hud_shader_destroys = 0;
    g_hud_pipeline_success = g_hud_pipeline_destroys = 0;
    g_hud_layout_destroys = g_hud_buffer_destroys = 0;
    g_hud_layout_created = 0;
    g_fail_shader_at = g_fail_pipeline = g_fail_array = g_fail_buffer_at = 0;
    g_hud_array_calls = 0;
}

static int hud_test(void) {
    dm_vk vk = fake_vk(); dm_vk_swapchain_options o = options(); dm_vk_swapchain sc = {0};
    dm_touch_hud_swapchain p = {0};
    dm_touch_hud_snapshot snap = {.width_pt = 640, .height_pt = 480, .density = 1,
        .pixel_width = 640, .pixel_height = 480, .enabled_mask = 1};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_hud_pass = sc.render_pass;
    CHECK(dm_touch_hud_swapchain_create(&vk, &sc, "v", "f", &p) == VK_SUCCESS);
    CHECK(p.ready && p.pipeline && p.vertex_buffers && p.image_count == sc.image_count);
    CHECK(g_hud_pipeline_creates == 1 && g_hud_buffer_creates == 3);
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, NULL) == VK_SUCCESS);
    g_hud_fb = sc.framebuffers[sc.image_index];
    g_hud_command = sc.commands[sc.image_index];
    snap.pixel_width = sc.extent.width - 1;
    CHECK(dm_touch_hud_swapchain_record(&vk, &sc, &p, &snap, 0) != VK_SUCCESS);
    CHECK(g_hud_draws == 0);
    snap.pixel_width = sc.extent.width;
    CHECK(dm_touch_hud_swapchain_record(&vk, &sc, &p, &snap, 0) == VK_SUCCESS);
    CHECK(g_hud_draws == 1 && g_hud_binds == 2 && g_hud_fb == sc.framebuffers[sc.image_index]);
    {
        const float *v = (const float *)p.vertex_buffers[sc.image_index].mapped;
        CHECK(v && v[0] == -1.0f && v[1] == -1.0f);
    }
    CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_present(&vk, &sc, NULL) == VK_SUCCESS);
    snap.pixel_width = 1;
    CHECK(dm_touch_hud_swapchain_record(&vk, &sc, &p, &snap, 0) != VK_SUCCESS);
    CHECK(dm_vk_swapchain_destroy(&vk, &sc) == VK_SUCCESS);
    dm_touch_hud_swapchain_destroy(&vk, &p);
    return 0;
}

static int hud_failure_case(int shader_fail, int pipeline_fail, int array_fail, int buffer_fail) {
    reset();
    hud_reset();
    dm_vk vk = fake_vk();
    dm_vk_swapchain_options o = options();
    dm_vk_swapchain sc = {0};
    dm_touch_hud_swapchain p = {0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_hud_pass = sc.render_pass;
    g_fail_shader_at = shader_fail;
    g_fail_pipeline = pipeline_fail;
    g_fail_array = array_fail;
    g_fail_buffer_at = buffer_fail;
    CHECK(dm_touch_hud_swapchain_create(&vk, &sc, "v", "f", &p) != VK_SUCCESS);
    CHECK(!p.ready && !p.vertex_buffers);
    CHECK(g_hud_shader_destroys == g_hud_shader_created);
    CHECK(g_hud_layout_destroys == g_hud_layout_created);
    CHECK(g_hud_pipeline_destroys == g_hud_pipeline_success);
    CHECK(g_hud_buffer_destroys == g_hud_buffer_creates);
    CHECK(dm_vk_swapchain_destroy(&vk, &sc) == VK_SUCCESS);
    return 0;
}

int main(void) {
    reset();
    hud_reset();
    if (hud_test()) return 1;
    if (hud_failure_case(1, 0, 0, 0)) return 1;
    if (hud_failure_case(2, 0, 0, 0)) return 1;
    if (hud_failure_case(0, 1, 0, 0)) return 1;
    if (hud_failure_case(0, 0, 1, 0)) return 1;
    if (hud_failure_case(0, 0, 0, 1)) return 1;
    printf("PASS hud swapchain: %d checks\n", checks);
    return failures ? 1 : 0;
}
