// #13 采样器回归：自有 2x2 RGBA8 纹理 + nearest/linear、clamp/repeat 独立像素对照；非法描述不修改 out。
// #13 sampler regression: owned 2x2 RGBA8 texture with nearest/linear and clamp/repeat checked against independently
// computed pixels; invalid descriptors leave out untouched.
#include "../vulkan/vk_sampler.h"
#include "../../gfx/tex/nvn_deswizzle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define VK_CHECK(c) do { VkResult r_ = (c); if (r_ != VK_SUCCESS) { fprintf(stderr, "FAIL %d: %s -> %d\n", __LINE__, #c, (int)r_); exit(1); } } while (0)

static const uint8_t kTex[2][2][4] = {
    {{10, 20, 30, 255}, {200, 100, 50, 255}},
    {{1, 2, 3, 255}, {250, 240, 230, 255}},
};

static dm_vk_texture make_texture(dm_vk *vk) {
    nvn_texdesc d; memset(&d, 0, sizeof d);
    d.target = 1; d.format = 0x25; d.width = 2; d.height = 2; d.depth = 1; d.levels = 1;
    nvn_texlayout layout; nvn_tex_layout(&d, &layout);
    uint8_t *storage = calloc(1, (size_t)layout.size);
    uint8_t linear[2 * 2 * 4];
    for (int y = 0; y < 2; y++) for (int x = 0; x < 2; x++) memcpy(linear + (y * 2 + x) * 4, kTex[y][x], 4);
    CHECK(storage && nvn_swizzle(&d, 0, 0, linear, 2 * 4, storage) == 0);
    dm_vk_texture t; memset(&t, 0, sizeof t);
    VK_CHECK(dm_vk_texture_upload(vk, &d, storage, (size_t)layout.size, &t));
    CHECK(t.view && t.format == VK_FORMAT_R8G8B8A8_UNORM && t.width == 2 && t.height == 2);
    free(storage);
    return t;
}

