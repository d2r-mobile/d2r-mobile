// #133 纹理接口 plan 验收：严格 UV/varying/sampler set-binding/type/array/hash 预检，plan-owned shader 的实际
// 纹理 indexed 像素 oracle、错误 UV/绑定控制和失败画布哨兵。
// #133 textured interface plan acceptance: strict UV/varying/sampler set-binding/type/array/hash preflight, actual
// indexed pixel oracle using plan-owned shaders, wrong-UV/binding controls and unchanged canvas on failure.
#include "../vulkan/vk_indexed_textured.h"
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
static void expect_reject_meta(const char *vs, const char *ps, const char *meta, const char *label) {
    dm_vk_graphics_plan p; char why[DM_VK_GRAPHICS_WHY] = {0};
    memset(&p, 0, sizeof p);
    VkResult r = dm_vk_textured_plan_load(vs, ps, meta, &p, why, sizeof why);
    checks++;
    if (r == VK_SUCCESS || p.vs_code || p.ps_code) {
        failures++; fprintf(stderr, "FAIL %s: expected reject (r=%d why=%s)\n", label, (int)r, why);
        dm_vk_graphics_plan_free(&p);
    }
}
static void expect_reject(const char *vs, const char *ps, const char *label) {
    expect_reject_meta(vs, ps, NULL, label);
}
static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    fclose(f); return ok;
}

static const uint8_t texA[16] = {
    255, 0, 0, 255,   0, 255, 0, 255,
    0, 0, 255, 255,   255, 255, 0, 255,
};

