// 行距上传／读回与生命周期的 Vulkan 对应验收（把 #11 自有场景公式移到真实 Vulkan 资源路径）。
// 期望像素由本文件的公式直接算，不调用被测的 update/read 代码，也不复用共享布局当期望值来源。
// Vulkan-side acceptance for row-stride update/readback and lifetime (the #11 scene formulas moved onto the real Vulkan
// resource path). Expected pixels come from formulas in this file; the code under test is not used as an oracle, and the
// shared layout helper is only used to build the initial block-linear upload source.
#include "../vulkan/vk_host.h"
#include "../../gfx/tex/nvn_deswizzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define VK_CHECK(call) do{VkResult r=(call);if(r!=VK_SUCCESS){fprintf(stderr,"FAIL %s = %d\n",#call,r);exit(1);}}while(0)

static unsigned checks, failures;
#define EXPECT(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond); } } while (0)

enum { W = 8, H = 4 };

// 初始内容 A：像素 (x,y) 的字节；与 #11 场景同一套公式。 | Initial content A: bytes of pixel (x,y), same formula as the #11 scene.
static void pixel_a(unsigned x, unsigned y, uint8_t out[4]) {
    out[0] = (uint8_t)(x * 16 + y); out[1] = (uint8_t)(y * 16 + x); out[2] = 0x55; out[3] = 0xAA;
}
// 更新内容 B：与 A 在区域内的每个通道都不同，用来抓「没真正写进去」。 | Update content B: differs from A in every channel inside the region.
static void pixel_b(unsigned x, unsigned y, uint8_t out[4]) {
    out[0] = (uint8_t)(0x40 + x * 7 + y * 3); out[1] = (uint8_t)(0x80 + y * 5 + x * 2);
    out[2] = (uint8_t)(0x10 + x + y); out[3] = 0x22;
}
// 紧排更新内容 C（另一块区域，覆盖在 A 上）。 | Tight update content C (another region, painted over A).
static void pixel_c(unsigned x, unsigned y, uint8_t out[4]) {
    out[0] = (uint8_t)(0xC0 + x); out[1] = (uint8_t)(0x30 + y); out[2] = 0x77; out[3] = 0x88;
}

typedef struct { nvn_texdesc desc; nvn_texlayout layout; } shape;
static shape make_shape(void) {
    shape s = {.desc = {.target = 1, .format = 0x25, .width = W, .height = H, .depth = 1, .levels = 1}};
    nvn_tex_layout(&s.desc, &s.layout);
    return s;
}
// 初始上传走 NVN 块线性存储：线性 A → nvn_swizzle → 上传。 | The initial upload goes through NVN block-linear storage: linear A -> nvn_swizzle -> upload.
static void make_texture(dm_vk *vk, const shape *s, dm_vk_texture *out) {
    uint8_t linear[W * H * 4], *storage = calloc(1, s->layout.size);
    CHECK(storage);
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) pixel_a(x, y, linear + (y * W + x) * 4);
    CHECK(!nvn_swizzle(&s->desc, 0, 0, linear, W * 4, storage));
    VK_CHECK(dm_vk_texture_upload(vk, &s->desc, storage, s->layout.size, out));
    free(storage);
}
static void expect_pixels(const uint8_t *rows, size_t row_bytes, int updated, const char *what) {
    unsigned bad = 0;
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) {
        uint8_t want[4]; int in_region = x >= 2 && x < 6 && y >= 1 && y < 3;
        if (in_region && updated) pixel_b(x, y, want); else pixel_a(x, y, want);
        bad += memcmp(rows + y * row_bytes + x * 4, want, 4) != 0;
    }
    if (bad) fprintf(stderr, "  %s: %u/32 pixels differ\n", what, bad);
    EXPECT(bad == 0);
}

