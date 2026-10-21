// #12 反射消费方回归：正例在真实 GPU（宿主 MoltenVK）上执行并独立复算数值；负例在创建 Vulkan 对象之前被拒。
// #12 reflection consumer regression: the positive case runs on a real GPU (host MoltenVK) and is recomputed
// independently; every negative case must be rejected before any Vulkan object exists.
#include "../vulkan/vk_shader_reflect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define EXPECT(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define W 8u
#define H 4u
#define WORDS (W * H)

static char g_dir[512];
static const char *g_spv, *g_res;

static char *path_of(const char *name) {
    static char buf[640];
    snprintf(buf, sizeof buf, "%s/%s", g_dir, name);
    return buf;
}
static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n ? (size_t)n : 1);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return p;
}
static int write_all(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}
static int mutate_json(const char *src, const char *from, const char *to, const char *out) {
    size_t n;
    char *text = (char *)read_all(src, &n);
    if (!text) return 0;
    if (strlen(from) != strlen(to)) { free(text); return 0; }
    char *hit = strstr(text, from);
    if (!hit) { free(text); return 0; }
    memcpy(hit, to, strlen(to));
    int ok = write_all(out, (const uint8_t *)text, n);
    free(text);
    return ok;
}
static int write_json(const char *out, const char *text) { return write_all(out, (const uint8_t *)text, strlen(text)); }

static int mutate_word(uint8_t *code, size_t size, uint16_t op, int nth, int operand, uint32_t value) {
    uint32_t *w = (uint32_t *)code;
    size_t words = size / 4;
    size_t i = 5;
    int seen = 0;
    while (i < words) {
        uint16_t got = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
        if (!wc || i + wc > words) return 0;
        if (got == op && seen++ == nth) {
            if (operand < 0 || operand >= (int)wc - 1) return 0;
            w[i + 1 + operand] = value;
            return 1;
        }
        i += wc;
    }
    return 0;
}
static int mutate_decor(uint8_t *code, size_t size, uint32_t dec, uint32_t value) {
    uint32_t *w = (uint32_t *)code;
    size_t words = size / 4;
    for (size_t i = 5; i < words;) {
        uint16_t op = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
        if (!wc || i + wc > words) return 0;
        if (op == 71 && wc >= 4 && w[i + 2] == dec) { w[i + 3] = value; return 1; }
        i += wc;
    }
    return 0;
}
static int mutate_decor_nth(uint8_t *code, size_t size, uint32_t dec, int nth, uint32_t newdec) {
    uint32_t *w = (uint32_t *)code;
    size_t words = size / 4;
    int seen = 0;
    for (size_t i = 5; i < words;) {
        uint16_t op = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
        if (!wc || i + wc > words) return 0;
        if (op == 71 && wc >= 3 && w[i + 2] == dec && seen++ == nth) { w[i + 2] = newdec; return 1; }
        i += wc;
    }
    return 0;
}
static int count_decor(const uint8_t *code, size_t size, uint32_t dec) {
    const uint32_t *w = (const uint32_t *)code;
    size_t words = size / 4;
    int n = 0;
    for (size_t i = 5; i < words;) {
        uint16_t op = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
        if (!wc || i + wc > words) return n;
        if (op == 71 && wc >= 3 && w[i + 2] == dec) n++;
        i += wc;
    }
    return n;
}
// 构造只有 5 字头 + 一条指令的模块，用于精确命中操作数越界（ASAN 下运行）。
// Build a header + one instruction module to hit exact operand truncation cases (run under ASAN).
static int write_module(const char *name, const uint32_t *words, size_t count) {
    uint32_t buf[16];
    if (count + 5 > sizeof buf / sizeof buf[0]) return 0;
    buf[0] = 0x07230203u; buf[1] = 0x00010300u; buf[2] = 0; buf[3] = 2; buf[4] = 0;
    memcpy(buf + 5, words, count * sizeof buf[0]);
    return write_all(path_of(name), (const uint8_t *)buf, (count + 5) * sizeof buf[0]);
}

