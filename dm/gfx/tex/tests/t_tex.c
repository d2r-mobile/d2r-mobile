// 块线性解排布单测（秒级）。A 往返；B 手算地址；C 真数据对拍（Switch 块线性 vs PC 线性，含逐级偏移核对）；D 错块高负对照。
// 用法：t_tex <样本目录>（fetch_samples.py 产出）。样本缺失算红，不跳过。
// Block-linear deswizzle unit tests (seconds). A round trip; B hand-calculated addresses; C real-data cross-check (Switch block-linear vs PC linear, including per-level offset verification); D wrong-block-height negative control.
// Usage: t_tex <sample dir> (produced by fetch_samples.py). Missing samples count as red, not skipped.
#include "../nvn_deswizzle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t mipd(uint32_t s, unsigned l) { s >>= l; return s ? s : 1; }
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint8_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint8_t)rng; }

// 本级线性尺寸（测试侧独立算：块宽字节、块行、切片）
// Linear size of this level (computed independently on the test side: width-in-blocks bytes, block rows, slices)
static void dims(const nvn_texdesc *d, unsigned l, uint32_t *rb, uint32_t *rows, uint32_t *sl) {
    int bw, bh, bpb = nvn_fmt_bpb(d->format, &bw, &bh);
    *rb = (mipd(d->width, l) + bw - 1) / bw * bpb; *rows = (mipd(d->height, l) + bh - 1) / bh;
    *sl = d->target == 2 ? mipd(d->depth, l) : 1;
}

// ---------- A 往返：全部级 / 层先各写入不同随机数据，再全部解出比对（级间、层间重叠或越界都会红）----------
// ---------- A round trip: first write different random data into every level / layer, then decode them all and compare (overlap between levels or between layers, or out-of-bounds, all fail) ----------
static void roundtrip(const char *tag, int target, int fmt, uint32_t w, uint32_t h, uint32_t depth, uint32_t levels) {
    nvn_texdesc d = {target, fmt, 0, 0, w, h, depth, levels, 0};
    nvn_texlayout L; nvn_tex_layout(&d, &L);
    size_t guard = 4096; uint8_t *st = malloc(L.size + guard); memset(st, 0, L.size); memset(st + L.size, 0xAB, guard);
    uint8_t *ref[64][16] = {{0}}; uint64_t bytes = 0;
    for (unsigned y = 0; y < L.layers; y++) for (unsigned l = 0; l < levels; l++) {
        uint32_t rb, rows, sl; dims(&d, l, &rb, &rows, &sl);
        size_t n = (size_t)rb * rows * sl; ref[y][l] = malloc(n); for (size_t i = 0; i < n; i++) ref[y][l][i] = rnd();
        CHECK(!nvn_swizzle(&d, l, y, ref[y][l], rb, st), "%s 写入失败 层%u 级%u", tag, y, l);
    }
    int guard_ok = 1; for (size_t i = 0; i < guard; i++) guard_ok &= st[L.size + i] == 0xAB;
    CHECK(guard_ok, "%s 写出存储末尾（越界）", tag);
    int ok = 1;
    for (unsigned y = 0; y < L.layers; y++) for (unsigned l = 0; l < levels; l++) {
        uint32_t rb, rows, sl; dims(&d, l, &rb, &rows, &sl);
        size_t n = (size_t)rb * rows * sl; uint8_t *out = malloc(n);
        CHECK(!nvn_deswizzle(&d, l, y, st, out, rb), "%s 解出失败", tag);
        if (memcmp(out, ref[y][l], n)) { ok = 0; printf("  ✗ %s 层%u 级%u 往返不等\n", tag, y, l); }
        bytes += n; free(out); free(ref[y][l]);
    }
    checks++; if (!ok) fails++;
    printf("  A %-34s 层%u × 级%u，往返 %llu 字节 %s\n", tag, L.layers, levels, (unsigned long long)bytes, ok ? "全等" : "不等");
    free(st);
}

