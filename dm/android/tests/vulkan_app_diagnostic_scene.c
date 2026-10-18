// #117 应用诊断场景的 GPU 交叉验证：用已验收 #115 接口在宿主 Vulkan 上渲染自有场景，按文档几何独立核对检查点，
// 并证明改动一个 texel 后原场景期望必须失败（纹理确实被采样，检查器不是空转）。
// #117 GPU cross-check of the app diagnostic scene: render the owned scene on the host Vulkan device through the
// accepted #115 API, verify the probes from the documented geometry, and prove that changing one texel makes the
// original expectations fail (the texture is really sampled and the checker is not vacuous).
#include "../app/diagnostic_scene.h"
#include "../vulkan/vk_indexed_textured.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define EXTENT 256u

static void probe_for(uint32_t tx, uint32_t ty, uint32_t *x, uint32_t *y) {
    const double u = ((double)tx + 0.5) / 4.0;
    const double v = ((double)ty + 0.5) / 4.0;
    *x = (uint32_t)(((u * 1.5 - 0.75) + 1.0) * 0.5 * (double)EXTENT);
    *y = (uint32_t)(((v * 1.5 - 0.75) + 1.0) * 0.5 * (double)EXTENT);
}

static VkResult render(dm_vk *vk, const dm_diag_scene *scene, const char *vert, const char *frag, dm_vk_pixels *target) {
    dm_vk_indexed_textured_vertex vertices[DM_DIAG_SCENE_VERTEX_COUNT];
    for (uint32_t i = 0; i < DM_DIAG_SCENE_VERTEX_COUNT; i++) {
        vertices[i].position[0] = scene->vertices[i].position[0];
        vertices[i].position[1] = scene->vertices[i].position[1];
        vertices[i].uv[0] = scene->vertices[i].uv[0];
        vertices[i].uv[1] = scene->vertices[i].uv[1];
    }
    dm_vk_indexed_textured_draw request = {
        .vertices = vertices, .vertex_count = scene->vertex_count,
        .vertex_stride = DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE,
        .indices = scene->indices, .index_count = scene->index_count,
        .texture_pixels = scene->texture, .texture_size = scene->texture_size,
        .texture_width = scene->texture_width, .texture_height = scene->texture_height,
        .vertex_spv = vert, .fragment_spv = frag,
    };
    return dm_vk_draw_indexed_textured_offscreen(vk, &request, target);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s indexed_textured.vert.spv indexed_textured.frag.spv\n", argv[0]);
        return 2;
    }
    dm_vk vk = {0};
    if (dm_vk_create(&vk) != VK_SUCCESS) {
        printf("vulkan_app_diagnostic_scene: no Vulkan device; SKIP\n");
        return 77;
    }
    const dm_diag_scene *scene = dm_diag_scene_owned();
    const size_t capacity = (size_t)EXTENT * EXTENT * 4u;
    uint8_t *output = malloc(capacity);
    CHECK(output != NULL);
    if (!output) { dm_vk_destroy(&vk); return 1; }
    dm_vk_pixels target = {.width = EXTENT, .height = EXTENT, .bytes = output, .capacity = capacity};

    CHECK(render(&vk, scene, argv[1], argv[2], &target) == VK_SUCCESS);
    dm_diag_scene_mismatch mismatch;
    CHECK(dm_diag_scene_check_pixels(scene, output, EXTENT, EXTENT, capacity, &mismatch) == DM_DIAG_SCENE_PROBE_COUNT);
    CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_NONE);

    // 改一个 texel：新场景的期望全部成立，原场景在同一读回上必须失败并定位到该 texel 的检查点。
    // Changing one texel: the altered scene's expectations hold, while the original fails on the same readback at
    // that texel's probe.
    uint8_t altered_texture[DM_DIAG_SCENE_TEXTURE_WIDTH * DM_DIAG_SCENE_TEXTURE_HEIGHT * 4u];
    memcpy(altered_texture, scene->texture, sizeof altered_texture);
    altered_texture[0] = 0; altered_texture[1] = 0; altered_texture[2] = 0;   // texel (0,0) 变黑 | black
    dm_diag_scene altered = *scene;
    altered.texture = altered_texture;
    CHECK(render(&vk, &altered, argv[1], argv[2], &target) == VK_SUCCESS);
    CHECK(dm_diag_scene_check_pixels(&altered, output, EXTENT, EXTENT, capacity, &mismatch) == DM_DIAG_SCENE_PROBE_COUNT);
    CHECK(dm_diag_scene_check_pixels(scene, output, EXTENT, EXTENT, capacity, &mismatch) == 0);
    CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_PIXEL);
    uint32_t probe_x, probe_y;
    probe_for(0, 0, &probe_x, &probe_y);
    CHECK(mismatch.x == probe_x && mismatch.y == probe_y);
    CHECK(mismatch.expected[0] == 255 && mismatch.expected[1] == 0 && mismatch.expected[2] == 0);

    free(output);
    dm_vk_destroy(&vk);
    printf("vulkan_app_diagnostic_scene: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
