// #115 indexed+texture 离屏绘制验收：固定 float2+float2、uint16 index、单个 RGBA8 纹理、nearest/clamp；
// 独立非均匀纹理像素 oracle、改纹理/UV 负控、重绘/改尺寸及预检负例。
// #115 indexed+textured offscreen acceptance: fixed float2+float2, uint16 indices, one RGBA8 texture, nearest/clamp;
// independent nonuniform texture pixel oracle, changed-texture/UV controls, redraw/resize and preflight negatives.
#include "../vulkan/vk_indexed_textured.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static const uint8_t SENTINEL = 0x5a;

static const uint8_t *pixel(const dm_vk_pixels *p, uint32_t x, uint32_t y) {
    return p->bytes + ((size_t)y * p->width + x) * 4;
}
static void expect_rgba(const dm_vk_pixels *p, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int line) {
    const uint8_t *q = pixel(p, x, y);
    checks++;
    if (!(q[0] == r && q[1] == g && q[2] == b && q[3] == a)) {
        failures++;
        fprintf(stderr, "FAIL %d: pixel(%u,%u)=%u,%u,%u,%u expected %u,%u,%u,%u\n",
                line, x, y, q[0], q[1], q[2], q[3], r, g, b, a);
    }
}

static dm_vk_indexed_textured_vertex verts[] = {
    {{-0.8f, -0.8f}, {0, 0}}, {{0.8f, -0.8f}, {1, 0}},
    {{0.8f, 0.8f}, {1, 1}}, {{-0.8f, 0.8f}, {0, 1}},
};
static dm_vk_indexed_textured_vertex uvs_constant[4];
static const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
static const uint8_t texA[16] = {
    255, 0, 0, 255,   0, 255, 0, 255,
    0, 0, 255, 255,   255, 255, 0, 255,
};
static const uint8_t texB[16] = {
    0, 255, 255, 255,   255, 0, 255, 255,
    255, 255, 255, 255,   128, 64, 32, 255,
};

static dm_vk_indexed_textured_draw make_draw(const uint8_t *tex, size_t tex_size) {
    dm_vk_indexed_textured_draw d = {.vertices = verts, .vertex_count = 4,
        .vertex_stride = DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE, .indices = indices, .index_count = 6,
        .texture_pixels = tex, .texture_size = tex_size, .texture_width = 2, .texture_height = 2};
    return d;
}