// ---------- 真数据 ----------
// ---------- Real data ----------
static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; } fclose(f); return b;
}
// Switch 转换过的文件：0x14 四字节 = 00 00 00 07（PC 为 0），0x13 = 第 0 级块高 log2（与游戏 SetPackagedTextureLayout 第 0 字节同义）。
// 未转换的（如 hd/debug 下的）与 PC 文件逐字节相同、线性存放。
// Files converted for Switch: the four bytes at 0x14 = 00 00 00 07 (0 on PC), 0x13 = log2 of the level-0 block height (same meaning as byte 0 of the game's SetPackagedTextureLayout).
// Unconverted ones (e.g. those under hd/debug) are byte-identical to the PC files and stored linearly.
typedef struct { int code, nvn, bl, th_hdr; uint32_t w, h, mips, hdr; uint32_t size[16]; uint64_t off[16]; const uint8_t *px; } tex_t;
static int d2r_nvn(int code) {   // D2R .texture 格式码 → NVNformat | D2R .texture format code → NVNformat
    switch (code) { case 0x1F: return 0x25; case 0x39: return 0x42; case 0x3A: return 0x46; case 0x3D: return 0x44;
                    case 0x3E: return 0x48; case 0x3F: return 0x49; case 0x41: return 0x4B; }
    return 0;
}
static int parse(const uint8_t *b, size_t n, tex_t *t) {
    if (n < 0x24 || memcmp(b, "<DE(", 4)) return -1;
    t->code = b[4]; t->nvn = d2r_nvn(t->code); t->bl = b[0x14] == 0 && b[0x15] == 0 && b[0x16] == 0 && b[0x17] == 7; t->th_hdr = b[0x13];
    memcpy(&t->w, b + 8, 4); memcpy(&t->h, b + 12, 4); memcpy(&t->mips, b + 0x1C, 4);
    if (!t->nvn || t->mips == 0 || t->mips > 16) return -1;
    t->hdr = 36 + 8 * t->mips; uint64_t o = 0;
    for (unsigned i = 0; i < t->mips; i++) { memcpy(&t->size[i], b + 0x24 + 8 * i, 4); t->off[i] = o; o += t->size[i]; }
    if (t->hdr + o != n) return -1;
    t->px = b + t->hdr; return 0;
}

