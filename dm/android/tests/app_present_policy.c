// #117 呈现复制策略宿主测试：格式/行距/容量/溢出边界、拒绝时零写入、居中裁剪与行内填充不被触碰。
// 依据 app/present_policy.h 的契约；不依赖 Android 或 GPU。
// #117 host tests for the presentation copy policy: format/stride/capacity/overflow boundaries, zero writes on
// rejection, centred crop and untouched in-row padding. Based on the app/present_policy.h contract; no Android or
// GPU dependency.
#include "../app/present_policy.h"
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

static const uint8_t SENTINEL = 0xa5;

static uint8_t *pattern(size_t *bytes, uint32_t width, uint32_t height) {
    *bytes = (size_t)width * height * 4u;
    uint8_t *canvas = malloc(*bytes);
    if (!canvas) return NULL;
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *p = canvas + ((size_t)y * width + x) * 4u;
            p[0] = (uint8_t)(x * 7u + 1u);
            p[1] = (uint8_t)(y * 11u + 2u);
            p[2] = (uint8_t)((x ^ y) & 0xffu);
            p[3] = 255u;
        }
    return canvas;
}

static uint8_t *sentinel_buffer(size_t *bytes, uint32_t stride, uint32_t height) {
    *bytes = (size_t)stride * height * 4u;
    uint8_t *buffer = malloc(*bytes);
    if (buffer) memset(buffer, SENTINEL, *bytes);
    return buffer;
}

static int all_sentinel(const uint8_t *buffer, size_t bytes) {
    for (size_t i = 0; i < bytes; i++)
        if (buffer[i] != SENTINEL) return 0;
    return 1;
}

// 每个字节都必须等于哨兵：拒绝路径不得写目标。| Every byte must stay sentinel: a rejection writes nothing.
static void expect_reject(const dm_present_source *source, const dm_present_target *target, size_t target_bytes, const char *label) {
    dm_present_rect copied;
    memset(&copied, 0x5a, sizeof copied);
    checks++;
    if (dm_present_copy(source, target, &copied) != 0) {
        failures++;
        fprintf(stderr, "FAIL %s: expected rejection\n", label);
        return;
    }
    checks++;
    if (copied.x || copied.y || copied.width || copied.height) {
        failures++;
        fprintf(stderr, "FAIL %s: reap rejected rect not zeroed\n", label);
    }
    if (target && target->bits) {
        checks++;
        if (!all_sentinel(target->bits, target_bytes)) {
            failures++;
            fprintf(stderr, "FAIL %s: rejected copy modified the target\n", label);
        }
    }
}

