// #112 有界 indexed 离屏绘制验收：固定 float2+float4、uint16 index、RGBA8 读回；非平凡像素 oracle、
// 重绘/改尺寸、预检负例与错误索引负控制。宿主 Vulkan 通过不是 Adreno/游戏命令验收。
// #112 bounded indexed offscreen acceptance: fixed float2+float4, uint16 indices, RGBA8 readback; nontrivial
// pixel oracle, redraw/resize, preflight negatives and a wrong-index negative control. Host Vulkan success is
// not Adreno/game-command acceptance.
#include "../vulkan/vk_indexed.h"
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

static dm_vk_indexed_vertex verts_left_red[] = {
    {{0.2f, -0.6f}, {0, 0, 1, 1}}, {{0.8f, -0.6f}, {0, 0, 1, 1}}, {{0.5f, 0.6f}, {0, 0, 1, 1}},
    {{-0.8f, -0.6f}, {1, 0, 0, 1}}, {{-0.2f, -0.6f}, {1, 0, 0, 1}}, {{-0.5f, 0.6f}, {1, 0, 0, 1}},
};
static const uint16_t indices_left_red[] = {3, 4, 5, 0, 1, 2};   // 左红、右蓝；顺序/索引错误会翻转 | left red, right blue
static dm_vk_indexed_vertex verts_left_yellow[] = {
    {{0.2f, -0.6f}, {0, 1, 0, 1}}, {{0.8f, -0.6f}, {0, 1, 0, 1}}, {{0.5f, 0.6f}, {0, 1, 0, 1}},
    {{-0.8f, -0.6f}, {1, 1, 0, 1}}, {{-0.2f, -0.6f}, {1, 1, 0, 1}}, {{-0.5f, 0.6f}, {1, 1, 0, 1}},
};
static const uint16_t indices_wrong[] = {0, 1, 2, 0, 1, 2};     // 负控制：只画右侧三角形，左侧应为背景 | negative control: right only; left must be background

static dm_vk_indexed_draw make_draw(dm_vk_indexed_vertex *v, const uint16_t *idx, const char *vs, const char *fs) {
    dm_vk_indexed_draw d = {.vertices = v, .vertex_count = 6, .vertex_stride = DM_VK_INDEXED_VERTEX_STRIDE,
        .indices = idx, .index_count = 6, .vertex_spv = vs, .fragment_spv = fs};
    return d;
}

static void check_reject(dm_vk *vk, dm_vk_indexed_draw *d, dm_vk_pixels *p, int line) {
    memset(p->bytes, SENTINEL, p->capacity);
    checks++;
    VkResult r = dm_vk_draw_indexed_offscreen(vk, d, p);
    if (r == VK_SUCCESS) { failures++; fprintf(stderr, "FAIL %d: expected reject\n", line); }
    for (size_t i = 0; i < p->capacity; i++)
        if (p->bytes[i] != SENTINEL) {
            failures++; fprintf(stderr, "FAIL %d: output modified at byte %zu\n", line, i); break;
        }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s indexed.vert.spv indexed.frag.spv\n", argv[0]); return 2; }
    dm_vk vk = {0};
    VkResult created = dm_vk_create(&vk);
    if (created != VK_SUCCESS) {
        printf("vulkan_indexed: no Vulkan device; SKIP\n");
        return 77;
    }
    uint8_t bytes[16 * 16 * 4];
    dm_vk_pixels target = {.width = 16, .height = 16, .bytes = bytes, .capacity = sizeof bytes};
    dm_vk_indexed_draw draw = make_draw(verts_left_red, indices_left_red, argv[1], argv[2]);
    memset(bytes, SENTINEL, sizeof bytes);
    CHECK(dm_vk_draw_indexed_offscreen(&vk, &draw, &target) == VK_SUCCESS);
    expect_rgba(&target, 4, 6, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 6, 0, 0, 255, 255, __LINE__);
    expect_rgba(&target, 0, 0, 0, 0, 0, 255, __LINE__);
    expect_rgba(&target, 15, 15, 0, 0, 0, 255, __LINE__);

    // 第二次绘制必须清除上一帧且使用新颜色；随后换尺寸。
    // Second draw must clear the previous frame and use new colors; then resize.
    dm_vk_indexed_draw second = make_draw(verts_left_yellow, indices_left_red, argv[1], argv[2]);
    CHECK(dm_vk_draw_indexed_offscreen(&vk, &second, &target) == VK_SUCCESS);
    expect_rgba(&target, 4, 6, 255, 255, 0, 255, __LINE__);
    expect_rgba(&target, 11, 6, 0, 255, 0, 255, __LINE__);
    expect_rgba(&target, 0, 0, 0, 0, 0, 255, __LINE__);

    uint8_t large[24 * 24 * 4];
    dm_vk_pixels resized = {.width = 24, .height = 24, .bytes = large, .capacity = sizeof large};
    CHECK(dm_vk_draw_indexed_offscreen(&vk, &draw, &resized) == VK_SUCCESS);
    expect_rgba(&resized, 6, 9, 255, 0, 0, 255, __LINE__);
    expect_rgba(&resized, 17, 9, 0, 0, 255, 255, __LINE__);
    expect_rgba(&resized, 23, 23, 0, 0, 0, 255, __LINE__);

    // 负控制：只保留右侧索引后左侧应回到背景，证明索引内容实际参与绘制。
    // Negative control: keeping only right-side indices must clear the left to background, proving index use.
    dm_vk_indexed_draw wrong = make_draw(verts_left_red, indices_wrong, argv[1], argv[2]);
    CHECK(dm_vk_draw_indexed_offscreen(&vk, &wrong, &target) == VK_SUCCESS);
    expect_rgba(&target, 4, 6, 0, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 6, 0, 0, 255, 255, __LINE__);

    // 预检负例：所有失败都发生在 GPU 对象创建前，输出保持哨兵。
    // Preflight negatives: all failures happen before GPU objects, output stays sentinel.
    dm_vk_indexed_draw bad = draw;
    bad.vertex_stride = 16; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.vertex_count = 2; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.index_count = 4; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.index_count = 0xffffffffu; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.vertices = NULL; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; bad.indices = NULL; check_reject(&vk, &bad, &target, __LINE__);
    dm_vk_indexed_vertex nonfinite[6]; memcpy(nonfinite, verts_left_red, sizeof nonfinite);
    nonfinite[0].position[0] = NAN;
    bad = draw; bad.vertices = nonfinite; check_reject(&vk, &bad, &target, __LINE__);
    memcpy(nonfinite, verts_left_red, sizeof nonfinite);
    nonfinite[1].color[0] = 1.5f;
    bad = draw; bad.vertices = nonfinite; check_reject(&vk, &bad, &target, __LINE__);
    uint16_t out_of_range[6] = {3, 4, 5, 0, 1, 6};
    bad = draw; bad.indices = out_of_range; check_reject(&vk, &bad, &target, __LINE__);
    bad = draw; target.capacity = 10; check_reject(&vk, &bad, &target, __LINE__);
    target.capacity = sizeof bytes;
    dm_vk_pixels empty = {.width = 0, .height = 16, .bytes = bytes, .capacity = sizeof bytes};
    memset(bytes, SENTINEL, sizeof bytes);
    CHECK(dm_vk_draw_indexed_offscreen(&vk, &draw, &empty) != VK_SUCCESS);
    for (size_t i = 0; i < sizeof bytes; i++) if (bytes[i] != SENTINEL) { failures++; break; }

    dm_vk_destroy(&vk);
    printf("vulkan_indexed: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