// 解码成 8 位 RGB（BC1 / BC3 取颜色块，BC4 取单通道铺三份，RGBA8 取 RGB）。行 = 块行，按 4×4 展开
// Decode to 8-bit RGB (BC1 / BC3 take the color block, BC4 takes the single channel replicated three times, RGBA8 takes RGB). Row = block row, expanded as 4×4
static void bc1(const uint8_t *b, uint8_t out[16][3]) {
    unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8; uint32_t bits = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
    int p[4][3];
    for (int k = 0; k < 2; k++) { unsigned c = k ? c1 : c0; p[k][0] = (c >> 11 & 31) * 255 / 31; p[k][1] = (c >> 5 & 63) * 255 / 63; p[k][2] = (c & 31) * 255 / 31; }
    for (int j = 0; j < 3; j++) {
        if (c0 > c1) { p[2][j] = (2 * p[0][j] + p[1][j]) / 3; p[3][j] = (p[0][j] + 2 * p[1][j]) / 3; }
        else { p[2][j] = (p[0][j] + p[1][j]) / 2; p[3][j] = 0; }
    }
    for (int i = 0; i < 16; i++) for (int j = 0; j < 3; j++) out[i][j] = (uint8_t)p[bits >> (2 * i) & 3][j];
}
static void bc4(const uint8_t *b, uint8_t out[16][3]) {
    int a0 = b[0], a1 = b[1], v[8] = {a0, a1};
    for (int k = 2; k < 8; k++) v[k] = a0 > a1 ? ((8 - k) * a0 + (k - 1) * a1) / 7 : k < 6 ? ((6 - k) * a0 + (k - 1) * a1) / 5 : (k == 6 ? 0 : 255);
    uint64_t bits = 0; for (int k = 0; k < 6; k++) bits |= (uint64_t)b[2 + k] << (8 * k);
    for (int i = 0; i < 16; i++) out[i][0] = out[i][1] = out[i][2] = (uint8_t)v[bits >> (3 * i) & 7];
}
static uint8_t *decode(int nvn, const uint8_t *lin, uint32_t w, uint32_t h) {   // → w×h×3
    uint8_t *img = calloc((size_t)w * h, 3);
    if (nvn == 0x25) { for (size_t i = 0; i < (size_t)w * h; i++) memcpy(img + 3 * i, lin + 4 * i, 3); return img; }
    uint32_t bwn = (w + 3) / 4, bhn = (h + 3) / 4, bpb = (nvn == 0x42 || nvn == 0x46 || nvn == 0x49) ? 8 : 16;
    for (uint32_t by = 0; by < bhn; by++) for (uint32_t bx = 0; bx < bwn; bx++) {
        const uint8_t *b = lin + ((size_t)by * bwn + bx) * bpb; uint8_t px[16][3];
        if (nvn == 0x49) bc4(b, px); else bc1(bpb == 16 ? b + 8 : b, px);
        for (int i = 0; i < 16; i++) { uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4; if (x < w && y < h) memcpy(img + 3 * ((size_t)y * w + x), px[i], 3); }
    }
    return img;
}
static double psnr(const uint8_t *a, const uint8_t *b, size_t n) {
    double e = 0; for (size_t i = 0; i < n; i++) { double d = (double)a[i] - b[i]; e += d * d; }
    return e == 0 ? 99 : 10 * log10(255.0 * 255.0 / (e / n));
}
// 用指定块高解一级（负对照用；越过存储的地址按 0 读）
// Decode one level with a specified block height (for the negative control; addresses beyond the storage read as 0)
static void deswz_tile(const uint8_t *st, uint64_t st_size, uint64_t base, uint32_t rb, uint32_t rows, unsigned th, uint8_t *out) {
    for (uint32_t y = 0; y < rows; y++) for (uint32_t x = 0; x < rb; x++) {
        uint64_t a = base + nvn_bl_offset(x, y, 0, rb, rows, th, 0);
        out[(size_t)y * rb + x] = a < st_size ? st[a] : 0;
    }
}

// 判据阈值：正确块高须 ≥、错块高须 < 此值。实测：正确 28.45–38.90 dB（降分辨率重压缩的 3 张；同分辨率的逐字节全等），
// 错一级块高 6.94–16.64 dB ⇒ 取两侧中点 22 dB，两边各留约 6 dB
// Criterion threshold: the correct block height must be ≥ this value and a wrong block height must be < it. Measured: correct 28.45–38.90 dB (the 3 reduced-resolution recompressed ones; same-resolution ones are byte-identical),
// block height off by one step 6.94–16.64 dB ⇒ take the midpoint of the two sides, 22 dB, leaving about 6 dB on each side
#define PSNR_PASS 22.0