static void rejections(void) {
    size_t src_bytes;
    uint8_t *canvas = pattern(&src_bytes, 4u, 4u);
    CHECK(canvas != NULL);
    if (!canvas) return;
    size_t dst_bytes;
    uint8_t *buffer = sentinel_buffer(&dst_bytes, 8u, 6u);
    CHECK(buffer != NULL);
    if (!buffer) { free(canvas); return; }

    dm_present_source source = {.pixels = canvas, .width = 4u, .height = 4u, .capacity = src_bytes};
    dm_present_target target = {.bits = buffer, .width = 8u, .height = 6u, .stride = 8u, .format_rgba8888 = 1};

    dm_present_source bad_source = source;
    dm_present_target bad_target = target;
    expect_reject(NULL, &target, dst_bytes, "null source");
    expect_reject(&source, NULL, 0u, "null target");
    bad_source = source; bad_source.pixels = NULL; expect_reject(&bad_source, &target, dst_bytes, "null source pixels");
    bad_source = source; bad_source.width = 0u; expect_reject(&bad_source, &target, dst_bytes, "zero source width");
    bad_source = source; bad_source.height = 0u; expect_reject(&bad_source, &target, dst_bytes, "zero source height");
    bad_source = source; bad_source.capacity = src_bytes - 1u; expect_reject(&bad_source, &target, dst_bytes, "short capacity");
    bad_target = target; bad_target.bits = NULL; expect_reject(&source, &bad_target, 0u, "null target bits");
    bad_target = target; bad_target.format_rgba8888 = 0; expect_reject(&source, &bad_target, dst_bytes, "non-RGBA8888 format");
    bad_target = target; bad_target.width = 0u; expect_reject(&source, &bad_target, dst_bytes, "zero target width");
    bad_target = target; bad_target.height = 0u; expect_reject(&source, &bad_target, dst_bytes, "zero target height");
    bad_target = target; bad_target.stride = 7u; expect_reject(&source, &bad_target, dst_bytes, "stride below width");

    // 目标整缓冲区字节数超出 size_t：在写任何字节之前拒绝。| Target byte count overflows size_t: reject before writing.
    uint8_t guard[4];
    memset(guard, SENTINEL, sizeof guard);
    dm_present_target huge = {.bits = guard, .width = 1u, .height = 0xffffffffu, .stride = 0xffffffffu, .format_rgba8888 = 1};
    dm_present_source tiny = {.pixels = canvas, .width = 1u, .height = 1u, .capacity = src_bytes};
    dm_present_rect copied;
    CHECK(dm_present_copy(&tiny, &huge, &copied) == 0);
    CHECK(copied.width == 0u && copied.height == 0u);
    CHECK(memcmp(guard, "\xa5\xa5\xa5\xa5", 4) == 0);
    // 复制矩形指针缺失同样拒绝，且不写目标。| A missing result pointer is rejected without writing the target.
    CHECK(dm_present_copy(&source, &target, NULL) == 0);
    CHECK(all_sentinel(buffer, dst_bytes));

    // 源尺寸乘法溢出：1 像素容量不足以覆盖 0xffffffff × 0xffffffff。
    // Source size multiplication overflows: one pixel cannot cover 0xffffffff x 0xffffffff.
    dm_present_source overflow = {.pixels = canvas, .width = 0xffffffffu, .height = 0xffffffffu, .capacity = src_bytes};
    CHECK(dm_present_copy(&overflow, &target, &copied) == 0);
    CHECK(all_sentinel(buffer, dst_bytes));

    free(buffer);
    free(canvas);
}

static void exact_copy(void) {
    size_t src_bytes, dst_bytes;
    uint8_t *canvas = pattern(&src_bytes, 4u, 4u);
    uint8_t *buffer = sentinel_buffer(&dst_bytes, 4u, 4u);
    CHECK(canvas != NULL && buffer != NULL);
    if (canvas && buffer) {
        dm_present_source source = {.pixels = canvas, .width = 4u, .height = 4u, .capacity = src_bytes};
        dm_present_target target = {.bits = buffer, .width = 4u, .height = 4u, .stride = 4u, .format_rgba8888 = 1};
        dm_present_rect copied;
        CHECK(dm_present_copy(&source, &target, &copied) == 1);
        CHECK(copied.x == 0u && copied.y == 0u && copied.width == 4u && copied.height == 4u);
        CHECK(memcmp(buffer, canvas, src_bytes) == 0);
        // 容量有余量也必须接受。| A surplus capacity must be accepted too.
        dm_present_source spare = source;
        spare.capacity = src_bytes + 32u;
        CHECK(dm_present_copy(&spare, &target, &copied) == 1);
        CHECK(memcmp(buffer, canvas, src_bytes) == 0);
    }
    free(buffer);
    free(canvas);
}

// 目标更大且带行距填充：居中放置，可见区域外清零，填充列保持平台原值。
// Larger target with stride padding: centred placement, cleared outside the copy, padding columns left untouched.
static void centred_into_padded_target(void) {
    size_t src_bytes, dst_bytes;
    uint8_t *canvas = pattern(&src_bytes, 4u, 4u);
    uint8_t *buffer = sentinel_buffer(&dst_bytes, 10u, 6u);
    CHECK(canvas != NULL && buffer != NULL);
    if (canvas && buffer) {
        dm_present_source source = {.pixels = canvas, .width = 4u, .height = 4u, .capacity = src_bytes};
        dm_present_target target = {.bits = buffer, .width = 8u, .height = 6u, .stride = 10u, .format_rgba8888 = 1};
        dm_present_rect copied;
        CHECK(dm_present_copy(&source, &target, &copied) == 1);
        CHECK(copied.x == 2u && copied.y == 1u && copied.width == 4u && copied.height == 4u);
        for (uint32_t y = 0; y < 6u; y++)
            for (uint32_t x = 0; x < 10u; x++) {
                const uint8_t *p = buffer + ((size_t)y * 10u + x) * 4u;
                if (x >= 8u) {  // 行距填充：不读不写 | row-stride padding: untouched
                    CHECK(p[0] == SENTINEL && p[1] == SENTINEL && p[2] == SENTINEL && p[3] == SENTINEL);
                } else if (x >= 2u && x < 6u && y >= 1u && y < 5u) {
                    const uint8_t *q = canvas + ((size_t)(y - 1u) * 4u + (x - 2u)) * 4u;
                    CHECK(memcmp(p, q, 4) == 0);
                } else {
                    CHECK(p[0] == 0u && p[1] == 0u && p[2] == 0u && p[3] == 0u);
                }
            }
    }
    free(buffer);
    free(canvas);
}

