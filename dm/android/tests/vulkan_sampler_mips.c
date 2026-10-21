// #13 多级纹理与 LOD 像素证明：三级纯色 RGBA8 纹理，nearest/linear mip 选择、分数 LOD 插值、LOD clamp、
// NVN 非 mipmap 固定在 level 0，以及非法输入在 GPU 之前的拒绝。
// #13 multi-mip texture and LOD pixel proof: a three-level solid-colour RGBA8 texture covering nearest/linear mip
// selection, fractional-LOD interpolation, LOD clamp, NVN non-mipmapped level-0 behaviour, and pre-GPU rejection of
// malformed input.
#include "../vulkan/vk_host.h"
#include "../vulkan/vk_sampler.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define VK_CHECK(c) do { VkResult r_ = (c); if (r_ != VK_SUCCESS) { fprintf(stderr, "FAIL %d: %s -> %d\n", __LINE__, #c, (int)r_); exit(1); } } while (0)

static const uint8_t kColors[3][4] = {{200, 10, 10, 255}, {10, 200, 10, 255}, {10, 10, 200, 255}};

// 按字节比较，避免对颜色表做 uint32_t 解引用（对齐/有效类型问题）。
// Compare bytes to avoid uint32_t dereference of the colour table (alignment/effective-type concerns).
static int same_rgba(const void *pixel, int level) { return !memcmp(pixel, kColors[level], 4); }

