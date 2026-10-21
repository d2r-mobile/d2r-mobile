// #105 反射驱动的自有 2D 采样消费验收：sampler2D + storage buffer，非均匀 RGBA8 纹理的独立数值 oracle，
// 以及 sidecar/所有权/形状/容量预检失败。失败不写画布、不接管调用者纹理/采样器。
// #105 reflection-driven owned 2D sampled consumer: sampler2D + storage buffer, independent numeric oracle over a
// nonuniform RGBA8 texture, plus sidecar/ownership/shape/capacity preflight failures. Failures never write the canvas
// and never take ownership of caller texture/sampler.
#include "../vulkan/vk_shader_reflect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static uint32_t pack_rgba(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s sampled.spv sampled.res.json reflect.res.json [uint.spv sint.spv shadow.spv]\n", argv[0]); return 2; }
    dm_vk_shader_plan plan;
    char why[DM_VK_REFLECT_WHY] = {0};
    CHECK(dm_vk_reflect_load(argv[1], argv[2], &plan, why, sizeof why) == VK_SUCCESS);
    CHECK(plan.binding_count == 2);
    int have_sbuf = 0, have_simg = 0;
    for (uint32_t i = 0; i < plan.binding_count; i++) {
        have_sbuf |= plan.bindings[i].kind == DM_VK_REFLECT_SBUF;
        have_simg |= plan.bindings[i].kind == DM_VK_REFLECT_SAMPLED_IMAGE;
        CHECK(plan.bindings[i].set == 0 && plan.bindings[i].count == 1);
    }
    CHECK(have_sbuf && have_simg);

    // sidecar 必须与模块一致：把普通 reflect sidecar（无 textures）配采样模块必须失败。
    dm_vk_shader_plan bad;
    char bad_why[DM_VK_REFLECT_WHY] = {0};
    CHECK(dm_vk_reflect_load(argv[1], argv[3], &bad, bad_why, sizeof bad_why) != VK_SUCCESS);
    CHECK(!bad.code && !bad.binding_count);
    // 采样专属负例：count=2、与 sbuf 重复绑定、未支持的 images 类别都必须预检失败。
    const char *bad_sidecars[] = {
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"textures\":[{\"binding\":1,\"count\":2}],\"push_constants\":[{\"offset\":0,\"size\":8}]}",
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"textures\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}]}",
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"images\":[{\"binding\":1,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}]}"
    };
    for (unsigned i = 0; i < 3; i++) {
        char path[64]; snprintf(path, sizeof path, "sampled-bad-%u.json", i);
        FILE *f = fopen(path, "wb"); CHECK(f != NULL);
        if (f) { fwrite(bad_sidecars[i], 1, strlen(bad_sidecars[i]), f); fclose(f); }
        memset(&bad, 0, sizeof bad);
        CHECK(dm_vk_reflect_load(argv[1], path, &bad, bad_why, sizeof bad_why) != VK_SUCCESS);
        CHECK(!bad.code && !bad.binding_count);
        remove(path);
    }
    // 有效签名/无符号/阴影模块必须被严格形态拒绝（不是畸形指令）。
    for (int i = 4; i < argc; i++) {
        memset(&bad, 0, sizeof bad);
        CHECK(dm_vk_reflect_load(argv[i], argv[2], &bad, bad_why, sizeof bad_why) != VK_SUCCESS);
        CHECK(!bad.code);
    }

    dm_vk vk = {0};
    VkResult created = dm_vk_create(&vk);
    if (created != VK_SUCCESS) {
        printf("vulkan_reflect_sampled: no Vulkan device; SKIP\n");
        dm_vk_reflect_free(&plan);
        return 77;
    }

    uint8_t texels[4 * 2 * 4];
    for (unsigned i = 0; i < 4 * 2 * 4; i++) texels[i] = (uint8_t)(0x11u + i * 7u);
    dm_vk_mip_level level = {.bytes = texels, .size = sizeof texels, .row_bytes = 4 * 4};
    dm_vk_texture texture = {0};
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 2, 1, &level, &texture) == VK_SUCCESS);

    dm_vk_sampler_desc sd = {.min_filter = DM_VK_FILTER_NEAREST, .mag_filter = DM_VK_FILTER_NEAREST,
        .mipmap = DM_VK_MIP_NEAREST, .address_u = DM_VK_ADDRESS_CLAMP_TO_EDGE,
        .address_v = DM_VK_ADDRESS_CLAMP_TO_EDGE, .address_w = DM_VK_ADDRESS_CLAMP_TO_EDGE,
        .lod_min = 0.0f, .lod_max = 0.0f, .lod_bias = 0.0f, .max_aniso = 1.0f, .compare_enable = 0};
    dm_vk_sampler sampler = {0};
    CHECK(dm_vk_sampler_create(&vk, &sd, &sampler) == VK_SUCCESS && sampler.handle);

    uint32_t words[4 * 2], expected[4 * 2];
    for (unsigned y = 0; y < 2; y++)
        for (unsigned x = 0; x < 4; x++) expected[y * 4 + x] = pack_rgba(texels + (y * 4 + x) * 4);
    dm_vk_pixels canvas = {.width = 4, .height = 2, .bytes = (uint8_t *)words, .capacity = sizeof words};
    uint32_t params[2] = {4, 2};
    memset(words, 0x5a, sizeof words);
    CHECK(dm_vk_reflect_compute_sampled(&vk, &plan, &canvas, params, sizeof params, &texture, &sampler) == VK_SUCCESS);
    CHECK(!memcmp(words, expected, sizeof words));

    // 失败预检：不写画布，不接管调用者资源。
    uint32_t sentinel[4 * 2]; memset(sentinel, 0x5a, sizeof sentinel);
    dm_vk_pixels canvas_small = {.width = 4, .height = 2, .bytes = (uint8_t *)sentinel, .capacity = 4};
    CHECK(dm_vk_reflect_compute_sampled(&vk, &plan, &canvas_small, params, sizeof params, &texture, &sampler) != VK_SUCCESS);
    for (unsigned i = 0; i < 4 * 2; i++) CHECK(sentinel[i] == 0x5a5a5a5au);
    CHECK(dm_vk_reflect_compute_sampled(&vk, &plan, &canvas, params, sizeof params, &texture, NULL) != VK_SUCCESS);
    for (unsigned i = 0; i < 4 * 2; i++) CHECK(words[i] == expected[i]);

    // 非单层/多级纹理在 GPU 之前拒绝。
    uint8_t texels2[4 * 2 * 4 + 2 * 1 * 4];
    memset(texels2, 0x33, sizeof texels2);
    dm_vk_mip_level levels2[2] = {{texels2, 4 * 2 * 4, 4 * 4}, {texels2 + 4 * 2 * 4, 2 * 1 * 4, 2 * 4}};
    dm_vk_texture texture2 = {0};
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 2, 2, levels2, &texture2) == VK_SUCCESS);
    CHECK(dm_vk_reflect_compute_sampled(&vk, &plan, &canvas, params, sizeof params, &texture2, &sampler) != VK_SUCCESS);
    CHECK(!memcmp(words, expected, sizeof words));
    dm_vk_texture_destroy(&vk, &texture2);

    dm_vk_sampler_destroy(&vk, &sampler);
    dm_vk_texture_destroy(&vk, &texture);
    dm_vk_reflect_free(&plan);
    dm_vk_destroy(&vk);
    printf("vulkan_reflect_sampled: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
