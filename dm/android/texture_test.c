// 无游戏数据的纹理冒烟测试：复用 nvn_tex / nvn_deswizzle，覆盖级、层和非法参数。
// Game-free texture smoke test: reuse nvn_tex / nvn_deswizzle, covering mips, layers and invalid parameters.
#include "../gfx/tex/nvn_deswizzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void roundtrip(int format, int target, uint32_t depth) {
    nvn_texdesc d = {.target = target, .format = format, .width = 65,
        .height = 37, .depth = depth, .levels = 6};
    nvn_texlayout layout;
    nvn_tex_layout(&d, &layout);
    uint8_t *storage = calloc(1, (size_t)layout.size + 64);
    CHECK(storage);
    memset(storage + layout.size, 0xab, 64);
    uint8_t *refs[6][6] = {{0}};
    for (unsigned layer = 0; layer < layout.layers; layer++) {
        CHECK(layer < 6);
        for (unsigned level = 0; level < d.levels; level++) {
            uint32_t rb, rows, slices, bpb; uint64_t base; unsigned th, td;
            CHECK(!nvn_deswizzle_geom(&d, level, layer, &rb, &rows, &slices, &base, &th, &td, &bpb));
            size_t n = (size_t)rb * rows * slices;
            refs[layer][level] = malloc(n);
            CHECK(refs[layer][level]);
            for (size_t i = 0; i < n; i++) refs[layer][level][i] = (uint8_t)(i * 37 + level * 13 + layer * 7);
            CHECK(!nvn_swizzle(&d, level, layer, refs[layer][level], rb, storage));
        }
    }
    for (unsigned layer = 0; layer < layout.layers; layer++) {
        for (unsigned level = 0; level < d.levels; level++) {
            uint32_t rb, rows, slices, bpb; uint64_t base; unsigned th, td;
            CHECK(!nvn_deswizzle_geom(&d, level, layer, &rb, &rows, &slices, &base, &th, &td, &bpb));
            size_t n = (size_t)rb * rows * slices;
            uint8_t *out = malloc(n);
            CHECK(out);
            CHECK(nvn_deswizzle(&d, level, layer, storage, out, rb - 1) == -1);
            CHECK(!nvn_deswizzle(&d, level, layer, storage, out, rb));
            CHECK(!memcmp(out, refs[layer][level], n));
            free(out); free(refs[layer][level]);
        }
    }
    uint8_t guard[64]; memset(guard, 0xab, sizeof guard);
    CHECK(!memcmp(storage + layout.size, guard, sizeof guard));
    uint8_t dummy[16];
    CHECK(nvn_deswizzle(&d, d.levels, 0, storage, dummy, 16) == -1);
    CHECK(nvn_deswizzle(&d, 0, layout.layers, storage, dummy, 16) == -1);
    free(storage);
}

int main(void) {
    const int formats[] = {0x25, 0x42, 0x44, 0x79};
    for (unsigned i = 0; i < sizeof formats / sizeof *formats; i++) {
        roundtrip(formats[i], 1, 1);
        roundtrip(formats[i], 4, 3);
        roundtrip(formats[i], 2, 5);
    }
    CHECK(nvn_bl_offset(0, 0, 0, 64, 8, 0, 0) == 0);
    CHECK(nvn_bl_offset(32, 0, 0, 64, 8, 0, 0) == 256);
    CHECK(nvn_bl_offset(0, 1, 0, 64, 8, 0, 0) == 16);
    puts("PASS: RGBA8 / BC1 / BC3 / ASTC, 2D / array / 3D, mips, bounds");
    return 0;
}