// 生成指定级别尺寸的纯色紧排数据 | build tight solid-colour data for one level
static uint8_t *solid(uint32_t w, uint32_t h, int level) {
    uint8_t *p = malloc((size_t)w * h * 4);
    for (uint32_t i = 0; i < w * h; i++) memcpy(p + i * 4, kColors[level], 4);
    return p;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <fullscreen.vert.spv> <sample2d.frag.spv>\n", argv[0]); return 2; }
    const char *vert = argv[1], *frag = argv[2];
    dm_vk vk; VK_CHECK(dm_vk_create(&vk));

    uint8_t *l0 = solid(4, 4, 0), *l1 = solid(2, 2, 1), *l2 = solid(1, 1, 2);
    dm_vk_mip_level tight[3] = {{l0, 4 * 4 * 4, 0}, {l1, 2 * 2 * 4, 0}, {l2, 4, 0}};
    dm_vk_texture tex; memset(&tex, 0, sizeof tex);
    VK_CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, tight, &tex));
    CHECK(tex.levels == 3 && tex.width == 4 && tex.height == 4 && tex.layers == 1);
    CHECK(tex.format == VK_FORMAT_R8G8B8A8_UNORM && tex.view_type == VK_IMAGE_VIEW_TYPE_2D);

    uint32_t px[4 * 4];
    dm_vk_pixels canvas = {.width = 4, .height = 4, .bytes = (uint8_t *)px, .capacity = sizeof px};

    // NVN minf=2：mip nearest；整数 LOD 必须精确命中 level 0/1/2。
    // NVN minf=2: mip nearest; integer LOD must hit level 0/1/2 exactly.
    dm_vk_nvn_sampler nvn; memset(&nvn, 0, sizeof nvn);
    nvn.minf = 2; nvn.magf = 0; nvn.ws = nvn.wt = nvn.wr = 1; nvn.lod_min = 0; nvn.lod_max = 2;
    nvn.aniso = 1; nvn.cmp_func = 2;
    dm_vk_sampler nearest_mip; VK_CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &nearest_mip));
    for (int level = 0; level < 3; level++) {
        memset(px, 0, sizeof px);
        VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &tex, &nearest_mip, 4, 4, &canvas, 1.0f, (float)level));
        int bad = 0;
        for (int i = 0; i < 16; i++) if (!same_rgba(&px[i], level)) bad++;
        if (bad) fprintf(stderr, "nearest LOD %d: %d/%d texels differ\n", level, bad, 16);
        CHECK(bad == 0);
    }

    // NVN minf=4：mip LINEAR；分数 LOD 0.5 必须是两级混合，且不同于任一纯色。
    // NVN minf=4: mip LINEAR; fractional LOD 0.5 must blend levels and differ from either solid colour.
    nvn.minf = 4;
    dm_vk_sampler linear_mip; VK_CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &linear_mip));
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &tex, &linear_mip, 4, 4, &canvas, 1.0f, 0.5f));
    const int want0_1[4] = {(200 + 10) / 2, (10 + 200) / 2, (10 + 10) / 2, 255};
    int blended = 1, linear_bad = 0;
    for (int i = 0; i < 16; i++) {
        const uint8_t *got = (const uint8_t *)&px[i];
        for (int c = 0; c < 4; c++) if (abs((int)got[c] - want0_1[c]) > 2) linear_bad++;
        if (same_rgba(&px[i], 0) || same_rgba(&px[i], 1)) blended = 0;
    }
    CHECK(linear_bad == 0);
    CHECK(blended == 1);            // 不是任一纯色：与 nearest 结果可区分 | not either endpoint, distinguishable from nearest
    printf("linear mip lod0.5: %d channel mismatches, blended=%d\n", linear_bad, blended);

    // 同一分数 LOD 下 mip nearest 必须等于某一级纯色（驱动可在 0/1 之间取最近），且不等于混合值。
    // At the same fractional LOD, mip nearest must equal one endpoint (driver may pick level 0 or 1) and never the blend.
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &tex, &nearest_mip, 4, 4, &canvas, 1.0f, 0.5f));
    int nearest_endpoint = 1, nearest_blend = 0;
    for (int i = 0; i < 16; i++) {
        if (!same_rgba(&px[i], 0) && !same_rgba(&px[i], 1)) nearest_endpoint = 0;
        if (abs((int)((const uint8_t *)&px[i])[0] - want0_1[0]) <= 2 &&
            abs((int)((const uint8_t *)&px[i])[1] - want0_1[1]) <= 2) nearest_blend = 1;
    }
    CHECK(nearest_endpoint == 1);
    CHECK(nearest_blend == 0);

    // LOD clamp：minLod=1,maxLod=1 时 lod 0 与 lod 2 都落在 level 1；minLod=2 时 lod 0 落在 level 2。
    // LOD clamp: with minLod=maxLod=1, lod 0 and lod 2 both resolve to level 1; minLod=2 maps lod 0 to level 2.
    dm_vk_sampler_desc clamp; memset(&clamp, 0, sizeof clamp);
    clamp.min_filter = clamp.mag_filter = DM_VK_FILTER_NEAREST; clamp.mipmap = DM_VK_MIP_NEAREST;
    clamp.address_u = clamp.address_v = clamp.address_w = DM_VK_ADDRESS_CLAMP_TO_EDGE;
    clamp.max_aniso = 1.0f; clamp.lod_min = 1; clamp.lod_max = 1;
    dm_vk_sampler clamp_mid; VK_CHECK(dm_vk_sampler_create(&vk, &clamp, &clamp_mid));
    for (int lod = 0; lod <= 2; lod += 2) {
        VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &tex, &clamp_mid, 4, 4, &canvas, 1.0f, (float)lod));
        CHECK(same_rgba(&px[0], 1));
    }
    clamp.lod_min = 2; clamp.lod_max = 2;
    dm_vk_sampler clamp_top; VK_CHECK(dm_vk_sampler_create(&vk, &clamp, &clamp_top));
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &tex, &clamp_top, 4, 4, &canvas, 1.0f, 0.0f));
    CHECK(same_rgba(&px[0], 2));

    // NVN 非 mipmap minf=0：有效 LOD 固定 0，即使 lod=2 也读 level 0。
    // NVN non-mipmapped minf=0: effective LOD 0, so lod 2 still reads level 0.
    memset(&nvn, 0, sizeof nvn);
    nvn.minf = 0; nvn.magf = 0; nvn.ws = nvn.wt = nvn.wr = 1; nvn.lod_min = 0; nvn.lod_max = 0; nvn.aniso = 1;
    dm_vk_sampler nonmip; VK_CHECK(dm_vk_sampler_from_nvn(&vk, &nvn, &nonmip));
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &tex, &nonmip, 4, 4, &canvas, 1.0f, 2.0f));
    CHECK(same_rgba(&px[0], 0));

    // 行距变体：带 8 字节填充的 row_bytes 仍必须逐行正确取样。
    // Stride variant: row_bytes with 8 bytes of padding must still sample the same rows.
    uint8_t *padded = calloc(1, (4 * 4 + 8) * 4);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
        memcpy(padded + (size_t)y * 24 + x * 4, kColors[0], 4);
    dm_vk_mip_level pad_levels[3] = {{padded, 24 * 3 + 16, 24}, {l1, 16, 0}, {l2, 4, 0}};
    dm_vk_texture padded_tex; memset(&padded_tex, 0, sizeof padded_tex);
    VK_CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, pad_levels, &padded_tex));
    VK_CHECK(dm_vk_sample_draw(&vk, vert, frag, &padded_tex, &nearest_mip, 4, 4, &canvas, 1.0f, 0.0f));
    CHECK(same_rgba(&px[0], 0) && same_rgba(&px[15], 0));
    dm_vk_texture_destroy(&vk, &padded_tex);
    free(padded);

    // 单级仍可用（保持既有行为） | single level still works (existing behaviour preserved)
    dm_vk_mip_level one[1] = {{l0, 4 * 4 * 4, 0}};
    dm_vk_texture one_tex; memset(&one_tex, 0, sizeof one_tex);
    VK_CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 1, one, &one_tex));
    CHECK(one_tex.levels == 1);
    dm_vk_texture_destroy(&vk, &one_tex);

    // 非法输入：GPU 之前拒绝，out 不变。 | Invalid input: rejected before GPU, out untouched.
    dm_vk_mip_level bad_levels[4];
    memcpy(bad_levels, tight, sizeof tight);
    dm_vk_texture sentinel; memset(&sentinel, 0x5a, sizeof sentinel);
    unsigned char before[sizeof sentinel]; memcpy(before, &sentinel, sizeof before);
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 0, bad_levels, &sentinel) != VK_SUCCESS);
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 4, bad_levels, &sentinel) != VK_SUCCESS);   // 超出完整链 | beyond full chain
    bad_levels[0].size = 4 * 4 * 4 - 1;
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, bad_levels, &sentinel) != VK_SUCCESS);   // 截断 | truncated
    memcpy(bad_levels, tight, sizeof tight); bad_levels[0].row_bytes = 12;
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, bad_levels, &sentinel) != VK_SUCCESS);   // 行距过小 | stride too small
    memcpy(bad_levels, tight, sizeof tight); bad_levels[0].row_bytes = 18;
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, bad_levels, &sentinel) != VK_SUCCESS);   // 非 4 倍数 | not multiple of 4
    memcpy(bad_levels, tight, sizeof tight); bad_levels[1].bytes = NULL;
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, bad_levels, &sentinel) != VK_SUCCESS);   // 空指针 | null
    memcpy(bad_levels, tight, sizeof tight); bad_levels[2].size = 0;
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, bad_levels, &sentinel) != VK_SUCCESS);
    CHECK(dm_vk_texture_upload_mips(&vk, 0, 4, 1, tight, &sentinel) != VK_SUCCESS);
    CHECK(dm_vk_texture_upload_mips(&vk, vk.properties.limits.maxImageDimension2D + 1, 4, 1, tight, &sentinel) != VK_SUCCESS);
    CHECK(dm_vk_texture_upload_mips(&vk, 4, 4, 3, NULL, &sentinel) != VK_SUCCESS);
    CHECK(!memcmp(before, &sentinel, sizeof before));

    dm_vk_sampler_destroy(&vk, &nearest_mip);
    dm_vk_sampler_destroy(&vk, &linear_mip);
    dm_vk_sampler_destroy(&vk, &clamp_mid);
    dm_vk_sampler_destroy(&vk, &clamp_top);
    dm_vk_sampler_destroy(&vk, &nonmip);
    dm_vk_texture_destroy(&vk, &tex);
    free(l0); free(l1); free(l2);
    dm_vk_destroy(&vk);
    printf("vulkan_sampler_mips: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
