// #213 scene+HUD in one acquired swapchain frame. This includes the accepted
// HUD test's narrow Vulkan table and adds only the scene resources/commands.
#define DM_SCENE_TEST_INCLUDE 1
#include "touch_hud_swapchain.c"
#undef main
#include "../app/diagnostic_scene_swapchain.h"

static int g_scene_draws, g_scene_descriptors, g_scene_texture_destroys;
static int g_pass_begins, g_pass_clear_loadops;
static int g_fail_scene_texture;
static char g_scene_order[16];
static unsigned g_scene_order_n;

static void scene_event(char c) {
    if (g_scene_order_n + 1 < sizeof g_scene_order) {
        g_scene_order[g_scene_order_n++] = c;
        g_scene_order[g_scene_order_n] = 0;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateDescriptorSetLayout(VkDevice d,
    const VkDescriptorSetLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorSetLayout *o) {
    (void)d; (void)a; CHECK(i && i->bindingCount == 1 && i->pBindings[0].binding == 0);
    *o = H(VkDescriptorSetLayout, 0xa001); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout o,
    const VkAllocationCallbacks *a) { (void)d; (void)o; (void)a; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateDescriptorPool(VkDevice d,
    const VkDescriptorPoolCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorPool *o) {
    (void)d; (void)i; (void)a; *o = H(VkDescriptorPool, 0xa002); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyDescriptorPool(VkDevice d, VkDescriptorPool o,
    const VkAllocationCallbacks *a) { (void)d; (void)o; (void)a; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkAllocateDescriptorSets(VkDevice d,
    const VkDescriptorSetAllocateInfo *i, VkDescriptorSet *o) {
    (void)d; (void)i; *o = H(VkDescriptorSet, 0xa003); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkUpdateDescriptorSets(VkDevice d, uint32_t n,
    const VkWriteDescriptorSet *w, uint32_t c, const VkCopyDescriptorSet *cp) {
    (void)d; (void)c; (void)cp; CHECK(n == 1 && w && w->dstBinding == 0); g_scene_descriptors++;
}
VKAPI_ATTR void VKAPI_CALL test_vkCmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b,
    VkPipelineLayout l, uint32_t first, uint32_t n, const VkDescriptorSet *sets,
    uint32_t dyn, const uint32_t *offsets) {
    (void)c; (void)b; (void)l; (void)first; (void)n; (void)sets; (void)dyn; (void)offsets;
}
VKAPI_ATTR void VKAPI_CALL test_vkCmdBindIndexBuffer(VkCommandBuffer c, VkBuffer b,
    VkDeviceSize o, VkIndexType t) { (void)c; (void)b; (void)o; (void)t; }
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer c, uint32_t n, uint32_t i,
    uint32_t f, int32_t v, uint32_t fi) {
    (void)c; (void)n; (void)i; (void)f; (void)v; (void)fi;
#ifndef DM_SCENE_MUTANT_NO_DRAW
    g_scene_draws++; scene_event('S');
#endif
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c,
    const VkRenderPassBeginInfo *i, VkSubpassContents s) {
    (void)c; (void)s; (void)i; g_pass_begins++;
#ifndef DM_SCENE_MUTANT_NO_CLEAR
    if (i && i->clearValueCount == 1 && i->pClearValues) g_pass_clear_loadops++;
#endif
}
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i,
    uint32_t f, uint32_t ii) {
    (void)c; (void)v; (void)i; (void)f; (void)ii; g_hud_draws++; scene_event('H');
}

VkResult dm_vk_texture_upload_mips(dm_vk *vk, uint32_t w, uint32_t h, uint32_t levels,
    const dm_vk_mip_level *level, dm_vk_texture *out) {
    (void)vk; (void)w; (void)h; (void)levels; (void)level;
    if (g_fail_scene_texture) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    out->image = H(VkImage, 0xb001); out->view = H(VkImageView, 0xb002);
    out->width = w; out->height = h; out->levels = 1; return VK_SUCCESS;
}
void dm_vk_texture_destroy(dm_vk *vk, dm_vk_texture *t) {
    (void)vk; if (t && t->image) g_scene_texture_destroys++; memset(t, 0, sizeof *t);
}
VkResult dm_vk_sampler_create(dm_vk *vk, const dm_vk_sampler_desc *d, dm_vk_sampler *out) {
    (void)vk; (void)d; out->handle = H(VkSampler, 0xb003); return VK_SUCCESS;
}
void dm_vk_sampler_destroy(dm_vk *vk, dm_vk_sampler *s) {
    (void)vk; if (s) memset(s, 0, sizeof *s);
}
static void scene_reset(void) {
    g_scene_draws = g_scene_descriptors = g_scene_texture_destroys = 0;
    g_pass_begins = g_pass_clear_loadops = 0;
    g_fail_scene_texture = 0; g_scene_order_n = 0; g_scene_order[0] = 0;
}

static int scene_hud_test(void) {
    reset(); hud_reset(); scene_reset();
    dm_vk vk = fake_vk();
    dm_vk_swapchain_options o = options();
    dm_vk_swapchain sc = {0};
    dm_touch_hud_swapchain hud = {0};
    dm_diag_scene_swapchain scene = {0};
    dm_touch_hud_snapshot snap = {.width_pt = 640, .height_pt = 480, .density = 1,
        .pixel_width = 640, .pixel_height = 480, .enabled_mask = 1};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_hud_pass = sc.render_pass;
    CHECK(dm_touch_hud_swapchain_create(&vk, &sc, "hud-v", "hud-f", &hud) == VK_SUCCESS);
    CHECK(dm_diag_scene_swapchain_create(&vk, &sc, "scene-v", "scene-f", &scene) == VK_SUCCESS);
    CHECK(scene.ready && scene.pipeline && scene.vertices.handle && scene.indices.handle);
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, NULL) == VK_SUCCESS);
    g_pass_begins = g_pass_clear_loadops = 0;
    g_scene_draws = g_hud_draws = 0;
    g_hud_fb = sc.framebuffers[sc.image_index];
    snap.pixel_width = sc.extent.width - 1;
    CHECK(dm_diag_scene_swapchain_record(&vk, &sc, &scene, &snap, &hud, 0) != VK_SUCCESS);
    CHECK(g_scene_draws == 0 && g_hud_draws == 0);
    snap.pixel_width = sc.extent.width;
    CHECK(dm_diag_scene_swapchain_record(&vk, &sc, &scene, &snap, &hud, 0) == VK_SUCCESS);
    CHECK(g_scene_draws == 1 && g_hud_draws == 1);
    CHECK(g_pass_begins == 1 && g_pass_clear_loadops == 1);
    CHECK(strcmp(g_scene_order, "SH") == 0);
    CHECK(g_scene_descriptors == 1);
    CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_present(&vk, &sc, NULL) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_destroy(&vk, &sc) == VK_SUCCESS);
    dm_diag_scene_swapchain_destroy(&vk, &scene);
    dm_touch_hud_swapchain_destroy(&vk, &hud);
    CHECK(g_scene_texture_destroys == 1);
    return 0;
}

static int scene_init_failure(void) {
    reset(); hud_reset(); scene_reset();
    dm_vk vk = fake_vk();
    dm_vk_swapchain_options o = options();
    dm_vk_swapchain sc = {0};
    dm_diag_scene_swapchain scene = {0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_hud_pass = sc.render_pass;
    g_fail_scene_texture = 1;
    CHECK(dm_diag_scene_swapchain_create(&vk, &sc, "v", "f", &scene) != VK_SUCCESS);
    CHECK(!scene.ready && g_scene_texture_destroys == 0);
    CHECK(dm_vk_swapchain_destroy(&vk, &sc) == VK_SUCCESS);
    return 0;
}

int scene_hud_main(void) {
    if (scene_hud_test()) return 1;
    if (scene_init_failure()) return 1;
    printf("PASS scene hud swapchain: %d checks\n", checks);
    return failures ? 1 : 0;
}
int main(void) { return scene_hud_main(); }