static int byte_round(double v) {
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (int)(v + 0.5);
}
// 独立双线性 oracle：u/v 为归一化坐标，纹理 2x2，单级。  | independent bilinear oracle for a 2x2 single-level texture.
static void bilinear(double u, double v, int out[4]) {
    double tx = u * 2.0 - 0.5, ty = v * 2.0 - 0.5;
    double x0 = floor(tx), y0 = floor(ty);
    double fx = tx - x0, fy = ty - y0;
    int ix0 = (int)x0, iy0 = (int)y0, ix1 = ix0 + 1, iy1 = iy0 + 1;
    if (ix0 < 0) { ix0 = 0; ix1 = 0; } if (ix1 > 1) { ix1 = 1; ix0 = ix0 > 1 ? 1 : ix0; }
    if (iy0 < 0) { iy0 = 0; iy1 = 0; } if (iy1 > 1) { iy1 = 1; iy0 = iy0 > 1 ? 1 : iy0; }
    for (int c = 0; c < 4; c++) {
        double top = kTex[iy0][ix0][c] * (1 - fx) + kTex[iy0][ix1][c] * fx;
        double bottom = kTex[iy1][ix0][c] * (1 - fx) + kTex[iy1][ix1][c] * fx;
        out[c] = byte_round(top * (1 - fy) + bottom * fy);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <fullscreen.vert.spv> <sample2d.frag.spv>\n", argv[0]); return 2; }
    const char *vert = argv[1], *frag = argv[2];
    dm_vk vk; VK_CHECK(dm_vk_create(&vk));
    dm_vk_texture texture = make_texture(&vk);

    dm_vk_sampler_desc base; memset(&base, 0, sizeof base);
    base.min_filter = DM_VK_FILTER_NEAREST; base.mag_filter = DM_VK_FILTER_NEAREST;
    base.mipmap = DM_VK_MIP_NEAREST;
    base.address_u = base.address_v = base.address_w = DM_VK_ADDRESS_CLAMP_TO_EDGE;
    base.lod_min = 0; base.lod_max = 0; base.lod_bias = 0; base.max_aniso = 1.0f;

    uint8_t out[4 * 4 * 4];
    dm_vk_pixels canvas = {.width = 4, .height = 4, .bytes = out, .capacity = sizeof out};

    // nearest：4x4 画布上每个 2x2 块必须精确等于对应纹素。 | nearest: every 2x2 block equals its texel exactly.
    dm_vk_sampler nearest; VK_CHECK(dm_vk_sampler_create(&vk, &base, &nearest));
    CHECK(nearest.handle != VK_NULL_HANDLE);
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &texture, &nearest, 4, 4, &canvas, 1.0f, 0.0f));
    int bad = 0;
    for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++)
        for (int c = 0; c < 4; c++) {
            int want = kTex[y / 2][x / 2][c];
            if (out[(y * 4 + x) * 4 + c] != want) bad++;
        }
    CHECK(bad == 0);
    printf("nearest: %d channel mismatches\n", bad);

    // linear：用独立双线性公式逐通道对照（±1 舍入）。 | linear: compare per channel with an independent bilinear formula (±1).
    dm_vk_sampler_desc lin = base; lin.min_filter = lin.mag_filter = DM_VK_FILTER_LINEAR;
    dm_vk_sampler linear; VK_CHECK(dm_vk_sampler_create(&vk, &lin, &linear));
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &texture, &linear, 4, 4, &canvas, 1.0f, 0.0f));
    bad = 0;
    for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) {
        double u = (x + 0.5) / 4.0, v = (y + 0.5) / 4.0;
        int want[4]; bilinear(u, v, want);
        for (int c = 0; c < 4; c++) {
            int got = out[(y * 4 + x) * 4 + c];
            if (abs(got - want[c]) > 1) bad++;
        }
    }
    CHECK(bad == 0);
    printf("linear: %d channel mismatches (>1 LSB)\n", bad);

    // clamp vs repeat：uv_scale=2，比较第 2/3 列（clamp 到纹素 1，repeat 回到纹素 0）。
    // clamp vs repeat: uv_scale=2; columns 2/3 differ (clamp stays at texel 1, repeat wraps to texel 0).
    dm_vk_sampler_desc clamp = base;
    dm_vk_sampler_desc repeat = base; repeat.address_u = DM_VK_ADDRESS_REPEAT;
    dm_vk_sampler sclamp, srepeat;
    VK_CHECK(dm_vk_sampler_create(&vk, &clamp, &sclamp));
    VK_CHECK(dm_vk_sampler_create(&vk, &repeat, &srepeat));
    uint8_t out_clamp[4 * 4 * 4], out_repeat[4 * 4 * 4];
    dm_vk_pixels pc = {.width = 4, .height = 4, .bytes = out_clamp, .capacity = sizeof out_clamp};
    dm_vk_pixels pr = {.width = 4, .height = 4, .bytes = out_repeat, .capacity = sizeof out_repeat};
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &texture, &sclamp, 4, 4, &pc, 2.0f, 0.0f));
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &texture, &srepeat, 4, 4, &pr, 2.0f, 0.0f));
    bad = 0;
    const int clamp_cols[4] = {0, 1, 1, 1};
    const int repeat_cols[4] = {0, 1, 0, 1};
    for (uint32_t y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) for (int c = 0; c < 4; c++) {
            if (out_clamp[(y * 4 + x) * 4 + c] != kTex[clamp_cols[y]][clamp_cols[x]][c]) { if (bad < 4) fprintf(stderr, "clamp mismatch x=%u y=%u c=%d got=%u want=%u\n", x, y, c, out_clamp[(y*4+x)*4+c], kTex[clamp_cols[y]][clamp_cols[x]][c]); bad++; }
            if (out_repeat[(y * 4 + x) * 4 + c] != kTex[clamp_cols[y]][repeat_cols[x]][c]) bad++;
        }
    }
    CHECK(bad == 0);
    CHECK(((const uint8_t *)out_clamp)[(2 * 4 + 2) * 4] != ((const uint8_t *)out_repeat)[(2 * 4 + 2) * 4]);
    printf("clamp/repeat: %d channel mismatches\n", bad);

    // sampler2D 输入契约：数组视图、sRGB/其它格式、超设备维度、多层都在 GPU 之前拒绝。
    // sampler2D input contract: array views, sRGB/other formats, oversized dimensions and multi-layer are rejected
    // before GPU work.
    dm_vk_texture bad_tex = texture;
    bad_tex.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    CHECK(dm_vk_sample_draw(&vk, vert, frag, &bad_tex, &nearest, 4, 4, &canvas, 1.0f, 0.0f) == VK_ERROR_FEATURE_NOT_PRESENT);
    bad_tex = texture; bad_tex.format = VK_FORMAT_R8G8B8A8_SRGB;
    CHECK(dm_vk_sample_draw(&vk, vert, frag, &bad_tex, &nearest, 4, 4, &canvas, 1.0f, 0.0f) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    bad_tex = texture; bad_tex.width = vk.properties.limits.maxImageDimension2D + 1;
    CHECK(dm_vk_sample_draw(&vk, vert, frag, &bad_tex, &nearest, 4, 4, &canvas, 1.0f, 0.0f) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    bad_tex = texture; bad_tex.layers = 2;
    CHECK(dm_vk_sample_draw(&vk, vert, frag, &bad_tex, &nearest, 4, 4, &canvas, 1.0f, 0.0f) == VK_ERROR_FEATURE_NOT_PRESENT);

    // 采样器在 GPU 完成后销毁：销毁后再次用新建采样器绘制仍正确。
    // Sampler destroyed after GPU completion: a fresh sampler still renders correctly afterwards.
    dm_vk_sampler_destroy(&vk, &nearest);
    CHECK(nearest.handle == VK_NULL_HANDLE);
    dm_vk_sampler again; VK_CHECK(dm_vk_sampler_create(&vk, &base, &again));
    memset(out, 0, sizeof out);
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &texture, &again, 4, 4, &canvas, 1.0f, 0.0f));
    CHECK(out[0] == kTex[0][0][0] && out[3] == 255);

    // 非法描述：不修改 out、不崩溃。 | invalid descriptors: out untouched, no crash.
    dm_vk_sampler sentinel; memset(&sentinel, 0x5a, sizeof sentinel);
    unsigned char before[sizeof sentinel]; memcpy(before, &sentinel, sizeof before);
    dm_vk_sampler_desc d = base;
    d.min_filter = (dm_vk_filter)9; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.mag_filter = (dm_vk_filter)7; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.lod_min = NAN; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.lod_max = INFINITY; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.lod_min = 2; d.lod_max = 1; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.max_aniso = 2.0f; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.compare_enable = 1; CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.address_u = DM_VK_ADDRESS_CLAMP_TO_BORDER; d.has_border = 1;
    CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.address_v = DM_VK_ADDRESS_MIRROR_CLAMP_TO_EDGE;
    CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    d = base; d.lod_bias = (float)vk.properties.limits.maxSamplerLodBias + 1.0f;
    CHECK(dm_vk_sampler_create(&vk, &d, &sentinel) != VK_SUCCESS);
    CHECK(!memcmp(before, &sentinel, sizeof before));
    sentinel.handle = VK_NULL_HANDLE;
    dm_vk_sampler_destroy(&vk, &sentinel);   // 二次销毁安全 | repeat destroy is safe
    dm_vk_sampler_destroy(&vk, &sentinel);

    // NVN 参数转换：默认值、未知枚举、未实现组合。 | NVN conversion: defaults, unknown enums, unimplemented combos.
    dm_vk_nvn_sampler nvn; memset(&nvn, 0, sizeof nvn);
    nvn.minf = 4; nvn.magf = 1; nvn.ws = nvn.wt = nvn.wr = 1; nvn.lod_min = -1000; nvn.lod_max = 1000;
    nvn.lod_bias = 0; nvn.aniso = 1; nvn.cmp_func = 2;
    dm_vk_sampler converted; VK_CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &converted));
    CHECK(converted.handle != VK_NULL_HANDLE);
    dm_vk_sampler_destroy(&vk, &converted);
    nvn.minf = 9; CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &sentinel) != VK_SUCCESS);
    nvn.minf = 4; nvn.cmp_mode = 1; CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &sentinel) != VK_SUCCESS);
    nvn.cmp_mode = 0; nvn.ws = 5; CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &sentinel) != VK_SUCCESS);
    nvn.ws = 1; nvn.aniso = 4; CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &sentinel) != VK_SUCCESS);
    // 非 mipmap 的 min filter 0：Metal NotMipmapped 只采样 level 0，正 lod_min 被精确定义为 0（映射结论，非像素证明）。
    // Non-mipmapped min filter 0: Metal NotMipmapped samples level 0, so a positive lod_min is defined as 0 (mapping
    // result only, not a pixel proof).
    nvn.aniso = 1; nvn.minf = 0; nvn.lod_min = 5.0f; nvn.lod_max = 5.0f;
    VK_CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &converted));
    dm_vk_sampler_destroy(&vk, &converted);

    dm_vk_sampler_destroy(&vk, &linear);
    dm_vk_sampler_destroy(&vk, &sclamp);
    dm_vk_sampler_destroy(&vk, &srepeat);
    dm_vk_sampler_destroy(&vk, &again);
    dm_vk_texture_destroy(&vk, &texture);
    dm_vk_destroy(&vk);
    printf("vulkan_sampler: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