// 源比目标大：从源居中裁剪。| Source larger than target: centred crop out of the source.
static void centred_crop_from_source(void) {
    size_t src_bytes, dst_bytes;
    uint8_t *canvas = pattern(&src_bytes, 8u, 8u);
    uint8_t *buffer = sentinel_buffer(&dst_bytes, 4u, 4u);
    CHECK(canvas != NULL && buffer != NULL);
    if (canvas && buffer) {
        dm_present_source source = {.pixels = canvas, .width = 8u, .height = 8u, .capacity = src_bytes};
        dm_present_target target = {.bits = buffer, .width = 4u, .height = 4u, .stride = 4u, .format_rgba8888 = 1};
        dm_present_rect copied;
        CHECK(dm_present_copy(&source, &target, &copied) == 1);
        CHECK(copied.x == 0u && copied.y == 0u && copied.width == 4u && copied.height == 4u);
        for (uint32_t y = 0; y < 4u; y++)
            for (uint32_t x = 0; x < 4u; x++) {
                const uint8_t *p = buffer + ((size_t)y * 4u + x) * 4u;
                const uint8_t *q = canvas + ((size_t)(2u + y) * 8u + 2u + x) * 4u;   // 源居中裁剪偏移 (2,2) | centred source offset (2,2)
                CHECK(memcmp(p, q, 4) == 0);
            }
    }
    free(buffer);
    free(canvas);
}

// 256×256 读回缩到小窗口：复制矩形、行距与内容都必须正确。| 256x256 readback into a small window.
static void resized_presentation(void) {
    const uint32_t width = 256u, height = 256u, dst_w = 100u, dst_h = 60u, stride = 104u;
    size_t src_bytes, dst_bytes;
    uint8_t *canvas = pattern(&src_bytes, width, height);
    uint8_t *buffer = sentinel_buffer(&dst_bytes, stride, dst_h);
    CHECK(canvas != NULL && buffer != NULL);
    if (canvas && buffer) {
        dm_present_source source = {.pixels = canvas, .width = width, .height = height, .capacity = src_bytes};
        dm_present_target target = {.bits = buffer, .width = dst_w, .height = dst_h, .stride = stride, .format_rgba8888 = 1};
        dm_present_rect copied;
        CHECK(dm_present_copy(&source, &target, &copied) == 1);
        CHECK(copied.x == 0u && copied.y == 0u && copied.width == dst_w && copied.height == dst_h);
        const uint32_t src_x = (width - dst_w) / 2u, src_y = (height - dst_h) / 2u;
        const uint32_t probes[4][2] = {{0u, 0u}, {dst_w - 1u, 0u}, {0u, dst_h - 1u}, {dst_w - 1u, dst_h - 1u}};
        for (size_t i = 0; i < 4u; i++) {
            const uint8_t *p = buffer + ((size_t)probes[i][1] * stride + probes[i][0]) * 4u;
            const uint8_t *q = canvas + ((size_t)(src_y + probes[i][1]) * width + src_x + probes[i][0]) * 4u;
            CHECK(memcmp(p, q, 4) == 0);
        }
        // 填充列保持原值。| Padding columns keep their original bytes.
        for (uint32_t y = 0; y < dst_h; y++)
            for (uint32_t x = dst_w; x < stride; x++) {
                const uint8_t *p = buffer + ((size_t)y * stride + x) * 4u;
                CHECK(p[0] == SENTINEL && p[3] == SENTINEL);
            }
    }
    free(buffer);
    free(canvas);
}

int main(void) {
    rejections();
    exact_copy();
    centred_into_padded_target();
    centred_crop_from_source();
    resized_presentation();
    printf("app_present_policy: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
