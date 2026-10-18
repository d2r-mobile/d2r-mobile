// Offscreen HUD acceptance for #111: geometry from dm_tc_layout, independent
// hit/background/blend/outside oracle, resize and invalid preflight, PNGs.
#include "touch_hud.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int checks;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } checks++; } while (0)

static const float BG[4][4] = {
    {0.10f, 0.16f, 0.24f, 1.00f}, {0.24f, 0.10f, 0.14f, 0.90f},
    {0.08f, 0.22f, 0.18f, 0.85f}, {0.20f, 0.18f, 0.08f, 0.95f}};
static const float NORMAL[4] = {0.15f, 0.65f, 0.95f, 0.55f};
static const float PRESSED[4] = {0.95f, 0.35f, 0.15f, 0.75f};
static const float UNAVAILABLE[4] = {0.40f, 0.40f, 0.40f, 0.25f};

static void bg_at(uint32_t x, uint32_t y, uint32_t w, uint32_t h, float out[4])
{
    float u = ((float)x + 0.5f) / (float)w, v = ((float)y + 0.5f) / (float)h;
    for (int c = 0; c < 4; c++) {
        float top = BG[0][c] + (BG[1][c] - BG[0][c]) * u;
        float bottom = BG[2][c] + (BG[3][c] - BG[2][c]) * u;
        out[c] = top + (bottom - top) * v;
    }
}

static void blend(const float bg[4], const float fg[4], float out[4])
{
    for (int c = 0; c < 3; c++) out[c] = fg[c] * fg[3] + bg[c] * (1.0f - fg[3]);
    out[3] = fg[3] + bg[3] * (1.0f - fg[3]);   // alpha factor ONE, as configured
}

