#include "../../gfx/nvn/nvn_tex.h"
#include "../../gfx/tex/nvn_deswizzle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

typedef struct { int format; unsigned width, height; } texture_case;

int main(void) {
    static const texture_case cases[] = {
        // Formats and dimensions from the real scene diagnostic (reuse-off capture).
        {0x44, 420, 128}, {0x44, 148, 24}, {0x44, 48, 48}, {0x44, 404, 1080},
        {0x44, 7992, 164}, {0x44, 20, 1004}, {0x44, 52, 12},
        {0x01, 2048, 2048},
        {0x25, 64, 64}, {0x48, 256, 256}, {0x29, 64, 64}, {0x49, 64, 64}, {0x46, 64, 64},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        nvn_texdesc desc = {.target = 1, .format = cases[i].format, .width = cases[i].width,
                            .height = cases[i].height, .depth = 1, .levels = 1};
        nvn_texlayout layout;
        nvn_tex_layout(&desc, &layout);
        uint32_t row_bytes, rows, slices, bpb;
        uint64_t base;
        unsigned th, td;
        CHECK(!nvn_deswizzle_geom(&desc, 0, 0, &row_bytes, &rows, &slices, &base, &th, &td, &bpb));
        CHECK(row_bytes && rows && bpb);
        size_t linear_size = (size_t)row_bytes * rows;
        uint8_t *linear = malloc(linear_size), *storage = calloc(1, layout.size), *roundtrip = calloc(1, linear_size);
        CHECK(linear && storage && roundtrip);
        for (size_t p = 0; p < linear_size; p++) linear[p] = (uint8_t)(p * 37u + i * 11u);
        CHECK(!nvn_swizzle(&desc, 0, 0, linear, row_bytes, storage));
        CHECK(!nvn_deswizzle(&desc, 0, 0, storage, roundtrip, row_bytes));
        CHECK(!memcmp(linear, roundtrip, linear_size));
        free(linear); free(storage); free(roundtrip);
    }
    puts("PASS: production deswizzle round-trip for scene formats/sizes");
    return 0;
}