static void expect_reject(const char *label, const char *spv, const char *res, const char *needle) {
    dm_vk_shader_plan plan;
    char why[DM_VK_REFLECT_WHY] = {0};
    VkResult r = dm_vk_reflect_load(spv, res, &plan, why, sizeof why);
    checks++;
    if (r == VK_SUCCESS) { failures++; fprintf(stderr, "FAIL %s: unexpectedly accepted\n", label); dm_vk_reflect_free(&plan); return; }
    EXPECT(!plan.code);
    if (!why[0]) { failures++; fprintf(stderr, "FAIL %s: rejected without a diagnostic\n", label); return; }
    if (needle && !strstr(why, needle)) { failures++; fprintf(stderr, "FAIL %s: diagnostic %s lacks %s\n", label, why, needle); }
    printf("reject %-22s %s\n", label, why);
}
static void expect_check_fail(const char *label, const dm_vk_shader_plan *plan, const dm_vk_reflect_limits *limits,
                              uint32_t words, const char *needle) {
    char why[DM_VK_REFLECT_WHY] = {0};
    checks++;
    if (dm_vk_reflect_check(plan, limits, words, why, sizeof why)) {
        failures++; fprintf(stderr, "FAIL %s: check unexpectedly passed\n", label);
        return;
    }
    if (!why[0] || (needle && !strstr(why, needle))) {
        failures++; fprintf(stderr, "FAIL %s: diagnostic %s lacks %s\n", label, why, needle);
    }
    printf("check  %-22s %s\n", label, why);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s <reflect.spv> <reflect.res.json> <image.spv> <image.res.json> <tmpdir>\n", argv[0]);
        return 2;
    }
    g_spv = argv[1]; g_res = argv[2];
    const char *image_spv = argv[3], *image_res = argv[4];
    snprintf(g_dir, sizeof g_dir, "%s", argv[5]);

    // ---- 正例：解析与交叉核对 | positive: parse and cross-check --------------------------------
    dm_vk_shader_plan plan;
    char why[DM_VK_REFLECT_WHY] = {0};
    VkResult r = dm_vk_reflect_load(g_spv, g_res, &plan, why, sizeof why);
    printf("load: %s %s\n", r == VK_SUCCESS ? "ok" : "FAILED", r == VK_SUCCESS ? plan.code_sha256 : why);
    EXPECT(r == VK_SUCCESS);
    if (r != VK_SUCCESS) { printf("vulkan_reflect: %d checks, %d failures\n", checks, failures); return 1; }
    EXPECT(plan.local[0] == 4 && plan.local[1] == 1 && plan.local[2] == 1);
    EXPECT(plan.push_offset == 0 && plan.push_size == 8);
    EXPECT(plan.binding_count == 1);
    EXPECT(plan.bindings[0].set == 0 && plan.bindings[0].binding == 0 && plan.bindings[0].count == 1 &&
           plan.bindings[0].kind == DM_VK_REFLECT_SBUF);
    EXPECT(plan.code_bytes > 0 && strlen(plan.code_sha256) == 64 && plan.code != NULL);
    EXPECT(plan.sidecar_hash_bound == 0);
    char hash[65];
    dm_vk_sha256_hex("abc", 3, hash);
    EXPECT(!strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));   // MbedTLS KAT

    // 完整设备上限下正向 check 必须通过（用宿主真实上限，不创建 GPU 对象）。
    dm_vk_reflect_limits real;
    memset(&real, 0, sizeof real);
    real.max_workgroup_size[0] = 1024; real.max_workgroup_size[1] = 1024; real.max_workgroup_size[2] = 64;
    real.max_invocations = 1024; real.max_workgroup_count[0] = 65535;
    real.max_push_constants = 128; real.max_storage_range = 1u << 30;
    EXPECT(dm_vk_reflect_check(&plan, &real, WORDS, why, sizeof why));

    // ---- 负例：模板块变异 | negative: module mutations ----------------------------------------
    size_t size;
    uint8_t *code = read_all(g_spv, &size);
    EXPECT(code != NULL);
    if (code) {
        uint8_t *m = malloc(size);
        EXPECT(m != NULL);
        if (m) {
            memcpy(m, code, size);
            ((uint32_t *)m)[0] = 0xdeadbeefu;
            EXPECT(write_all(path_of("magic.spv"), m, size));
            expect_reject("magic", path_of("magic.spv"), g_res, "魔数");

            memcpy(m, code, size);
            EXPECT(write_all(path_of("truncated.spv"), m, size - 4));
            expect_reject("truncated", path_of("truncated.spv"), g_res, "OpFunctionEnd");

            memcpy(m, code, size);
            int renamed = 0;
            for (size_t i = 0; i + 4 < size; i++)
                if (!memcmp(m + i, "main", 4)) { m[i + 1] = 'x'; renamed = 1; break; }
            EXPECT(renamed);
            EXPECT(write_all(path_of("entry.spv"), m, size));
            expect_reject("entry-name", path_of("entry.spv"), g_res, "入口名");

            memcpy(m, code, size);
            EXPECT(mutate_word(m, size, 16, 0, 2, 0));
            EXPECT(write_all(path_of("local.spv"), m, size));
            expect_reject("localsize-zero", path_of("local.spv"), g_res, "LocalSize");

            memcpy(m, code, size);
            EXPECT(mutate_decor(m, size, 34, 1));
            EXPECT(write_all(path_of("set.spv"), m, size));
            expect_reject("descriptor-set", path_of("set.spv"), g_res, "set");

            memcpy(m, code, size);
            int ptr_changed = 0;
            size_t words = size / 4;
            for (size_t i = 5; i < words;) {
                uint32_t *w = (uint32_t *)m;
                uint16_t op = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
                if (!wc || i + wc > words) break;
                if (op == 32 && wc == 4 && w[i + 2] == 12) { w[i + 2] = 2; ptr_changed = 1; break; }
                i += wc;
            }
            EXPECT(ptr_changed);
            EXPECT(write_all(path_of("storage.spv"), m, size));
            expect_reject("storage-class", path_of("storage.spv"), g_res, "存储类");
            free(m);
        }
        // EOF 截断：从 5 字头起每 4 字节切一次，公开 load 必须拒绝且不越界读（ASAN 下运行）。
        // EOF truncation: cut every 4 bytes from the header; the public load must reject without OOB reads (ASAN).
        int trunc_bad = 0;
        for (size_t cut = 20; cut < size; cut += 4) {
            uint8_t *part = malloc(size);
            if (!part) { trunc_bad++; break; }
            memcpy(part, code, cut);
            if (!write_all(path_of("eof.spv"), part, cut)) trunc_bad++;
            free(part);
            dm_vk_shader_plan p;
            char w[DM_VK_REFLECT_WHY] = {0};
            if (dm_vk_reflect_load(path_of("eof.spv"), g_res, &p, w, sizeof w) == VK_SUCCESS) {
                trunc_bad++;
                dm_vk_reflect_free(&p);
            }
        }
        EXPECT(trunc_bad == 0);
        free(code);
    }

    // 精确的缺操作数探测（对应 codex-pr60-truncated.c 的 7 字模块）：FLOAT wc=2 曾在 EOF 后读 4 字节。
    // Exact missing-operand probes (the 7-word module from codex-pr60-truncated.c): FLOAT wc=2 used to read past the end.
    const uint32_t float_trunc[] = {(2u << 16) | 22u, 1u};
    EXPECT(write_module("float-trunc.spv", float_trunc, 2));
    expect_reject("float-trunc", path_of("float-trunc.spv"), g_res, "操作数不足");
    const uint32_t int_trunc[] = {(1u << 16) | 21u};
    EXPECT(write_module("int-trunc.spv", int_trunc, 1));
    expect_reject("int-trunc", path_of("int-trunc.spv"), g_res, "结果 id");
    const uint32_t vec_trunc[] = {(3u << 16) | 23u, 1u, 2u};
    EXPECT(write_module("vec-trunc.spv", vec_trunc, 3));
    expect_reject("vector-trunc", path_of("vec-trunc.spv"), g_res, "OpTypeVector");
    const uint32_t img_trunc[] = {(8u << 16) | 25u, 1u, 1u, 0u, 0u, 0u, 2u, 0u};
    EXPECT(write_module("image-trunc.spv", img_trunc, 8));
    expect_reject("image-format-missing", path_of("image-trunc.spv"), g_res, "Image Format");

    // 公开长度/容量被篡改时，校验必须在哈希与解析之前拒绝。
    // A mutated public length/capacity must be rejected before hashing or parsing.
    dm_vk_shader_plan cap = plan;
    cap.code_bytes = 0xfffffff0u;
    expect_check_fail("plan-bytes-overflow", &cap, &real, WORDS, "长度");
    cap = plan;
    cap.code_capacity = 16;
    expect_check_fail("plan-capacity-small", &cap, &real, WORDS, "容量");

    // PushConstant 结构体缺少 Block：逐个把 Block 装饰改成无效值，必须拒绝且诊断指向 push constant。
    // PushConstant struct without Block: turn each Block decoration into an invalid one; the diagnostic must name it.
    int blocks = count_decor(plan.code, plan.code_bytes, 2);
    EXPECT(blocks >= 1);
    int push_block_hit = 0;
    for (int n = 0; n < blocks; n++) {
        uint8_t *b = malloc(plan.code_bytes);
        if (!b) break;
        memcpy(b, plan.code, plan.code_bytes);
        if (!mutate_decor_nth(b, plan.code_bytes, 2, n, 33)) { free(b); continue; }
        EXPECT(write_all(path_of("noblock.spv"), b, plan.code_bytes));
        free(b);
        dm_vk_shader_plan nb;
        char nbwhy[DM_VK_REFLECT_WHY] = {0};
        VkResult rr = dm_vk_reflect_load(path_of("noblock.spv"), g_res, &nb, nbwhy, sizeof nbwhy);
        if (rr == VK_SUCCESS) { dm_vk_reflect_free(&nb); continue; }
        if (strstr(nbwhy, "push constant") && strstr(nbwhy, "Block")) push_block_hit = 1;
    }
    EXPECT(push_block_hit);

    // image 夹具：sampled=2 的 storage image 仍被明确拒绝（本切片不执行 image）；把 sampled 改成 1 时诊断必须指向
    // Sampled 操作数，而不是 ImageFormat，证明扫描器区分了这两个操作数。
    // The image fixture is rejected even with sampled=2 (images are out of scope); flipping sampled to 1 must produce a
    // Sampled-operand diagnostic rather than a format one, proving the scanner separates the two operands.
    expect_reject("image-scope", image_spv, image_res, "不在本切片执行范围");
    size_t isize;
    uint8_t *icode = read_all(image_spv, &isize);
    EXPECT(icode != NULL);
    if (icode) {
        uint8_t *im = malloc(isize);
        if (im) {
            memcpy(im, icode, isize);
            EXPECT(mutate_word(im, isize, 25, 0, 6, 1));   // OpTypeImage Sampled 2 -> 1
            EXPECT(write_all(path_of("image-sampled.spv"), im, isize));
            expect_reject("image-sampled", path_of("image-sampled.spv"), image_res, "Sampled");
            free(im);
        }
        free(icode);
    }

    // ---- 负例：sidecar 变异 | negative: sidecar mutations -------------------------------------
    EXPECT(mutate_json(g_res, "\"cs\"", "\"vs\"", path_of("stage.json")));
    expect_reject("stage", g_spv, path_of("stage.json"), "stage");
    EXPECT(mutate_json(g_res, "\"binding\": 0", "\"binding\": 5", path_of("binding.json")));
    expect_reject("binding", g_spv, path_of("binding.json"), "绑定");
    EXPECT(mutate_json(g_res, "\"count\": 1", "\"count\": 2", path_of("count.json")));
    expect_reject("count", g_spv, path_of("count.json"), "count");
    EXPECT(mutate_json(g_res, "\"sbufs\"", "\"sbuf_\"", path_of("missing.json")));
    expect_reject("missing-binding", g_spv, path_of("missing.json"), "绑定");
    EXPECT(mutate_json(g_res, "\"offset\": 0", "\"offset\": 4", path_of("pc-offset.json")));
    expect_reject("push-offset", g_spv, path_of("pc-offset.json"), "push constant");
    EXPECT(write_json(path_of("unsupported.json"),
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"textures\":[{\"binding\":1,\"count\":1}]}"));
    expect_reject("textures-mismatch", g_spv, path_of("unsupported.json"), "声明");
    EXPECT(write_json(path_of("no-pc.json"), "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}]}"));
    expect_reject("missing-pc", g_spv, path_of("no-pc.json"), "push constant");
    EXPECT(write_json(path_of("dup.json"),
        "{\"stage\":\"cs\",\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}]}"));
    expect_reject("duplicate-key", g_spv, path_of("dup.json"), "重复键");
    char ctl[256];
    snprintf(ctl, sizeof ctl,
        "{\"stage\":\"c\ns\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}]}");
    EXPECT(write_json(path_of("control.json"), ctl));
    expect_reject("control-char", g_spv, path_of("control.json"), "控制字符");
    char bogus[128];
    memset(bogus, '0', 64); bogus[64] = 0;
    char json[512];
    snprintf(json, sizeof json,
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}],"
        "\"spirv_sha256\":\"%s\"}", bogus);
    EXPECT(write_json(path_of("digest.json"), json));
    expect_reject("sidecar-digest", g_spv, path_of("digest.json"), "spirv_sha256");
    char good[600];
    snprintf(good, sizeof good,
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}],"
        "\"spirv_sha256\":\"%s\"}", plan.code_sha256);
    EXPECT(write_json(path_of("digest-ok.json"), good));
    dm_vk_shader_plan bound;
    char why2[DM_VK_REFLECT_WHY] = {0};
    EXPECT(dm_vk_reflect_load(g_spv, path_of("digest-ok.json"), &bound, why2, sizeof why2) == VK_SUCCESS);
    EXPECT(bound.sidecar_hash_bound == 1);
    dm_vk_reflect_free(&bound);

    // ---- 负例：篡改公开 plan 与设备上限（全部在 GPU 之前） --------------------------------------
    // Mutated public plan and device limits; all rejected before GPU work.
    dm_vk_shader_plan copy = plan;   // 共享 code 指针，仅用于检查字段；不单独 free | shares code pointer, inspection only
    copy.local[0] = 1;
    expect_check_fail("plan-local", &copy, &real, WORDS, "不一致");
    copy = plan; copy.bindings[0].binding = 3;
    expect_check_fail("plan-binding", &copy, &real, WORDS, "不一致");
    copy = plan; copy.push_size = 4;
    expect_check_fail("plan-push-size", &copy, &real, WORDS, "不一致");
    copy = plan; copy.code_sha256[0] = copy.code_sha256[0] == 'a' ? 'b' : 'a';
    expect_check_fail("plan-digest", &copy, &real, WORDS, "sha256");
    copy = plan; copy.push_offset = 0xfffffffcu; copy.push_size = 8;
    expect_check_fail("plan-push-overflow", &copy, &real, WORDS, "不一致");

    dm_vk_reflect_limits small = real;
    small.max_workgroup_size[0] = 2;
    expect_check_fail("limit-workgroup", &plan, &small, WORDS, "maxComputeWorkGroupSize");
    small = real; small.max_invocations = 2;
    expect_check_fail("limit-invocations", &plan, &small, WORDS, "Invocations");
    small = real; small.max_workgroup_count[0] = 1;
    expect_check_fail("limit-count", &plan, &small, WORDS, "maxComputeWorkGroupCount");
    small = real; small.max_push_constants = 4;
    expect_check_fail("limit-push", &plan, &small, WORDS, "maxPushConstantsSize");
    small = real; small.max_storage_range = 16;
    expect_check_fail("limit-storage", &plan, &small, WORDS, "maxStorageBufferRange");
    small = real; small.max_workgroup_count[0] = 65535;
    expect_check_fail("huge-dispatch", &plan, &small, 16384u * 16384u, "maxComputeWorkGroupCount");

    // plan 没有字节（被 free 或零值）也必须拒绝 | a plan without bytes (freed/zeroed) is rejected
    dm_vk_shader_plan zero = {0};
    expect_check_fail("plan-empty", &zero, &real, WORDS, "模块字节");

    // ---- A→B：载入 A 后把同一路径改成 B，执行必须仍用 A 的字节 ---------------------------------
    // A→B: after load, replace the file at the same path; execution must still use A's owned bytes.
    size_t asize;
    uint8_t *acode = read_all(g_spv, &asize);
    EXPECT(acode != NULL);
    dm_vk_shader_plan a;
    char awhy[DM_VK_REFLECT_WHY] = {0};
    if (acode) {
        EXPECT(write_all(path_of("ab.spv"), acode, asize));   // 先放 A，再从该路径载入 | place A, then load from that path
        EXPECT(dm_vk_reflect_load(path_of("ab.spv"), g_res, &a, awhy, sizeof awhy) == VK_SUCCESS);
        uint8_t *replacement = malloc(asize);
        if (replacement) {
            memcpy(replacement, acode, asize);
            ((uint32_t *)replacement)[0] = 0xdeadbeefu;   // 同一路径被换成无效模块 B | same path replaced by invalid module B
            EXPECT(write_all(path_of("ab.spv"), replacement, asize));
            dm_vk vk = {0};
            if (dm_vk_create(&vk) != VK_SUCCESS) {
                failures++; fprintf(stderr, "FAIL A->B: no Vulkan device\n");
            } else {
                uint32_t canvas_bytes[WORDS] = {0};
                dm_vk_pixels canvas = {.width = W, .height = H, .bytes = (uint8_t *)canvas_bytes, .capacity = sizeof canvas_bytes};
                uint32_t pc[2] = {7, 3};
                EXPECT(dm_vk_reflect_compute(&vk, &a, &canvas, pc, sizeof pc) == VK_SUCCESS);
                int mismatch = 0;
                for (uint32_t i = 0; i < WORDS; i++) if (canvas_bytes[i] != pc[0] + i * pc[1]) mismatch++;
                EXPECT(mismatch == 0);
                printf("A->B: executed owned bytes, %d/%u mismatched\n", mismatch, WORDS);
                dm_vk_shader_plan mutated = a;
                mutated.local[0] = 2;
                EXPECT(dm_vk_reflect_compute(&vk, &mutated, &canvas, pc, sizeof pc) == VK_ERROR_INITIALIZATION_FAILED);
                dm_vk_destroy(&vk);
            }
            free(replacement);
        }
        dm_vk_reflect_free(&a);
        free(acode);
    }

    // ---- 正例：真实 GPU 执行 + 独立数值复算 | positive: real GPU execution + independent oracle -----
    dm_vk vk = {0};
    if (dm_vk_create(&vk) != VK_SUCCESS) { fprintf(stderr, "no Vulkan device available\n"); return 1; }
    uint32_t canvas_bytes[WORDS] = {0};
    dm_vk_pixels canvas = {.width = W, .height = H, .bytes = (uint8_t *)canvas_bytes, .capacity = sizeof canvas_bytes};
    uint32_t pc[2] = {7, 3};
    r = dm_vk_reflect_compute(&vk, &plan, &canvas, pc, sizeof pc);
    EXPECT(r == VK_SUCCESS);
    int mismatch = 0;
    for (uint32_t i = 0; i < WORDS; i++) if (canvas_bytes[i] != pc[0] + i * pc[1]) mismatch++;
    EXPECT(mismatch == 0);
    printf("oracle run1: %u/%u words mismatched, first=%u expected=%u\n", mismatch, WORDS, canvas_bytes[0], pc[0]);
    pc[0] = 100; pc[1] = 0;
    memset(canvas_bytes, 0, sizeof canvas_bytes);
    EXPECT(dm_vk_reflect_compute(&vk, &plan, &canvas, pc, sizeof pc) == VK_SUCCESS);
    mismatch = 0;
    for (uint32_t i = 0; i < WORDS; i++) if (canvas_bytes[i] != 100u) mismatch++;
    EXPECT(mismatch == 0);
    printf("oracle run2: %u/%u words mismatched\n", mismatch, WORDS);
    EXPECT(dm_vk_reflect_compute(&vk, &plan, &canvas, pc, 4) == VK_ERROR_INITIALIZATION_FAILED);
    dm_vk_destroy(&vk);

    dm_vk_reflect_free(&plan);
    printf("vulkan_reflect: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
