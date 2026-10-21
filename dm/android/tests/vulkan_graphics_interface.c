// #125 VS/PS 图形接口反射验收：严格派生 location/type/varying、拒绝 stage/descriptor/畸形/哈希错误；
// 用同一 plan 代码和 location 执行实际 indexed pipeline 像素 oracle，并验证错误 plan 不产生 GPU 输出。
// #125 VS/PS interface reflection: strict locations/types/varying, stage/descriptor/malformed/hash rejection;
// execute the actual indexed pipeline from the same plan code and locations with a pixel oracle, and verify a
// wrong plan cannot produce output.
#include "../vulkan/vk_graphics_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static const uint32_t SENTINEL = 0x5a5a5a5au;

static int read_all(const char *path, uint8_t **out, size_t *size) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) { fclose(f); return 0; }
    long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n ? (size_t)n : 1);
    if (!b) { fclose(f); return 0; }
    int ok = fread(b, 1, (size_t)n, f) == (size_t)n;
    fclose(f); if (!ok) { free(b); return 0; }
    *out = b; *size = (size_t)n; return 1;
}
static int write_all(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size; fclose(f); return ok;
}

// 把 valid VS 中所有 Location=1 改成 0（产生重复 location）。 | Change all Location=1 decorations to 0 (duplicate).
static int mutate_location_duplicate(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 20 || size % 4) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), changed = 0;
    for (uint32_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 71u && wc >= 4 && w[i + 2] == 30u && w[i + 3] == 1u) { w[i + 3] = 0u; changed = 1; }
        i += wc;
    }
    int ok = changed && write_all(dst, b, size);
    free(b); return ok;
}
// 删除颜色输入的 Location 装饰（改成 RelaxedPrecision），制造 missing-location。 | Remove the color Location decoration.
static int mutate_location_missing(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 20 || size % 4) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), changed = 0;
    for (uint32_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 71u && wc >= 4 && w[i + 2] == 30u && w[i + 3] == 1u) { w[i + 2] = 0u; changed = 1; }
        i += wc;
    }
    int ok = changed && write_all(dst, b, size);
    free(b); return ok;
}
// 超过 plan 读取上限的模块；应在 read_file/scan 前拒绝。 | Module above the plan read limit; reject before scanning.
static int oversize_module(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size)) return 0;
    size_t out = (size_t)DM_VK_GRAPHICS_MAX_CODE + 4;
    uint8_t *big = calloc(1, out);
    if (!big) { free(b); return 0; }
    memcpy(big, b, size);
    int ok = write_all(dst, big, out);
    free(big); free(b); return ok;
}
// 把 OpEntryPoint 的 "main" 改成 "xxxx"；不改长度，保持模块结构。 | Change the entry name in place.
static int mutate_entry_name(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 20 || size % 4) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), changed = 0;
    for (uint32_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 15u && wc >= 4) {
            uint32_t *name = &w[i + 3];   // 第一个 name 字，通常是 "main" | first name word, normally "main"
            if (memcmp(name, "main", 4) == 0) { memcpy(name, "xxxx", 4); changed = 1; }
        }
        i += wc;
    }
    int ok = changed && write_all(dst, b, size);
    free(b); return ok;
}
static int truncate_module(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 24) { free(b); return 0; }
    int ok = write_all(dst, b, size - 4);
    free(b); return ok;
}
// 入口函数 id 改为 header.bound+100；无 OpFunction 且越界。 | Entry function id -> header.bound+100.
static int mutate_entry_out_of_bound(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 24 || size % 4) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), changed = 0;
    uint32_t bound = w[3];
    for (uint32_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 15u && wc >= 4) { w[i + 2] = bound + 100u; changed = 1; }
        i += wc;
    }
    int ok = changed && write_all(dst, b, size);
    free(b); return ok;
}
// 入口函数 id 改为 in-bound 的 OpTypeVoid id；该 id 不是函数定义。 | Entry id -> in-bound type id, not a function.
static int mutate_entry_to_type(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 24 || size % 4) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), type_id = 0, changed = 0;
    for (uint32_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 19u && wc >= 2) { type_id = w[i + 1]; break; }
        i += wc;
    }
    if (type_id) {
        for (uint32_t i = 5; i < n;) {
            uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
            if (!wc || i + wc > n) break;
            if (op == 15u && wc >= 4) { w[i + 2] = type_id; changed = 1; break; }
            i += wc;
        }
    }
    int ok = changed && write_all(dst, b, size);
    free(b); return ok;
}
// 把 OpTypeVoid 的 result id 改成 OpFunction 的 id，制造重复/冲突定义。 | Duplicate a type/function definition id.
static int mutate_duplicate_definition(const char *src, const char *dst) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 24 || size % 4) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), void_word = 0, function_id = 0;
    for (uint32_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 19u && wc >= 2 && !void_word) void_word = i;
        if (op == 54u && wc >= 5 && !function_id) function_id = w[i + 2];
        i += wc;
    }
    int ok = void_word && function_id && (w[void_word + 1] = function_id) != 0 && write_all(dst, b, size);
    free(b); return ok;
}
// 在 OpTypeFunction 中插入 N 个参数类型，构造 8/9/大量参数的 CPU 反例。 | Insert N parameter types.
static int mutate_function_parameters(const char *src, const char *dst, uint32_t params) {
    uint8_t *b; size_t size;
    if (!read_all(src, &b, &size) || size < 24 || size % 4 || params > 4096) { free(b); return 0; }
    uint32_t *w = (uint32_t *)(void *)b, n = (uint32_t)(size / 4), i = 0;
    for (i = 5; i < n;) {
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > n) break;
        if (op == 33u && wc == 3) break;
        i += wc;
    }
    if (i >= n) { free(b); return 0; }
    uint32_t old_wc = w[i] >> 16;
    uint64_t total = (uint64_t)n + params;
    if (total > (DM_VK_GRAPHICS_MAX_CODE / 4)) { free(b); return 0; }
    uint32_t *out = calloc((size_t)total, sizeof *out);
    if (!out) { free(b); return 0; }
    for (uint32_t k = 0; k < i; k++) out[k] = w[k];
    out[i] = (((old_wc + params) & 0xffffu) << 16) | 33u;
    out[i + 1] = w[i + 1];
    out[i + 2] = w[i + 2];
    for (uint32_t k = 0; k < params; k++) out[i + 3 + k] = w[i + 2];
    for (uint32_t k = i + old_wc; k < n; k++) out[k + params] = w[k];
    int ok = write_all(dst, out, (size_t)total * 4);
    free(out); free(b); return ok;
}