static void real_pair(const char *dir, int i, const char *why) {
    char p[1024]; size_t ns, np;
    snprintf(p, sizeof p, "%s/dm/%d.texture", dir, i); uint8_t *sb = slurp(p, &ns);
    snprintf(p, sizeof p, "%s/pc/%d.texture", dir, i); uint8_t *pb = slurp(p, &np);
    tex_t s, c;
    CHECK(sb && pb && !parse(sb, ns, &s) && !parse(pb, np, &c), "样本 %d 读取 / 解析失败（%s）", i, why);
    if (!sb || !pb || parse(sb, ns, &s) || parse(pb, np, &c)) return;
    if (!s.bl) {   // 未经 Switch 转换：线性，应与 PC 原件逐字节相同 | not converted for Switch: linear, should be byte-identical to the PC original
        CHECK(ns == np && !memcmp(sb, pb, ns), "样本%d 无块线性标记但与 PC 原件不同", i);
        printf("  C %d %-30s 无 Switch 块线性标记（0x14 = 0）→ 线性存放，与 PC 原件 %s（%zu 字节）\n", i, why, ns == np && !memcmp(sb, pb, ns) ? "逐字节全等" : "不等", ns);
        free(sb); free(pb); return;
    }
    nvn_texdesc d = {1, s.nvn, 0, 0, s.w, s.h, 1, s.mips, 0, s.th_hdr + 1};   // 块高以文件头为准（同游戏传入的预排布参数） | block height follows the file header (same as the packaged layout parameters the game passes in)
    unsigned th_auto, td_auto; { nvn_texdesc a = d; a.pkg_block_h = 0; nvn_tex_level_tiles(&a, 0, &th_auto, &td_auto); }
    // ③ 文件里的逐级偏移（mip 表累加）对 nvn_tex_level_offset
    // ③ Per-level offsets in the file (cumulative mip table) vs nvn_tex_level_offset
    int off_ok = 1;
    for (unsigned l = 0; l < s.mips; l++)
        if (s.off[l] != nvn_tex_level_offset(&d, l)) { off_ok = 0; printf("  ✗ 样本%d 级%u 文件偏移 %llu ≠ nvn_tex %llu\n", i, l, (unsigned long long)s.off[l], (unsigned long long)nvn_tex_level_offset(&d, l)); }
    nvn_texlayout L; nvn_tex_layout(&d, &L);
    uint64_t file_total = s.off[s.mips - 1] + s.size[s.mips - 1];
    checks++; if (!off_ok) fails++;
    CHECK(file_total == L.size, "样本%d 文件像素区 %llu ≠ nvn_tex 总尺寸 %llu", i, (unsigned long long)file_total, (unsigned long long)L.size);
    // 对应 PC 级：PC 边长 = Switch × 2^k
    // Corresponding PC level: PC side length = Switch × 2^k
    unsigned k = 0; while ((s.w << k) < c.w) k++;
    if ((s.w << k) != c.w || (s.h << k) != c.h) { CHECK(0, "样本%d 尺寸比非 2 的幂", i); return; }
    unsigned nl = s.mips < c.mips - k ? s.mips : c.mips - k;
    uint64_t eq = 0, tot = 0; double ps = 0, ps_bad = 99; unsigned th0, td0;
    nvn_tex_level_tiles(&d, 0, &th0, &td0);
    for (unsigned l = 0; l < nl; l++) {
        uint32_t rb, rows, sl; dims(&d, l, &rb, &rows, &sl);
        uint8_t *lin = malloc((size_t)rb * rows);
        CHECK(!nvn_deswizzle(&d, l, 0, s.px, lin, rb), "样本%d 级%u 解排布失败", i, l);
        const uint8_t *pcl = c.px + c.off[l + k];
        CHECK(c.size[l + k] >= (uint64_t)rb * rows, "样本%d 级%u PC 级尺寸不足", i, l);
        if (!k) { for (size_t j = 0; j < (size_t)rb * rows; j++) eq += lin[j] == pcl[j]; tot += (uint64_t)rb * rows; }
        if (l == 0) {
            uint32_t w = s.w, h = s.h;
            uint8_t *a = decode(s.nvn, lin, w, h), *b = decode(c.nvn, pcl, w, h); ps = psnr(a, b, (size_t)w * h * 3);
            for (int dt = -1; dt <= 1; dt += 2) {                          // D：块高错一级（只有 1 个 GOB 高时块高不影响排布，不适用） | D: block height off by one step (when only 1 GOB high, block height does not affect the layout; not applicable)
                if ((int)th0 + dt < 0 || rows <= 8) continue;
                uint8_t *bad = malloc((size_t)rb * rows); deswz_tile(s.px, ns - s.hdr, 0, rb, rows, th0 + dt, bad);
                uint8_t *e = decode(s.nvn, bad, w, h); double q = psnr(e, b, (size_t)w * h * 3); if (q < ps_bad) ps_bad = q;
                if (!k) CHECK(memcmp(bad, pcl, (size_t)rb * rows), "样本%d 块高 %+d 仍逐字节相等（负对照失效）", i, dt);
                free(bad); free(e);
            }
            free(a); free(b);
        }
        free(lin);
    }
    if (!k) CHECK(eq == tot, "样本%d 逐字节 %llu/%llu", i, (unsigned long long)eq, (unsigned long long)tot);
    CHECK(ps >= PSNR_PASS, "样本%d 正确块高 PSNR %.2f < %.0f", i, ps, PSNR_PASS);
    uint32_t rows0 = (s.h + 3) / 4; if (s.nvn == 0x25) rows0 = s.h;
    if (rows0 > 8) CHECK(ps_bad < PSNR_PASS, "样本%d 错块高 PSNR %.2f ≥ %.0f（负对照失效）", i, ps_bad, PSNR_PASS);
    char neg[48]; if (rows0 > 8) snprintf(neg, sizeof neg, "%.2f", ps_bad); else snprintf(neg, sizeof neg, "不适用（1 个 GOB 高）");
    printf("  C %d %-30s %4u×%-4u→PC 级%u  头块高 %d（自动 %u）偏移%s  %s  PSNR 正确 %.2f / 错块高 %s dB\n", i, why, s.w, s.h, k,
           s.th_hdr, th_auto, off_ok ? "✓" : "✗", k ? "（降分辨率，不比字节）" : (eq == tot ? "逐字节全等" : "逐字节不等"), ps, neg);
    if (!k) printf("      逐字节 %llu/%llu\n", (unsigned long long)eq, (unsigned long long)tot);
    free(sb); free(pb);
}

