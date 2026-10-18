// #117 自有工程诊断场景数据与探针检查：16 色 4×4 RGBA8 纹理、NDC ±0.75 索引四边形、20 个读回检查点。
// 依据已验收 #115 固定 indexed+textured 接口与 app/lifecycle.h；数据全部自有合成，不含任何游戏内容。
// #117 owned engineering-diagnostic scene data and probe checks: a 16-colour 4x4 RGBA8 texture, an indexed quad
// at NDC ±0.75 and 20 readback probes. Based on the accepted #115 fixed indexed+textured interface and
// app/lifecycle.h; all data is owned and synthetic, with no game content.
#include "diagnostic_scene.h"

#include <string.h>

// 行优先 RGBA8：每一行四个互不相同的颜色，整体 16 个 texel 两两不同（测试独立核对字面值）。
// Row-major RGBA8: four distinct colours per row and 16 pairwise distinct texels (tests verify the literals).
static const uint8_t kTexture[DM_DIAG_SCENE_TEXTURE_WIDTH * DM_DIAG_SCENE_TEXTURE_HEIGHT * 4u] = {
    255, 0, 0, 255,     0, 255, 0, 255,     0, 0, 255, 255,     255, 255, 0, 255,     // 红/绿/蓝/黄 | red/green/blue/yellow
    255, 0, 255, 255,   0, 255, 255, 255,   255, 128, 0, 255,   128, 0, 255, 255,     // 品红/青/橙/紫 | magenta/cyan/orange/violet
    0, 128, 0, 255,     0, 0, 128, 255,     128, 128, 128, 255, 255, 255, 255, 255,   // 深绿/藏青/灰/白 | dark green/navy/grey/white
    64, 32, 16, 255,    255, 64, 128, 255,  0, 64, 64, 255,     32, 32, 32, 255,      // 棕/粉/墨绿/炭 | brown/pink/teal/charcoal
};

// UV (0,0) 对应顶点 (-0.75,-0.75)。Vulkan 视口把 NDC y=-1 映射到帧缓冲顶行，因此纹理第 0 行出现在上侧。
// UV (0,0) belongs to vertex (-0.75,-0.75). The Vulkan viewport maps NDC y=-1 to the top framebuffer row, so
// texture row 0 appears at the top.
static const dm_diag_scene_vertex kVertices[DM_DIAG_SCENE_VERTEX_COUNT] = {
    {{-0.75f, -0.75f}, {0.0f, 0.0f}},
    {{ 0.75f, -0.75f}, {1.0f, 0.0f}},
    {{ 0.75f,  0.75f}, {1.0f, 1.0f}},
    {{-0.75f,  0.75f}, {0.0f, 1.0f}},
};
static const uint16_t kIndices[DM_DIAG_SCENE_INDEX_COUNT] = {0, 1, 2, 0, 2, 3};

// 空画布的清屏色与 #115 一致：不透明黑。| The empty-canvas clear colour matches #115: opaque black.
static const uint8_t kClear[4] = {0, 0, 0, 255};

const dm_diag_scene *dm_diag_scene_owned(void) {
    static const dm_diag_scene scene = {
        kVertices, DM_DIAG_SCENE_VERTEX_COUNT,
        kIndices, DM_DIAG_SCENE_INDEX_COUNT,
        kTexture, DM_DIAG_SCENE_TEXTURE_WIDTH, DM_DIAG_SCENE_TEXTURE_HEIGHT,
        sizeof kTexture,
    };
    return &scene;
}

static int scene_valid(const dm_diag_scene *scene) {
    if (!scene->vertices || !scene->indices || !scene->texture) return 0;
    if (!scene->texture_width || !scene->texture_height) return 0;
    if (scene->texture_width > 16384u || scene->texture_height > 16384u) return 0;
    if (scene->texture_size != (size_t)scene->texture_width * scene->texture_height * 4u) return 0;
    if (scene->vertex_count < 3u || scene->vertex_count > 65536u) return 0;
    if (scene->index_count < 3u || scene->index_count % 3u || scene->index_count > scene->vertex_count * 3u) return 0;
    for (uint32_t i = 0; i < scene->vertex_count; i++) {
        const float *v = scene->vertices[i].position;
        const float *uv = scene->vertices[i].uv;
        if (!(v[0] >= -1.0f && v[0] <= 1.0f && v[1] >= -1.0f && v[1] <= 1.0f)) return 0;
        if (!(uv[0] >= 0.0f && uv[0] <= 1.0f && uv[1] >= 0.0f && uv[1] <= 1.0f)) return 0;
    }
    for (uint32_t i = 0; i < scene->index_count; i++)
        if (scene->indices[i] >= scene->vertex_count) return 0;
    return 1;
}