int main(int argc, char **argv) {
    if (argc != 12) {
        fprintf(stderr, "usage: %s valid.vert valid.frag bad_uv.vert bad_link.vert bad_set.frag bad_binding.frag bad_array.frag extra.frag no_desc.frag uint.frag bad_model.comp\n", argv[0]);
        return 2;
    }
    dm_vk_graphics_plan plan; char why[DM_VK_GRAPHICS_WHY] = {0};
    CHECK(dm_vk_textured_plan_load(argv[1], argv[2], NULL, &plan, why, sizeof why) == VK_SUCCESS);
    CHECK(plan.kind == DM_VK_PLAN_TEXTURED);
    CHECK(plan.position_location == 0 && plan.uv_location == 1 && plan.varying_location == 0);
    CHECK(plan.position_components == 2 && plan.varying_components == 2);
    CHECK(plan.sampler_set == 0 && plan.sampler_binding == 0);
    CHECK(plan.vs_code && plan.ps_code && plan.vs_code_bytes && plan.ps_code_bytes);
    CHECK(dm_vk_graphics_plan_check(&plan, why, sizeof why));

    char meta[512];
    snprintf(meta, sizeof meta, "{\"vs_stage\":\"vs\",\"vs_sha256\":\"%s\",\"ps_stage\":\"ps\",\"ps_sha256\":\"%s\"}",
        plan.vs_sha256, plan.ps_sha256);
    CHECK(write_text("textured-meta-good.json", meta));
    dm_vk_graphics_plan good; memset(&good, 0, sizeof good);
    CHECK(dm_vk_textured_plan_load(argv[1], argv[2], "textured-meta-good.json", &good, why, sizeof why) == VK_SUCCESS);
    CHECK(good.sidecar_hash_bound && dm_vk_graphics_plan_check(&good, why, sizeof why));
    dm_vk_graphics_plan_free(&good);
    remove("textured-meta-good.json");
    char wrong_hash[65]; memset(wrong_hash, '0', 64); wrong_hash[64] = 0;
    snprintf(meta, sizeof meta, "{\"vs_stage\":\"vs\",\"vs_sha256\":\"%s\",\"ps_stage\":\"ps\",\"ps_sha256\":\"%s\"}",
        wrong_hash, plan.ps_sha256);
    CHECK(write_text("textured-meta-bad.json", meta));
    expect_reject_meta(argv[1], argv[2], "textured-meta-bad.json", "hash-mismatch");
    remove("textured-meta-bad.json");
    snprintf(meta, sizeof meta, "{\"vs_stage\":\"ps\",\"vs_sha256\":\"%s\",\"ps_stage\":\"ps\",\"ps_sha256\":\"%s\"}",
        plan.vs_sha256, plan.ps_sha256);
    CHECK(write_text("textured-meta-stage.json", meta));
    expect_reject_meta(argv[1], argv[2], "textured-meta-stage.json", "stage-mismatch");
    remove("textured-meta-stage.json");

    expect_reject(argv[3], argv[2], "bad-uv-type");
    expect_reject(argv[1], argv[4], "bad-varying-link");
    expect_reject(argv[1], argv[5], "bad-descriptor-set");
    expect_reject(argv[1], argv[6], "bad-descriptor-binding");
    expect_reject(argv[1], argv[7], "descriptor-array");
    expect_reject(argv[1], argv[8], "extra-descriptor");
    expect_reject(argv[1], argv[9], "missing-descriptor");
    expect_reject(argv[1], argv[10], "non-float-sampled-type");
    expect_reject(argv[11], argv[2], "wrong-stage");

    dm_vk vk = {0};
    VkResult created = dm_vk_create(&vk);
    if (created != VK_SUCCESS) {
        printf("vulkan_textured_interface: no Vulkan device; SKIP GPU\n");
        dm_vk_graphics_plan_free(&plan);
        return 77;
    }
    uint8_t output[16 * 16 * 4];
    dm_vk_pixels target = {.width = 16, .height = 16, .bytes = output, .capacity = sizeof output};
    dm_vk_indexed_textured_vertex verts[4] = {
        {{-0.8f, -0.8f}, {0, 0}}, {{0.8f, -0.8f}, {1, 0}},
        {{0.8f, 0.8f}, {1, 1}}, {{-0.8f, 0.8f}, {0, 1}},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    dm_vk_indexed_textured_draw draw = {.vertices = verts, .vertex_count = 4,
        .vertex_stride = DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE, .indices = indices, .index_count = 6,
        .texture_pixels = texA, .texture_size = sizeof texA, .texture_width = 2, .texture_height = 2};

    dm_vk_graphics_plan bad_uv = plan; bad_uv.uv_location = 0;
    memset(output, SENTINEL, sizeof output);
    CHECK(dm_vk_draw_indexed_textured_plan_offscreen(&vk, &draw, &bad_uv, &target) != VK_SUCCESS);
    for (size_t i = 0; i < sizeof output; i++) if (output[i] != SENTINEL) { CHECK(0); break; }
    dm_vk_graphics_plan bad_binding = plan; bad_binding.sampler_binding = 1;
    memset(output, SENTINEL, sizeof output);
    CHECK(dm_vk_draw_indexed_textured_plan_offscreen(&vk, &draw, &bad_binding, &target) != VK_SUCCESS);
    for (size_t i = 0; i < sizeof output; i++) if (output[i] != SENTINEL) { CHECK(0); break; }

    CHECK(dm_vk_draw_indexed_textured_plan_offscreen(&vk, &draw, &plan, &target) == VK_SUCCESS);
    expect_rgba(&target, 5, 5, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 5, 0, 255, 0, 255, __LINE__);
    expect_rgba(&target, 5, 11, 0, 0, 255, 255, __LINE__);
    expect_rgba(&target, 11, 11, 255, 255, 0, 255, __LINE__);
    expect_rgba(&target, 0, 0, 0, 0, 0, 255, __LINE__);

    dm_vk_indexed_textured_vertex constant_uv[4];
    memcpy(constant_uv, verts, sizeof verts);
    for (unsigned i = 0; i < 4; i++) { constant_uv[i].uv[0] = 0.25f; constant_uv[i].uv[1] = 0.25f; }
    draw.vertices = constant_uv;
    CHECK(dm_vk_draw_indexed_textured_plan_offscreen(&vk, &draw, &plan, &target) == VK_SUCCESS);
    expect_rgba(&target, 5, 5, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 5, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 5, 11, 255, 0, 0, 255, __LINE__);
    expect_rgba(&target, 11, 11, 255, 0, 0, 255, __LINE__);

    dm_vk_graphics_plan_free(&plan);
    CHECK(!plan.vs_code && !plan.ps_code && !plan.vs_code_bytes && !plan.ps_code_bytes && !plan.kind);
    dm_vk_destroy(&vk);
    printf("vulkan_textured_interface: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