static void expect_reject(const char *vs, const char *ps, const char *meta, const char *label) {
    dm_vk_graphics_plan p; char why[DM_VK_GRAPHICS_WHY] = {0};
    memset(&p, 0, sizeof p);
    VkResult r = dm_vk_graphics_plan_load(vs, ps, meta, &p, why, sizeof why);
    checks++;
    if (r == VK_SUCCESS || p.vs_code || p.ps_code) {
        failures++; fprintf(stderr, "FAIL %s: expected reject (r=%d why=%s)\n", label, (int)r, why);
        dm_vk_graphics_plan_free(&p);
    }
}

int main(int argc, char **argv) {
    if (argc != 9) {
        fprintf(stderr, "usage: %s valid.vert valid.frag bad_model.comp bad_type.vert bad_varying.vert bad_descriptor.vert bad_scalar.vert bad_push.vert\n", argv[0]);
        return 2;
    }
    dm_vk_graphics_plan plan; char why[DM_VK_GRAPHICS_WHY] = {0};
    CHECK(dm_vk_graphics_plan_load(argv[1], argv[2], NULL, &plan, why, sizeof why) == VK_SUCCESS);
    CHECK(plan.position_location == 0 && plan.color_location == 1 && plan.varying_location == 0);
    CHECK(plan.vs_code && plan.ps_code && plan.vs_code_bytes && plan.ps_code_bytes);
    CHECK(strlen(plan.vs_sha256) == 64 && strlen(plan.ps_sha256) == 64);
    CHECK(dm_vk_graphics_plan_check(&plan, why, sizeof why));
    CHECK(!plan.sidecar_hash_bound);

    // 合法 stage/hash sidecar；错误 hash 必须拒绝。 | Valid stage/hash sidecar; wrong hash must reject.
    char meta[512];
    snprintf(meta, sizeof meta, "{\"vs_stage\":\"vs\",\"vs_sha256\":\"%s\",\"ps_stage\":\"ps\",\"ps_sha256\":\"%s\"}",
        plan.vs_sha256, plan.ps_sha256);
    CHECK(write_all("graphics-meta-good.json", meta, strlen(meta)));
    dm_vk_graphics_plan good;
    memset(&good, 0, sizeof good);
    VkResult good_rc = dm_vk_graphics_plan_load(argv[1], argv[2], "graphics-meta-good.json", &good, why, sizeof why);
    CHECK(good_rc == VK_SUCCESS);
    CHECK(good.sidecar_hash_bound);
    CHECK(dm_vk_graphics_plan_check(&good, why, sizeof why));
    dm_vk_graphics_plan_free(&good);
    remove("graphics-meta-good.json");
    char extra_meta[640];
    snprintf(extra_meta, sizeof extra_meta,
        "{\"vs_stage\":\"vs\",\"vs_sha256\":\"%s\",\"ps_stage\":\"ps\",\"ps_sha256\":\"%s\",\"extra\":\"x\"}",
        plan.vs_sha256, plan.ps_sha256);
    CHECK(write_all("graphics-meta-extra.json", extra_meta, strlen(extra_meta)));
    expect_reject(argv[1], argv[2], "graphics-meta-extra.json", "unknown-meta-key");
    remove("graphics-meta-extra.json");
    char bad_meta[512];
    char wrong_hash[65]; memset(wrong_hash, '0', 64); wrong_hash[64] = 0;
    snprintf(bad_meta, sizeof bad_meta, "{\"vs_stage\":\"vs\",\"vs_sha256\":\"%s\",\"ps_stage\":\"ps\",\"ps_sha256\":\"%s\"}",
        wrong_hash, plan.ps_sha256);
    CHECK(write_all("graphics-meta-bad.json", bad_meta, strlen(bad_meta)));
    expect_reject(argv[1], argv[2], "graphics-meta-bad.json", "hash-mismatch");
    remove("graphics-meta-bad.json");

    // 负例：计算模型、错误类型、错误 varying、descriptor 和畸形/截断模块。
    expect_reject(argv[3], argv[2], NULL, "compute-model");
    expect_reject(argv[4], argv[2], NULL, "bad-type");
    expect_reject(argv[7], argv[2], NULL, "scalar-type");
    expect_reject(argv[5], argv[2], NULL, "bad-varying");
    expect_reject(argv[6], argv[2], NULL, "descriptor");
    expect_reject(argv[8], argv[2], NULL, "push-constant");
    CHECK(mutate_location_duplicate(argv[1], "graphics-dup-location.spv"));
    expect_reject("graphics-dup-location.spv", argv[2], NULL, "duplicate-location");
    remove("graphics-dup-location.spv");
    CHECK(mutate_location_missing(argv[1], "graphics-missing-location.spv"));
    expect_reject("graphics-missing-location.spv", argv[2], NULL, "missing-location");
    remove("graphics-missing-location.spv");
    CHECK(mutate_entry_name(argv[1], "graphics-bad-entry.spv"));
    expect_reject("graphics-bad-entry.spv", argv[2], NULL, "bad-entry");
    remove("graphics-bad-entry.spv");
    CHECK(mutate_entry_out_of_bound(argv[1], "graphics-entry-oob.spv"));
    expect_reject("graphics-entry-oob.spv", argv[2], NULL, "entry-out-of-bound");
    remove("graphics-entry-oob.spv");
    CHECK(mutate_entry_to_type(argv[1], "graphics-entry-type.spv"));
    expect_reject("graphics-entry-type.spv", argv[2], NULL, "entry-inbound-undefined");
    remove("graphics-entry-type.spv");
    CHECK(mutate_duplicate_definition(argv[1], "graphics-duplicate-def.spv"));
    expect_reject("graphics-duplicate-def.spv", argv[2], NULL, "duplicate-definition");
    remove("graphics-duplicate-def.spv");
    CHECK(mutate_function_parameters(argv[1], "graphics-params-8.spv", 8));
    expect_reject("graphics-params-8.spv", argv[2], NULL, "function-params-8");
    remove("graphics-params-8.spv");
    CHECK(mutate_function_parameters(argv[1], "graphics-params-9.spv", 9));
    expect_reject("graphics-params-9.spv", argv[2], NULL, "function-params-9");
    remove("graphics-params-9.spv");
    CHECK(mutate_function_parameters(argv[1], "graphics-params-large.spv", 4096));
    expect_reject("graphics-params-large.spv", argv[2], NULL, "function-params-large");
    remove("graphics-params-large.spv");
    CHECK(truncate_module(argv[1], "graphics-truncated.spv"));
    expect_reject("graphics-truncated.spv", argv[2], NULL, "truncated");
    remove("graphics-truncated.spv");
    CHECK(oversize_module(argv[1], "graphics-oversize.spv"));
    expect_reject("graphics-oversize.spv", argv[2], NULL, "oversize");
    remove("graphics-oversize.spv");

    // plan 字段被篡改：consumer 必须在 GPU 前拒绝且不改画布。 | Mutated plan fields: consumer rejects before GPU.
    dm_vk_graphics_plan mutated = plan;
    mutated.position_location = 1; mutated.color_location = 0;
    uint32_t words[16 * 16];
    for (unsigned i = 0; i < 16 * 16; i++) words[i] = SENTINEL;
    dm_vk_pixels pixels = {.width = 16, .height = 16, .bytes = (uint8_t *)words, .capacity = sizeof words};
    dm_vk_indexed_vertex verts[6] = {
        {{0.2f, -0.6f}, {0, 0, 1, 1}}, {{0.8f, -0.6f}, {0, 0, 1, 1}}, {{0.5f, 0.6f}, {0, 0, 1, 1}},
        {{-0.8f, -0.6f}, {1, 0, 0, 1}}, {{-0.2f, -0.6f}, {1, 0, 0, 1}}, {{-0.5f, 0.6f}, {1, 0, 0, 1}},
    };
    const uint16_t indices[6] = {3, 4, 5, 0, 1, 2};
    dm_vk_indexed_draw draw = {.vertices = verts, .vertex_count = 6, .vertex_stride = DM_VK_INDEXED_VERTEX_STRIDE,
        .indices = indices, .index_count = 6};
    dm_vk vk = {0};
    VkResult created = dm_vk_create(&vk);
    if (created != VK_SUCCESS) {
        printf("vulkan_graphics_interface: no Vulkan device; SKIP GPU\n");
        dm_vk_graphics_plan_free(&plan);
        return 77;
    }
    CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &mutated, &pixels) != VK_SUCCESS);
    for (unsigned i = 0; i < 16 * 16; i++) CHECK(words[i] == SENTINEL);

    dm_vk_graphics_plan bad_components = plan;
    bad_components.position_components = 3;
    for (unsigned i = 0; i < 16 * 16; i++) words[i] = SENTINEL;
    CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &bad_components, &pixels) != VK_SUCCESS);
    for (unsigned i = 0; i < 16 * 16; i++) CHECK(words[i] == SENTINEL);

    // 实际 plan 代码/属性驱动的 indexed draw 像素 oracle。
    VkResult draw_rc = dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels);
    CHECK(draw_rc == VK_SUCCESS);
    const uint8_t *p = (const uint8_t *)words;
    #define PX(x, y) (p + ((size_t)(y) * 16 + (x)) * 4)
    CHECK(PX(4, 6)[0] == 255 && PX(4, 6)[1] == 0 && PX(4, 6)[2] == 0 && PX(4, 6)[3] == 255);
    CHECK(PX(11, 6)[0] == 0 && PX(11, 6)[1] == 0 && PX(11, 6)[2] == 255 && PX(11, 6)[3] == 255);
    CHECK(PX(0, 0)[0] == 0 && PX(0, 0)[1] == 0 && PX(0, 0)[2] == 0 && PX(0, 0)[3] == 255);
    #undef PX

    dm_vk_destroy(&vk);
    dm_vk_graphics_plan_free(&plan);
    CHECK(!plan.vs_code && !plan.ps_code && !plan.vs_code_bytes && !plan.ps_code_bytes);
    CHECK(!plan.position_location && !plan.color_location && !plan.position_components && !plan.color_components);
    printf("vulkan_graphics_interface: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