int main(int argc, char **argv) {
    printf("A 往返（随机数据，非 2 的幂尺寸）\n");
    roundtrip("2D 100×37 R8 4 级", 1, 0x01, 100, 37, 1, 4);
    roundtrip("2D 100×37 RG8 4 级", 1, 0x0D, 100, 37, 1, 4);
    roundtrip("2D 100×37 RGBA8 4 级", 1, 0x25, 100, 37, 1, 4);
    roundtrip("2D 100×37 RGBA16F 4 级", 1, 0x29, 100, 37, 1, 4);
    roundtrip("2D 100×37 RGBA32F 4 级", 1, 0x2E, 100, 37, 1, 4);
    roundtrip("2D 100×37 BC3(16B/块) 4 级", 1, 0x44, 100, 37, 1, 4);
    roundtrip("2D 300×300 BC1(8B/块) 5 级", 1, 0x42, 300, 300, 1, 5);
    roundtrip("2D 数组 100×37×3 层 RGBA8 3 级", 4, 0x25, 100, 37, 3, 3);
    roundtrip("2D 数组 64×64×2 层 BC3 4 级", 4, 0x44, 64, 64, 2, 4);
    roundtrip("3D 100×37×5 RGBA8 3 级", 2, 0x25, 100, 37, 5, 3);
    roundtrip("3D 64×64×20 R8 4 级", 2, 0x01, 64, 64, 20, 4);
    roundtrip("3D 40×30×9 RGBA32F 3 级", 2, 0x2E, 40, 30, 9, 3);

    printf("B 地址公式（期望值按 GOB 64B×8 行公开排布手算）\n");
    struct { uint32_t x, y, z, rb, rows; unsigned th, td; uint64_t want; const char *how; } B[] = {
        {0, 0, 0, 64, 8, 0, 0, 0, "原点"},
        {17, 3, 0, 64, 8, 0, 0, 113, "GOB 内：(3/2)·64 + (17%32/16)·32 + (3%2)·16 + 17%16 = 64+32+16+1"},
        {70, 9, 0, 128, 32, 1, 0, 1558, "块高 2：第 1 列块 1024 + 块内第 1 个 GOB 512 + GOB 内 (9%2)·16 + 6 = 22"},
        {0, 0, 1, 64, 8, 0, 1, 512, "块深 2：块内第 1 个 GOB（z%2·块高1）= 512"},
        {0, 0, 2, 64, 8, 0, 1, 1024, "块深 2：第 1 层块 = 1 列 × 1 行 × 1024"},
        {200, 40, 0, 256, 64, 2, 0, 9284, "块高 4（32 行）：第 1 行块 4·2048=8192 + 第 3 列块 3·2048=6144？见下"},
    };
    // 最后一例手算：rb=256 → 4 个 GOB 宽；块高 4 GOB = 32 行、块 2048 字节；y=40 → 第 1 行块：4×2048 = 8192；
    // x=200 → 第 3 列块：3×2048 = 6144；块内 GOB = (40%32)/8 = 1 → 512；GOB 内 x%64=8、y%8=0 → 8。合计 8192+6144+512+8 = 14856
    // Last hand-calculated case: rb=256 → 4 GOBs wide; block height 4 GOBs = 32 rows, block 2048 bytes; y=40 → block row 1: 4×2048 = 8192;
    // x=200 → block column 3: 3×2048 = 6144; GOB within the block = (40%32)/8 = 1 → 512; within the GOB x%64=8, y%8=0 → 8. Total 8192+6144+512+8 = 14856
    B[5].want = 14856; B[5].how = "块高 4：行块 8192 + 列块 6144 + 块内 GOB 512 + GOB 内 8";
    for (unsigned i = 0; i < sizeof B / sizeof *B; i++) {
        uint64_t got = nvn_bl_offset(B[i].x, B[i].y, B[i].z, B[i].rb, B[i].rows, B[i].th, B[i].td);
        CHECK(got == B[i].want, "B%u 得 %llu 期望 %llu", i, (unsigned long long)got, (unsigned long long)B[i].want);
        printf("  B%u (%u,%u,%u) → %llu %s  %s\n", i, B[i].x, B[i].y, B[i].z, (unsigned long long)got, got == B[i].want ? "✓" : "✗", B[i].how);
    }

    // ---------- E 手工 3D：按手算地址往块线性存储里放标记，解出后核标记落点（往返 A 只证自洽，证不了排布对不对）----------
    // 块高块深规则出处：deko3d dk_image.cpp —— 3D 贴图块高固定 1 个 GOB，块深 = pickTileSize(深度 + 深度/2)。
    // ---------- E hand-built 3D: place markers into block-linear storage at hand-calculated addresses, decode, and verify where the markers land (round trip A only proves self-consistency, not whether the layout is right) ----------
    // Source of the block height / block depth rule: deko3d dk_image.cpp - for 3D textures block height is fixed at 1 GOB, block depth = pickTileSize(depth + depth/2).
    printf("E 手工 3D 块线性（标记放在手算地址，解排布后核落点）\n");
    {
        nvn_texdesc d = {2, 0x25, 0, 0, 32, 16, 20, 1, 0, 0};   // 3D RGBA8 32×16×20：行 128 字节 = 2 个 GOB 宽，16 行 = 2 个 GOB 高 | 3D RGBA8 32×16×20: row 128 bytes = 2 GOBs wide, 16 rows = 2 GOBs high
        nvn_texlayout L; nvn_tex_layout(&d, &L);
        // 块深：20 + 10 = 30 ≥ 16 → 16 片（log2 = 4）；块 = 512 × 16 = 8192 字节；一层块 = 2 列 × 2 行 × 8192 = 32768；ceil(20/16) = 2 层 → 65536
        // Block depth: 20 + 10 = 30 ≥ 16 → 16 slices (log2 = 4); block = 512 × 16 = 8192 bytes; one block layer = 2 columns × 2 rows × 8192 = 32768; ceil(20/16) = 2 layers → 65536
        CHECK(L.tileH == 0 && L.tileD == 4, "E 32×16×20 块高/块深 = %u/%u，期望 0/4", L.tileH, L.tileD);
        CHECK(L.size == 65536, "E 32×16×20 存储 %llu 期望 65536", (unsigned long long)L.size);
        struct { uint32_t x, y, z; uint64_t addr; const char *how; } V[] = {
            {0, 0, 0, 0, "原点"},
            {20, 11, 3, 26224, "行块 1·2·8192=16384 + 列块(80/64=1)·8192 + 块内 GOB z%16=3·512=1536 + GOB 内 (x80%64=16,y3) 64+32+16 = 112"},
            {31, 15, 17, 58364, "深块 1·32768 + 行块 16384 + 列块 8192 + 块内 GOB 1·512 + GOB 内 (x60,y7) 256+192+32+16+12 = 508"},
            {5, 2, 19, 34404, "深块 32768 + 块内 GOB 3·512=1536 + GOB 内 (x20,y2) 64+32+4 = 100"},
        };
        uint8_t *st = calloc(L.size, 1), *lin = calloc(128 * 16 * 20, 1);
        for (unsigned i = 0; i < 4; i++) for (int k = 0; k < 4; k++) st[V[i].addr + k] = (uint8_t)(0x10 * (i + 1) + k + 1);
        CHECK(!nvn_deswizzle(&d, 0, 0, st, lin, 128), "E 解排布失败");
        for (unsigned i = 0; i < 4; i++) {
            const uint8_t *p = lin + ((size_t)V[i].z * 16 + V[i].y) * 128 + V[i].x * 4; int ok = 1;
            for (int k = 0; k < 4; k++) ok &= p[k] == (uint8_t)(0x10 * (i + 1) + k + 1);
            CHECK(ok, "E 体素 (%u,%u,%u) 未在手算地址 %llu 处取到标记", V[i].x, V[i].y, V[i].z, (unsigned long long)V[i].addr);
            printf("  E (%2u,%2u,%2u) ← 存储 %5llu %s  %s\n", V[i].x, V[i].y, V[i].z, (unsigned long long)V[i].addr, ok ? "✓" : "✗", V[i].how);
        }
        size_t nz = 0; for (size_t j = 0; j < 128 * 16 * 20; j++) nz += lin[j] != 0;
        CHECK(nz == 16, "E 解出非零字节 %zu 期望 16（标记外不该有数据）", nz);
        free(st); free(lin);
        // 选角页真调色表的形态：32³ RGBA16F（ser 17 / 16）。块深 32 + 16 = 48 ≥ 16 → 16 片；行 256 字节 = 4 GOB 宽、32 行 = 4 GOB 高、2 层块 → 4·4·2·8192 = 262144
        // Shape of the real color lookup table on the character select screen: 32³ RGBA16F (ser 17 / 16). Block depth 32 + 16 = 48 ≥ 16 → 16 slices; row 256 bytes = 4 GOBs wide, 32 rows = 4 GOBs high, 2 block layers → 4·4·2·8192 = 262144
        nvn_texdesc lut = {2, 0x29, 0, 0, 32, 32, 32, 1, 0, 0}; nvn_tex_layout(&lut, &L);
        CHECK(L.tileH == 0 && L.tileD == 4 && L.size == 4 * 4 * 2 * 8192ull, "E 32³ RGBA16F 块高/块深 %u/%u 存储 %llu，期望 0/4、262144", L.tileH, L.tileD, (unsigned long long)L.size);
        printf("  E 32³ RGBA16F（选角页调色表形态）块高/块深 %u/%u、存储 %llu（期望 0/4、262144）\n", L.tileH, L.tileD, (unsigned long long)L.size);
    }

    printf("C/D 真数据对拍（Switch 块线性 vs PC 线性）+ 错块高负对照，阈值 %.0f dB\n", PSNR_PASS);
    const char *dir = argc > 1 ? argv[1] : getenv("DM_TEX_SAMPLES");
    char lp[1024]; snprintf(lp, sizeof lp, "%s/list.tsv", dir ? dir : "."); FILE *f = dir ? fopen(lp, "r") : NULL;
    CHECK(f, "缺样本目录（先 make samples）"); int npair = 0;
    if (f) { char line[1024]; while (fgets(line, sizeof line, f)) { int i; char path[512], why[256];
        if (sscanf(line, "%d\t%511[^\t]\t%255[^\n]", &i, path, why) == 3) { real_pair(dir, i, why); npair++; } } fclose(f); }
    CHECK(npair >= 3, "真数据样本不足 3 对（%d）", npair);
    printf("%s：%d/%d 项通过（真数据 %d 对）\n", fails ? "红" : "全过", checks - fails, checks, npair);
    return fails ? 1 : 0;
}