int main(int argc, char **argv) {
    dm_vk vk = {0};
    // --validation：请求真实验证层并通过错误计数参与判定；层不可用时返回 77（CTest SKIP），不当作通过。
    // --validation: request the real layer and let its error count decide; when unavailable return 77 (CTest SKIP), never a pass.
    int want_validation = argc > 1 && !strcmp(argv[1], "--validation");
    dm_vk_options options = DM_VK_OPTIONS_INIT;
    options.validation = DM_VK_VALIDATION_REQUIRED;
    VkResult created = dm_vk_create_ex(want_validation ? &options : NULL, &vk);
    if (want_validation && created == VK_ERROR_LAYER_NOT_PRESENT) {
        printf("vulkan_texture_rows: VK_LAYER_KHRONOS_validation unavailable; SKIP (validation not run, not a pass)\n");
        return 77;
    }
    VK_CHECK(created);
    if (want_validation) {
        CHECK(dm_vk_validation_enabled(&vk));
        printf("validation: %s (instance %u.%u, driver %u.%u)\n", dm_vk_validation_status(&vk),
               VK_VERSION_MAJOR(vk.api_version), VK_VERSION_MINOR(vk.api_version),
               VK_VERSION_MAJOR(vk.driver_api_version), VK_VERSION_MINOR(vk.driver_api_version));
    }
    shape s = make_shape();
    dm_vk_texture texture = {0};
    make_texture(&vk, &s, &texture);
    const dm_vk_region full = {0, 0, W, H}, region = {2, 1, 4, 2};

    // 1. 整帧读回，行距 48 > 紧排 32：区域外的填充必须保持调用者的哨兵值。
    // 1. Whole-frame readback at row stride 48 > tight 32: padding outside the extent must keep the caller's sentinel.
    uint8_t *rows = malloc(48 * H);
    CHECK(rows); memset(rows, 0x5C, 48 * H);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &full, 48, rows, 48 * H) == VK_SUCCESS);
    expect_pixels(rows, 48, 0, "initial frame");
    unsigned pad_bad = 0;
    for (unsigned y = 0; y < H; y++) for (unsigned i = 32; i < 48; i++) pad_bad += rows[y * 48 + i] != 0x5C;
    if (pad_bad) fprintf(stderr, "  readback padding: %u bytes changed\n", pad_bad);
    EXPECT(pad_bad == 0);

    // 2. 行距 40 > 紧排 16 的更新源，只写 region{2,1,4,2}；行距里的填充是 0xE7，不能被当成像素读进去。
    // 2. Update source at row stride 40 > tight 16 over region{2,1,4,2}; padding inside the stride is 0xE7 and must not be read as pixels.
    uint8_t source[2 * 40];
    memset(source, 0xE7, sizeof source);
    for (unsigned y = 0; y < 2; y++) for (unsigned x = 0; x < 4; x++) pixel_b(2 + x, 1 + y, source + y * 40 + x * 4);
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 0, &region, source, sizeof source, 40) == VK_SUCCESS);

    // 3. 再读整帧：区域内变 B，区域外仍是 A（写没有溢出），读回填充仍是哨兵。
    // 3. Read the whole frame again: B inside the region, still A outside (no spill), readback padding still the sentinel.
    memset(rows, 0x5C, 48 * H);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &full, 48, rows, 48 * H) == VK_SUCCESS);
    expect_pixels(rows, 48, 1, "after strided update");
    pad_bad = 0;
    for (unsigned y = 0; y < H; y++) for (unsigned i = 32; i < 48; i++) pad_bad += rows[y * 48 + i] != 0x5C;
    EXPECT(pad_bad == 0);

    // 4. 同一区域按紧排（row_bytes = 0）读回，逐字节等于 B。
    // 4. Read the same region tightly (row_bytes = 0); every byte must equal B.
    uint8_t tight_region[4 * 2 * 4]; memset(tight_region, 0x00, sizeof tight_region);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &region, 0, tight_region, sizeof tight_region) == VK_SUCCESS);
    unsigned region_bad = 0;
    for (unsigned y = 0; y < 2; y++) for (unsigned x = 0; x < 4; x++) {
        uint8_t want[4]; pixel_b(2 + x, 1 + y, want);
        region_bad += memcmp(tight_region + (y * 4 + x) * 4, want, 4) != 0;
    }
    EXPECT(region_bad == 0);

    // 5. 读回目的缓冲用行距 24 > 紧排 16：目的里的填充保持原样。
    // 5. Read into a destination at row stride 24 > tight 16: destination padding stays untouched.
    uint8_t dest_rows[2 * 24]; memset(dest_rows, 0x33, sizeof dest_rows);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &region, 24, dest_rows, sizeof dest_rows) == VK_SUCCESS);
    unsigned dest_bad = 0;
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned i = 16; i < 24; i++) dest_bad += dest_rows[y * 24 + i] != 0x33;
        for (unsigned x = 0; x < 4; x++) { uint8_t want[4]; pixel_b(2 + x, 1 + y, want);
            dest_bad += memcmp(dest_rows + y * 24 + x * 4, want, 4) != 0; }
    }
    EXPECT(dest_bad == 0);

    // 6. 紧排更新另一块区域 {0,0,2,2}，内容 C；随后整帧读回 A/C/B 三者都在。
    // 6. Tight update of another region {0,0,2,2} with content C; then all of A/C/B are present in the whole frame.
    uint8_t tight_source[2 * 2 * 4];
    for (unsigned y = 0; y < 2; y++) for (unsigned x = 0; x < 2; x++) pixel_c(x, y, tight_source + (y * 2 + x) * 4);
    const dm_vk_region corner = {0, 0, 2, 2};
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 0, &corner, tight_source, sizeof tight_source, 0) == VK_SUCCESS);
    memset(rows, 0x5C, 48 * H);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &full, 48, rows, 48 * H) == VK_SUCCESS);
    unsigned all_bad = 0;
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) {
        uint8_t want[4];
        if (x < 2 && y < 2) pixel_c(x, y, want); else if (x >= 2 && x < 6 && y >= 1 && y < 3) pixel_b(x, y, want);
        else pixel_a(x, y, want);
        all_bad += memcmp(rows + y * 48 + x * 4, want, 4) != 0;
    }
    EXPECT(all_bad == 0);

    // 7. 失败路径：参数／范围／容量错误必须拒绝，且不改动已有内容。
    // 7. Failure paths: bad arguments, ranges and capacities must be rejected without disturbing existing contents.
    uint8_t one[64]; memset(one, 0x44, sizeof one);
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 0, &region, one, sizeof one, 15) != VK_SUCCESS);   // 行距 < 紧排 16
    uint8_t short_source[24]; memset(short_source, 0x44, sizeof short_source);
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 0, &region, short_source, sizeof short_source, 0) != VK_SUCCESS);   // 容量 24 < 紧排 2×16
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 0, &region, NULL, sizeof one, 40) != VK_SUCCESS);  // 空源
    const dm_vk_region overflow = {7, 1, 2, 1};
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 0, &overflow, one, sizeof one, 8) != VK_SUCCESS);  // x+宽 越界
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 1, 0, &region, one, sizeof one, 16) != VK_SUCCESS);   // 级越界
    EXPECT(dm_vk_texture_update_rows(&vk, &texture, 0, 1, &region, one, sizeof one, 16) != VK_SUCCESS);   // 层越界
    EXPECT(dm_vk_texture_update_rows(&vk, NULL, 0, 0, &region, one, sizeof one, 16) != VK_SUCCESS);       // 空贴图
    uint8_t tiny[8];
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &region, 0, tiny, sizeof tiny) != VK_SUCCESS);    // 目的容量不足
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &region, 15, rows, 48 * H) != VK_SUCCESS);        // 读行距 < 紧排
    // 行操作接受 UNORM 与 sRGB 两种 4 字节语义，其他格式仍必须拒绝：用同宽的 UINT 当反例。
    // Row operations accept both 4-byte semantics (UNORM, sRGB); any other format must still be refused: same-width UINT is the counterexample.
    dm_vk_texture other_format = texture; other_format.format = VK_FORMAT_R8G8B8A8_UINT;
    EXPECT(dm_vk_texture_update_rows(&vk, &other_format, 0, 0, &region, one, sizeof one, 16) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    EXPECT(dm_vk_texture_read_rows(&vk, &other_format, 0, 0, &region, 0, rows, 48 * H) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    // 失败之后内容不变：整帧仍应是 A/C/B。 | Contents unchanged after failures: the frame still shows A/C/B.
    memset(rows, 0x5C, 48 * H);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &full, 48, rows, 48 * H) == VK_SUCCESS);
    unsigned stable_bad = 0;
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) {
        uint8_t want[4];
        if (x < 2 && y < 2) pixel_c(x, y, want); else if (x >= 2 && x < 6 && y >= 1 && y < 3) pixel_b(x, y, want);
        else pixel_a(x, y, want);
        stable_bad += memcmp(rows + y * 48 + x * 4, want, 4) != 0;
    }
    EXPECT(stable_bad == 0);

    // 8. 生命周期：销毁后同参数重建，读回只能是新上传的 A，不残留旧内容。
    // 8. Lifetime: destroy and rebuild with the same descriptor; the readback must be the fresh A, with no stale contents.
    dm_vk_texture_destroy(&vk, &texture);
    EXPECT(!texture.image && !texture.memory && !texture.view);
    make_texture(&vk, &s, &texture);
    memset(rows, 0x5C, 48 * H);
    EXPECT(dm_vk_texture_read_rows(&vk, &texture, 0, 0, &full, 48, rows, 48 * H) == VK_SUCCESS);
    expect_pixels(rows, 48, 0, "recreated texture");
    dm_vk_texture_destroy(&vk, &texture);
    free(rows);
    printf("vulkan_texture_rows: %u checks, %u failures (GPU: %s)\n", checks, failures, vk.properties.deviceName);
    if (want_validation) {
        uint32_t errors = dm_vk_validation_errors(&vk), warnings = dm_vk_validation_warnings(&vk);
        printf("validation: errors %u, warnings %u\n", errors, warnings);
        if (errors) { char first[256] = {0}; dm_vk_validation_first_error(&vk, first, sizeof first); fprintf(stderr, "  first validation error: %s\n", first); }
        EXPECT(errors == 0);   // 真实验证层报错即失败 | any real validation error fails the run
    }
    dm_vk_destroy(&vk);
    CHECK(!vk.device && !vk.instance);
    return failures ? 1 : 0;
}
