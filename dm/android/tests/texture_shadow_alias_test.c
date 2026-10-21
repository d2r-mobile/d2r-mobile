// 深度比较别名：颜色格式上的 Dref 采样必须改走同字节深度格式，且上传字节与原格式逐字节一致。
// Depth-compare alias: Dref sampling on a color format must use a same-byte depth format, and the uploaded bytes
// must match the original format byte for byte.
#include "texture_shadow_alias.h"
#include "nvn_tex.h"
#include "nvn_deswizzle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

// 生产路径采样用的 NVN 格式（与 descriptor_image 同一判定）。
// The NVN format the production path samples with (same decision as descriptor_image).
static int sampled_format(uint32_t type, int storage, int format) {
    int alias = texture_shadow_alias_format(type, storage, format);
    return alias ? alias : format;
}
static int is_depth_format(int format) { return format >= 0x32 && format <= 0x36; }

int main(void) {
    // 设备实测绑定：R16_UNORM 立方体落在阴影比较槽（type 0x405）上，必须以深度格式采样。
    // Observed device binding: an R16_UNORM cube on a shadow-compare slot (type 0x405) must sample as a depth format.
    CHECK(is_depth_format(sampled_format(0x405, 0, 0x06)));
    CHECK(sampled_format(0x405, 0, 0x06) == 0x32);
    CHECK(sampled_format(0x503, 0, 0x0a) == 0x34);
    // 已是深度格式、非比较槽、storage 与其他颜色格式保持原样。
    // Already-depth formats, non-compare slots, storage and other color formats are unchanged.
    CHECK(sampled_format(0x503, 0, 0x32) == 0x32);
    CHECK(sampled_format(0x005, 0, 0x06) == 0x06);
    CHECK(sampled_format(0x405, 1, 0x06) == 0x06);
    CHECK(sampled_format(0x403, 0, 0x25) == 0x25);
    CHECK(sampled_format(0x403, 0, 0x05) == 0x05);
    // 别名序号不与原序号或低位桶冲突。
    // The alias serial differs from the original but keeps the low hash bits.
    CHECK(texture_shadow_alias_serial(44) != 44 && (texture_shadow_alias_serial(44) & 4095) == 44);

    // 用生产排布/解排布帮助函数确认：同一块客体字节按 R16 与按 DEPTH16 解出来逐字节相同（6 层立方体全部层）。
    // Using the production layout/deswizzle helpers: the same guest bytes deswizzle identically as R16 and as DEPTH16 (all 6 cube faces).
    nvn_texdesc color = {0};
    color.target = 8; color.width = 128; color.height = 128; color.depth = 1; color.levels = 1; color.format = 0x06;
    nvn_texdesc depth = color; depth.format = sampled_format(0x405, 0, 0x06);
    nvn_texlayout lc, ld; nvn_tex_layout(&color, &lc); nvn_tex_layout(&depth, &ld);
    CHECK(lc.size == ld.size && lc.layers == ld.layers && lc.layers == 6 && lc.layer_size == ld.layer_size);
    int bw, bh; CHECK(nvn_fmt_bpb(0x06, &bw, &bh) == nvn_fmt_bpb(0x32, &bw, &bh));
    uint8_t *guest = malloc(lc.size), *a = malloc(128 * 128 * 2), *b = malloc(128 * 128 * 2);
    CHECK(guest && a && b);
    for (uint64_t i = 0; i < lc.size; i++) guest[i] = (uint8_t)(i * 2654435761u >> 13);
    for (unsigned layer = 0; layer < 6; layer++) {
        CHECK(!nvn_deswizzle(&color, 0, layer, guest, a, 256));
        CHECK(!nvn_deswizzle(&depth, 0, layer, guest, b, 256));
        CHECK(!memcmp(a, b, 128 * 128 * 2));
    }
    free(guest); free(a); free(b);
    puts("texture_shadow_alias_test OK");
    return 0;
}