static void check_reject(dm_vk *vk, dm_vk_indexed_textured_draw *d, dm_vk_pixels *p, int line) {
    memset(p->bytes, SENTINEL, p->capacity);
    checks++;
    VkResult r = dm_vk_draw_indexed_textured_offscreen(vk, d, p);
    if (r == VK_SUCCESS) { failures++; fprintf(stderr, "FAIL %d: expected reject\n", line); }
    for (size_t i = 0; i < p->capacity; i++)
        if (p->bytes[i] != SENTINEL) {
            failures++; fprintf(stderr, "FAIL %d: output modified at byte %zu\n", line, i); break;
        }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s indexed_textured.vert.spv indexed_textured.frag.spv\n", argv[0]); return 2; }
    dm_vk vk = {0};
    VkResult created = dm_vk_create(&vk);
    if (created != VK_SUCCESS) {
        printf("vulkan_indexed_textured: no Vulkan device; SKIP\n");
        return 77;
    }
    uint8_t output[16 * 16 * 4];
    dm_vk_pixels target = {.width = 16, .height = 16, .bytes = output, .capacity = sizeof output};
    dm_vk_indexed_textured_draw draw = make_draw(texA, sizeof texA);
    draw.vertex_spv = argv[1]; draw.fragment_spv = argv[2];
    memset(output, SENTINEL, sizeof output);
    CHECK(dm_vk_draw_indexed_textured_offscreen(&vk, &draw, &target) == VK_SUCCESS);
    expect_rgba(&target, 5, 5, 255, 0, 0, 255, __LINE__);       // texel (0,0) red
    expect_rgba(&target, 11, 5, 0, 255, 0, 255, __LINE__);      // texel (1,0) green
    expect_rgba(&target, 5, 11, 0, 0, 255, 255, __LINE__);      // texel (0,1) blue
    expect_rgba(&target, 11, 11, 255, 255, 0, 255, __LINE__);   // texel (1,1) yellow
    expect_rgba(&target, 0, 0, 0, 0, 0, 255, __LINE__);         // clear background
    for (size_t i = 0; i < sizeof texA; i++) CHECK(texA[i] == ((const uint8_t *)draw.texture_pixels)[i]);

    // 改纹理后同一 target 必须使用新像素，旧红/绿/蓝/黄不能残留。
    // Changed texture on the same target must use new pixels; no stale red/green/blue/yellow.
    dm_vk_indexed_textured_draw changed = make_draw(texB, sizeof texB);
    changed.vertex_spv = argv[1]; changed.fragment_spv = argv[2];
    CHECK(dm_vk_draw_indexed_textured_offscreen(&vk, &changed, &target) == VK_SUCCESS);
    expect_rgba(&target, 5, 5, 0, 255, 255, 255, __LINE__);
    expect_rgba(&target, 11, 5, 255, 0, 255, 255, __LINE__);
    expect_rgba(&target, 5, 11, 255, 255, 255, 255, __LINE__);
    expect_rgba(&target, 11, 11, 128, 64, 32, 255, __LINE__);
    expect_rgba(&target, 0, 0, 0, 0, 0, 255, __LINE__);

    // 改尺寸后重新绘制 A，四个象限必须重新出现。
    uint8_t large[24 * 24 * 4];
    dm_vk_pixels resized = {.width = 24, .height = 24, .bytes = large, .capacity = sizeof large};
    CHECK(dm_vk_draw_indexed_textured_offscreen(&vk, &draw, &resized) == VK_SUCCESS);
    expect_rgba(&resized, 8, 8, 255, 0, 0, 255, __LINE__);
    expect_rgba(&resized, 16, 8, 0, 255, 0, 255, __LINE__);
    expect_rgba(&resized, 8, 16, 0, 0, 255, 255, __LINE__);
    expect_rgba(&resized, 16, 16, 255, 255, 0, 255, __LINE__);
    expect_rgba(&resized, 0, 0, 0, 0, 0, 255, __LINE__);

    // UV 负控：所有 UV 固定为 0.25 时四个点都应是同一个红 texel；忽略 UV 的实现会失败。
    // UV negative control: constant UV 0.25 must sample the same red texel at all four points.
    memcpy(uvs_constant, verts, sizeof verts);
    for (unsigned i = 0; i < 4; i++) { uvs_constant[i].uv[0] = 0.25f; uvs_constant[i].uv[1] = 0.25f; }
    dm_vk_indexed_textured_draw constant_uv = make_draw(texA, sizeof texA);
    constant_uv.vertices = uvs_constant; constant_uv.vertex_spv = argv[1]; constant_uv.fragment_spv = argv[2];
    CHECK(dm_vk_draw_indexed_textured_offscreen(&vk, &constant_uv, &target) == VK_SUCCESS);
    expect_rgba(&target, 5, 5, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 5, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 5, 11, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 11, 255, 0, 0, 255, __LINE__);

    // 预检负例：全部在 GPU 对象创建前失败，输出保持哨兵，借用输入不变。
    dm_vk_indexed_textured_draw bad = draw;
    bad.texture_size = sizeof texA - 1; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.texture_pixels = NULL; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.texture_width = 0; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.vertex_stride = 12; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.vertex_count = 2; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.index_count = 4; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.index_count = 0xffffffffu; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.vertices = NULL; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.indices = NULL; check_reject(&vk, &bad, &target, __LINE__);
    dm_vk_indexed_textured_vertex nonfinite[4]; memcpy(nonfinite, verts, sizeof nonfinite);
    nonfinite[0].position[1] = NAN;
    bad = draw; bad.vertices = nonfinite; check_reject(&vk, &bad, &target, __LINE__);
    memcpy(nonfinite, verts, sizeof nonfinite); nonfinite[1].uv[0] = 1.25f;
    bad = draw; bad.vertices = nonfinite; check_reject(&vk, &bad, &target, __LINE__);
    memcpy(nonfinite, verts, sizeof nonfinite); nonfinite[2].uv[1] = INFINITY;
    bad = draw; bad.vertices = nonfinite; check_reject(&vk, &bad, &target, __LINE__);
    uint16_t bad_index[6] = {0, 1, 2, 0, 2, 4};
    bad = draw; bad.indices = bad_index; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; target.capacity = 10; check_reject(&vk, &bad, &target, __LINE__);
    target.capacity = sizeof output;
    dm_vk_pixels too_wide = {.width = 20000, .height = 16, .bytes = output, .capacity = sizeof output};
    memset(output, SENTINEL, sizeof output);
    CHECK(dm_vk_draw_indexed_textured_offscreen(&vk, &draw, &too_wide) != VK_SUCCESS);

    dm_vk_destroy(&vk);
    printf("vulkan_indexed_textured: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
