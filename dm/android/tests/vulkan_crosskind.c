// #133 交叉 kind 预检：有效的 textured plan 不能进入 colored consumer，反之亦然；两者都必须在任何
// Vulkan format query 前返回失败，画布保持不变。此测试只替换 format-query 计数探针，不调用 GPU。
// #133 cross-kind preflight: a valid textured plan must not enter the colored consumer (and vice versa); both must
// fail before any Vulkan format query with unchanged canvas. Only the format-query counter is replaced; no GPU call.
#include "../vulkan/vk_indexed_textured.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int queries;
void dm_test_probe_format_properties(VkPhysicalDevice p, VkFormat f, VkFormatProperties *out) {
    (void)p; (void)f; queries++; *out = (VkFormatProperties){0};
}

static void set_limits(dm_vk *v) {
    memset(v, 0, sizeof *v);
    v->device = (VkDevice)(uintptr_t)1;
    VkPhysicalDeviceLimits *l = &v->properties.limits;
    l->maxStorageBufferRange = 65536;
    l->maxImageDimension2D = 64;
    l->maxViewportDimensions[0] = l->maxViewportDimensions[1] = 64;
    l->maxFramebufferWidth = l->maxFramebufferHeight = 64;
    l->maxVertexInputBindings = 1;
    l->maxVertexInputAttributes = 2;
}
static int expect_clean(const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) if (bytes[i] != 0x5a) { fprintf(stderr, "FAIL: canvas changed at %zu\n", i); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s colored.vert colored.frag textured.vert textured.frag\n", argv[0]);
        return 2;
    }
    dm_vk_graphics_plan colored, textured;
    char why[DM_VK_GRAPHICS_WHY] = {0};
    if (dm_vk_graphics_plan_load(argv[1], argv[2], NULL, &colored, why, sizeof why) != VK_SUCCESS) return 3;
    if (dm_vk_textured_plan_load(argv[3], argv[4], NULL, &textured, why, sizeof why) != VK_SUCCESS) return 3;
    dm_vk vk; set_limits(&vk);
    uint8_t output[16 * 16 * 4];
    memset(output, 0x5a, sizeof output);
    dm_vk_pixels pixels = {.width = 16, .height = 16, .bytes = output, .capacity = sizeof output};

    // valid colored draw, textured plan -> colored wrapper must reject before format query.
    dm_vk_indexed_vertex cv[3] = {{{0, 0}, {1, 0, 0, 1}}, {{1, 0}, {1, 0, 0, 1}}, {{0, 1}, {1, 0, 0, 1}}};
    uint16_t ci[3] = {0, 1, 2};
    dm_vk_indexed_draw cd = {.vertices = cv, .vertex_count = 3, .vertex_stride = DM_VK_INDEXED_VERTEX_STRIDE,
        .indices = ci, .index_count = 3};
    queries = 0;
    VkResult r1 = dm_vk_draw_indexed_plan_offscreen(&vk, &cd, &textured, &pixels);
    printf("textured->colored r=%d queries=%d\n", (int)r1, queries);
    int fail = 0;
    if (r1 != VK_ERROR_INITIALIZATION_FAILED || queries != 0) { fprintf(stderr, "FAIL textured->colored guard\n"); fail = 1; }
    fail |= expect_clean(output, sizeof output);

    // valid textured draw, colored plan -> textured wrapper must reject before format query.
    dm_vk_indexed_textured_vertex tv[4] = {
        {{-0.8f, -0.8f}, {0, 0}}, {{0.8f, -0.8f}, {1, 0}},
        {{0.8f, 0.8f}, {1, 1}}, {{-0.8f, 0.8f}, {0, 1}},
    };
    uint16_t ti[6] = {0, 1, 2, 0, 2, 3};
    uint8_t tex[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
    dm_vk_indexed_textured_draw td = {.vertices = tv, .vertex_count = 4,
        .vertex_stride = DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE, .indices = ti, .index_count = 6,
        .texture_pixels = tex, .texture_size = sizeof tex, .texture_width = 2, .texture_height = 2};
    queries = 0;
    VkResult r2 = dm_vk_draw_indexed_textured_plan_offscreen(&vk, &td, &colored, &pixels);
    printf("colored->textured r=%d queries=%d\n", (int)r2, queries);
    if (r2 != VK_ERROR_INITIALIZATION_FAILED || queries != 0) { fprintf(stderr, "FAIL colored->textured guard\n"); fail = 1; }
    fail |= expect_clean(output, sizeof output);

    dm_vk_graphics_plan_free(&colored);
    dm_vk_graphics_plan_free(&textured);
    return fail;
}