static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static uint32_t crc32_of(const unsigned char *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t adler32_of(const unsigned char *p, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

static int png_write(const char *path, const uint32_t *rgba, uint32_t w, uint32_t h)
{
    size_t raw = (size_t)h * (1u + (size_t)w * 4u);
    unsigned char *data = calloc(1, raw);
    if (!data) return 0;
    const unsigned char *src = (const unsigned char *)rgba;
    for (uint32_t y = 0; y < h; y++) {
        unsigned char *row = data + (size_t)y * (1u + (size_t)w * 4u);
        row[0] = 0;
        memcpy(row + 1, src + (size_t)y * w * 4u, (size_t)w * 4u);
    }
    size_t blocks = (raw + 65534u) / 65535u;
    size_t zlen = 2 + blocks * 5u + raw + 4u;
    unsigned char *z = malloc(zlen);
    if (!z) { free(data); return 0; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    for (size_t off = 0; off < raw; off += 65535u) {
        size_t len = raw - off < 65535u ? raw - off : 65535u;
        z[zp++] = (unsigned char)(off + len == raw ? 1 : 0);
        z[zp++] = (unsigned char)(len & 0xff); z[zp++] = (unsigned char)(len >> 8);
        z[zp++] = (unsigned char)(~len & 0xff); z[zp++] = (unsigned char)((~len >> 8) & 0xff);
        memcpy(z + zp, data + off, len); zp += len;
    }
    put32(z + zp, adler32_of(data, raw)); zp += 4;
    free(data);

    FILE *f = fopen(path, "wb");
    if (!f) { free(z); return 0; }
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    struct chunk { const char *type; const unsigned char *p; size_t n; } chunks[3];
    unsigned char ihdr[13];
    put32(ihdr, w); put32(ihdr + 4, h); ihdr[8] = 8; ihdr[9] = 6;
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunks[0] = (struct chunk){"IHDR", ihdr, sizeof ihdr};
    chunks[1] = (struct chunk){"IDAT", z, zp};
    chunks[2] = (struct chunk){"IEND", NULL, 0};
    for (int i = 0; i < 3; i++) {
        unsigned char len[4];
        put32(len, (uint32_t)chunks[i].n);
        fwrite(len, 1, 4, f);
        fwrite(chunks[i].type, 1, 4, f);
        if (chunks[i].n) fwrite(chunks[i].p, 1, chunks[i].n, f);
        unsigned char *crcdata = malloc(4 + chunks[i].n);
        if (!crcdata) { fclose(f); free(z); return 0; }
        memcpy(crcdata, chunks[i].type, 4);
        if (chunks[i].n) memcpy(crcdata + 4, chunks[i].p, chunks[i].n);
        put32(len, crc32_of(crcdata, 4 + chunks[i].n));
        fwrite(len, 1, 4, f);
        free(crcdata);
    }
    int ok = ferror(f) == 0;
    fclose(f);
    free(z);
    return ok;
}

static uint32_t *alloc_pixels(dm_vk_pixels *p, uint32_t w, uint32_t h)
{
    p->width = w; p->height = h; p->capacity = (size_t)w * h * 4u;
    p->bytes = malloc(p->capacity);
    return (uint32_t *)p->bytes;
}

static int pixel_near(const uint32_t *p, uint32_t w, uint32_t x, uint32_t y,
                      const float expect[4], float tol)
{
    uint32_t v = p[(size_t)y * w + x];
    float got[4] = {(float)(v & 255u) / 255.0f, (float)((v >> 8) & 255u) / 255.0f,
                    (float)((v >> 16) & 255u) / 255.0f, (float)((v >> 24) & 255u) / 255.0f};
    for (int c = 0; c < 4; c++) if (!near(got[c], expect[c], tol)) {
        fprintf(stderr, "pixel (%u,%u) c%d got %.3f expect %.3f\n", x, y, c, got[c], expect[c]);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s hud.vert.spv hud.frag.spv [artifact-dir]\n", argv[0]); return 2; }
    const char *dir = argc > 3 ? argv[3] : ".";
    mkdir(dir, 0755);
    dm_touch_hud_snapshot s = {
        .width_pt = 667, .height_pt = 375, .density = 2,
        .pixel_width = 1334, .pixel_height = 750,
        .available_mask = (1u << DM_TC_COUNT) - 1u,
        .enabled_mask = (1u << DM_TC_COUNT) - 1u,
        .pressed_mask = 1u << DM_TC_MAIN,
    };
    CHECK(dm_touch_hud_validate(&s));
    CHECK(strstr(dm_touch_hud_art_status(), "unavailable") != NULL);
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, s.width_pt, s.height_pt);
    dm_touch_hud_geometry g[DM_TC_COUNT];
    dm_touch_hud_joy_geometry joy;
    CHECK(dm_touch_hud_build_geometry(&s, g, &joy));
    for (int i = 0; i < DM_TC_COUNT; i++) {
        CHECK(dm_tc_hit(&layout, g[i].center_x_pt, g[i].center_y_pt) == i);
        CHECK(g[i].radius_px > 0 && g[i].radius_px == g[i].radius_pt * 2.0f);
    }

    dm_vk vk;
    VkResult kr = dm_vk_create(&vk);
    if (kr != VK_SUCCESS) {
        fprintf(stderr, "VULKAN UNAVAILABLE: dm_vk_create=%d; no fake pass\n", (int)kr);
        return 77;
    }
    dm_vk_pixels px = {0};
    uint32_t *p = alloc_pixels(&px, s.pixel_width, s.pixel_height);
    CHECK(p);
    CHECK(dm_touch_hud_render(&vk, &s, &px, 0, argv[1], argv[2]));

    char path[1024];
    snprintf(path, sizeof path, "%s/touch-hud-pressed.png", dir);
    CHECK(png_write(path, p, s.pixel_width, s.pixel_height));

    int main_i = DM_TC_MAIN, skill_i = DM_TC_SKILL2;
    uint32_t mx = (uint32_t)(g[main_i].center_x_px + g[main_i].radius_px * 0.30f);
    uint32_t my = (uint32_t)(g[main_i].center_y_px + g[main_i].radius_px * 0.20f);
    float bg[4], expect[4];
    bg_at(mx, my, s.pixel_width, s.pixel_height, bg);
    blend(bg, PRESSED, expect);
    CHECK(pixel_near(p, s.pixel_width, mx, my, expect, 0.015f));

    bg_at(5, 5, s.pixel_width, s.pixel_height, bg);
    CHECK(pixel_near(p, s.pixel_width, 5, 5, bg, 0.012f));
    CHECK(bg[3] > 0.85f && bg[3] < 1.0f);

    // Normal and unavailable variants use independent expected colors.
    s.pressed_mask = 0;
    CHECK(dm_touch_hud_render(&vk, &s, &px, 0, argv[1], argv[2]));
    bg_at(mx, my, s.pixel_width, s.pixel_height, bg);
    blend(bg, NORMAL, expect);
    CHECK(pixel_near(p, s.pixel_width, mx, my, expect, 0.015f));
    snprintf(path, sizeof path, "%s/touch-hud-normal.png", dir);
    CHECK(png_write(path, p, s.pixel_width, s.pixel_height));

    s.available_mask &= ~(1u << skill_i);
    s.pressed_mask = 1u << skill_i;   // pressed must not override unavailable
    CHECK(dm_touch_hud_render(&vk, &s, &px, 0, argv[1], argv[2]));
    uint32_t sx = (uint32_t)(g[skill_i].center_x_px + g[skill_i].radius_px * 0.30f);
    uint32_t sy = (uint32_t)(g[skill_i].center_y_px + g[skill_i].radius_px * 0.20f);
    bg_at(sx, sy, s.pixel_width, s.pixel_height, bg);
    blend(bg, UNAVAILABLE, expect);
    CHECK(pixel_near(p, s.pixel_width, sx, sy, expect, 0.015f));
    snprintf(path, sizeof path, "%s/touch-hud-unavailable.png", dir);
    CHECK(png_write(path, p, s.pixel_width, s.pixel_height));

    CHECK(dm_touch_hud_render(&vk, &s, &px, 1, argv[1], argv[2]));
    uint32_t edge = (uint32_t)(g[main_i].center_x_px - g[main_i].radius_px);
    uint32_t cy = (uint32_t)g[main_i].center_y_px;
    uint32_t ov = p[(size_t)cy * s.pixel_width + edge];
    CHECK((ov & 255u) >= 180u && ((ov >> 8) & 255u) <= 120u);
    snprintf(path, sizeof path, "%s/touch-hud-hit-overlay.png", dir);
    CHECK(png_write(path, p, s.pixel_width, s.pixel_height));

    // Resize/density change: same point layout transform, new pixel geometry.
    dm_touch_hud_snapshot r = s;
    r.width_pt = 812; r.height_pt = 375; r.density = 3;
    r.pixel_width = 2436; r.pixel_height = 1125;
    r.available_mask = (1u << DM_TC_COUNT) - 1u;
    r.pressed_mask = 0;
    CHECK(dm_touch_hud_validate(&r));
    dm_touch_hud_geometry g2[DM_TC_COUNT];
    dm_touch_hud_joy_geometry joy2;
    CHECK(dm_touch_hud_build_geometry(&r, g2, &joy2));
    CHECK(g2[main_i].center_x_px != g[main_i].center_x_px);
    CHECK(fabsf((g2[main_i].center_x_pt - g[main_i].center_x_pt) - (812.0f - 667.0f)) < 0.01f);
    CHECK(g2[main_i].center_y_pt == g[main_i].center_y_pt);   // same height, same layout scale
    dm_vk_pixels px2 = {0};
    uint32_t *p2 = alloc_pixels(&px2, r.pixel_width, r.pixel_height);
    CHECK(p2);
    r.pressed_mask = 0;
    CHECK(dm_touch_hud_render(&vk, &r, &px2, 0, argv[1], argv[2]));
    uint32_t mx2 = (uint32_t)(g2[main_i].center_x_px + g2[main_i].radius_px * 0.30f);
    uint32_t my2 = (uint32_t)(g2[main_i].center_y_px + g2[main_i].radius_px * 0.20f);
    bg_at(mx2, my2, r.pixel_width, r.pixel_height, bg);
    blend(bg, NORMAL, expect);
    CHECK(pixel_near(p2, r.pixel_width, mx2, my2, expect, 0.02f));
    snprintf(path, sizeof path, "%s/touch-hud-resized.png", dir);
    CHECK(png_write(path, p2, r.pixel_width, r.pixel_height));

    // Invalid preflight must not touch pixels or allocate GPU objects.
    uint32_t sentinel = p2[0];
    dm_touch_hud_snapshot bad = r;
    bad.width_pt = 0;
    CHECK(!dm_touch_hud_validate(&bad));
    CHECK(!dm_touch_hud_render(&vk, &bad, &px2, 0, argv[1], argv[2]));
    CHECK(p2[0] == sentinel);
    bad = r; bad.density = 2;   // 812*2 != 2436
    CHECK(!dm_touch_hud_validate(&bad));
    bad = r; bad.enabled_mask = 1u << 20;
    CHECK(!dm_touch_hud_validate(&bad));
    bad = r; bad.pixel_width = 16385; bad.width_pt = 16385; bad.density = 1; bad.pixel_height = 1; bad.height_pt = 1;
    CHECK(!dm_touch_hud_validate(&bad));

    free(px.bytes); free(px2.bytes);
    dm_vk_destroy(&vk);
    printf("touch HUD offscreen: %d checks, artifacts=%s\n", checks, dir);
    return 0;
}