// UV 中心 → NDC → 像素；映射只依赖场景的固定几何，不读取实现状态。
// UV centre -> NDC -> pixel; the mapping depends only on the fixed scene geometry and no implementation state.
static int probe_position(double u, double v, uint32_t width, uint32_t height, uint32_t *x, uint32_t *y) {
    const double extent = (double)DM_DIAG_SCENE_QUAD_EXTENT;
    const double ndc_x = u * 2.0 * extent - extent;
    const double ndc_y = v * 2.0 * extent - extent;
    const double px = (ndc_x + 1.0) * 0.5 * (double)width;
    const double py = (ndc_y + 1.0) * 0.5 * (double)height;
    if (!(px >= 0.0 && px < (double)width) || !(py >= 0.0 && py < (double)height)) return 0;
    *x = (uint32_t)px;
    *y = (uint32_t)py;
    return 1;
}

static uint32_t fail(dm_diag_scene_mismatch *mismatch, dm_diag_scene_mismatch_kind kind,
                     uint32_t x, uint32_t y, const uint8_t *expected, const uint8_t *observed) {
    if (mismatch) {
        mismatch->kind = kind;
        mismatch->x = x;
        mismatch->y = y;
        if (expected) memcpy(mismatch->expected, expected, 4);
        if (observed) memcpy(mismatch->observed, observed, 4);
    }
    return 0;
}

uint32_t dm_diag_scene_check_pixels(const dm_diag_scene *scene, const uint8_t *pixels,
                                    uint32_t width, uint32_t height, size_t capacity,
                                    dm_diag_scene_mismatch *mismatch) {
    if (mismatch) memset(mismatch, 0, sizeof *mismatch);
    if (!scene || !scene_valid(scene) || !pixels || !width || !height)
        return fail(mismatch, DM_DIAG_SCENE_MISMATCH_INPUT, 0, 0, NULL, NULL);
    // 64 位尺寸检查：容量必须覆盖整张紧排画布。| 64-bit size check: capacity must cover the tight canvas.
    if ((uint64_t)width > (uint64_t)SIZE_MAX / 4u / (uint64_t)height)
        return fail(mismatch, DM_DIAG_SCENE_MISMATCH_INPUT, 0, 0, NULL, NULL);
    const size_t required = (size_t)width * height * 4u;
    if (capacity < required)
        return fail(mismatch, DM_DIAG_SCENE_MISMATCH_INPUT, 0, 0, NULL, NULL);

    uint32_t probes = 0;
    for (uint32_t ty = 0; ty < scene->texture_height; ty++) {
        for (uint32_t tx = 0; tx < scene->texture_width; tx++) {
            const double u = ((double)tx + 0.5) / (double)scene->texture_width;
            const double v = ((double)ty + 0.5) / (double)scene->texture_height;
            uint32_t x, y;
            if (!probe_position(u, v, width, height, &x, &y))
                return fail(mismatch, DM_DIAG_SCENE_MISMATCH_INPUT, 0, 0, NULL, NULL);
            const uint8_t *expected = scene->texture + ((size_t)ty * scene->texture_width + tx) * 4u;
            const uint8_t *observed = pixels + ((size_t)y * width + x) * 4u;
            if (memcmp(expected, observed, 4))
                return fail(mismatch, DM_DIAG_SCENE_MISMATCH_PIXEL, x, y, expected, observed);
            probes++;
        }
    }
    // 四边形外四角必须是清屏色；纹理被拉伸到整屏或清屏被跳过时这里会失败。
    // The four corners outside the quad must stay clear; a full-screen quad or a skipped clear fails here.
    static const uint32_t corner_x[4] = {0, 1, 0, 1};
    static const uint32_t corner_y[4] = {0, 0, 1, 1};
    for (uint32_t i = 0; i < 4u; i++) {
        const uint32_t x = corner_x[i] ? width - 1u : 0u;
        const uint32_t y = corner_y[i] ? height - 1u : 0u;
        const uint8_t *observed = pixels + ((size_t)y * width + x) * 4u;
        if (memcmp(kClear, observed, 4))
            return fail(mismatch, DM_DIAG_SCENE_MISMATCH_PIXEL, x, y, kClear, observed);
        probes++;
    }
    return probes;
}

int dm_diag_scene_ready(const dm_app_lifecycle *lifecycle, const void *surface) {
    if (!lifecycle || !surface) return 0;
    if (dm_app_lifecycle_state(lifecycle) != DM_APP_STATE_OWNED_DIAGNOSTIC) return 0;
    return dm_app_lifecycle_surface(lifecycle) == surface;
}
