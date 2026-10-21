// #12 反射驱动计算消费方：有界 SPIR-V 扫描 + res.json 交叉核对 + 描述符/常量映射，全部在创建 Vulkan 对象之前完成。
// #12 reflection-driven compute consumer: bounded SPIR-V scan + res.json cross-check + descriptor/constant mapping, all
// completed before any Vulkan object is created.
#include "vk_shader_reflect.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/sha256.h>

#define SPV_MAGIC 0x07230203u
#define OP_ENTRY_POINT 15u
#define OP_EXECUTION_MODE 16u
#define OP_TYPE_FLOAT 22u
#define OP_TYPE_INT 21u
#define OP_TYPE_VECTOR 23u
#define OP_TYPE_IMAGE 25u
#define OP_TYPE_SAMPLED_IMAGE 27u
#define OP_TYPE_RUNTIME_ARRAY 29u
#define OP_TYPE_STRUCT 30u
#define OP_TYPE_POINTER 32u
#define OP_DECORATE 71u
#define OP_MEMBER_DECORATE 72u
#define OP_VARIABLE 59u
#define DEC_BLOCK 2u
#define DEC_BINDING 33u
#define DEC_DESCRIPTOR_SET 34u
#define DEC_OFFSET 35u
#define MODE_LOCAL_SIZE 17u
#define SC_UNIFORM_CONSTANT 0u
#define SC_UNIFORM 2u
#define SC_PUSH_CONSTANT 9u
#define SC_STORAGE_BUFFER 12u
#define MODEL_GLCOMPUTE 5u
#define DIM_2D 1u
#define MAX_INSTRUCTIONS 4096u
#define MAX_TYPES 256u
#define MAX_VARS 64u
#define MAX_DECOR 512u
#define MAX_MEMBERS 8u
#define MAX_ENTRIES 4u
#define MAX_MODES 4u
#define MAX_JSON_NODES 256u
#define MAX_JSON_BYTES 65536u
#define MAX_MODULE_BYTES (256u * 1024u)

typedef struct { uint32_t id; uint32_t kind; uint32_t a, b, c, d, e; uint32_t members; uint32_t member_type[MAX_MEMBERS]; uint32_t offset[MAX_MEMBERS]; } type_t;
typedef struct { uint32_t id; uint32_t storage; uint32_t pointee; } pointer_t;
typedef struct { uint32_t id; uint32_t type; uint32_t storage; } var_t;
typedef struct { uint32_t id; uint32_t dec; uint32_t a, b; } decor_t;
typedef struct { uint32_t model; uint32_t id; char name[32]; } entry_t;
typedef struct { uint32_t target; uint32_t dims[3]; } local_mode_t;

typedef struct {
    type_t types[MAX_TYPES]; uint32_t type_count;
    pointer_t pointers[MAX_TYPES]; uint32_t pointer_count;
    var_t vars[MAX_VARS]; uint32_t var_count;
    decor_t decors[MAX_DECOR]; uint32_t decor_count;
    entry_t entries[MAX_ENTRIES]; uint32_t entry_count;
    local_mode_t modes[MAX_MODES]; uint32_t mode_count; uint32_t words;
} module_t;

static void set_why(char *why, size_t cap, const char *fmt, ...) {
    if (!why || !cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, cap, fmt, ap);
    va_end(ap);
}

// ---- 最小 JSON（对象/数组/字符串/非负整数/布尔/null），有界、无动态分配 ----------------------------------
enum { J_OBJ, J_ARR, J_STR, J_NUM, J_BOOL, J_NULL };
typedef struct jnode {
    int type;
    uint64_t num;
    char str[96];
    char key[40];
    struct jnode *first, *next;
} jnode;
typedef struct { const char *p, *end; jnode *nodes; int used, cap; const char *err; } jparser;

static void jws(jparser *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static jnode *jnew(jparser *j) {
    if (j->used >= j->cap) { j->err = "JSON 节点过多（超过 256）"; return NULL; }
    jnode *n = &j->nodes[j->used++];
    memset(n, 0, sizeof *n);
    return n;
}
static int jstring(jparser *j, char *out, size_t cap) {
    if (j->p >= j->end || *j->p != '"') { j->err = "JSON 字符串缺少引号"; return 0; }
    j->p++;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->end) { j->err = "JSON 转义截断"; return 0; }
            char e = *j->p++;
            if (e == '"' || e == '\\' || e == '/') c = e;
            else if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else { j->err = "JSON 不支持的转义（只接受 \\\" \\\\ \\/ \\n \\t）"; return 0; }
        }
        if ((unsigned char)c < 0x20) { j->err = "JSON 字符串包含未转义控制字符"; return 0; }
        if (n + 1 >= cap) { j->err = "JSON 字符串过长"; return 0; }
        out[n++] = c;
    }
    if (j->p >= j->end) { j->err = "JSON 字符串未闭合"; return 0; }
    j->p++;
    out[n] = 0;
    return 1;
}
static jnode *jvalue(jparser *j, int depth);
static jnode *jobject(jparser *j, int depth) {
    jnode *o = jnew(j);
    if (!o) return NULL;
    o->type = J_OBJ;
    jnode **tail = &o->first;
    jws(j);
    if (j->p >= j->end || *j->p != '{') { j->err = "JSON 期望对象"; return NULL; }
    j->p++;
    jws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return o; }
    for (;;) {
        jnode *v = jnew(j);
        if (!v) return NULL;
        if (!jstring(j, v->key, sizeof v->key)) return NULL;
        for (const jnode *e = o->first; e; e = e->next)
            if (!strcmp(e->key, v->key)) { j->err = "JSON 对象出现重复键"; return NULL; }
        jws(j);
        if (j->p >= j->end || *j->p != ':') { j->err = "JSON 键后缺少冒号"; return NULL; }
        j->p++;
        jnode *parsed = jvalue(j, depth + 1);
        if (!parsed) return NULL;
        v->type = parsed->type; v->num = parsed->num; memcpy(v->str, parsed->str, sizeof v->str);
        v->first = parsed->first; v->next = NULL;
        *tail = v; tail = &v->next;
        jws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; jws(j); continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; return o; }
        j->err = "JSON 对象未闭合"; return NULL;
    }
}
static jnode *jarray(jparser *j, int depth) {
    jnode *a = jnew(j);
    if (!a) return NULL;
    a->type = J_ARR;
    jnode **tail = &a->first;
    jws(j);
    if (j->p >= j->end || *j->p != '[') { j->err = "JSON 期望数组"; return NULL; }
    j->p++;
    jws(j);
    if (j->p < j->end && *j->p == ']') { j->p++; return a; }
    for (;;) {
        jnode *v = jvalue(j, depth + 1);
        if (!v) return NULL;
        *tail = v; tail = &v->next;
        jws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; jws(j); continue; }
        if (j->p < j->end && *j->p == ']') { j->p++; return a; }
        j->err = "JSON 数组未闭合"; return NULL;
    }
}
static jnode *jvalue(jparser *j, int depth) {
    if (depth > 6) { j->err = "JSON 嵌套过深（超过 6 层）"; return NULL; }
    jws(j);
    if (j->p >= j->end) { j->err = "JSON 提前结束"; return NULL; }
    jnode *n = jnew(j);
    if (!n) return NULL;
    if (*j->p == '{') { n->type = J_OBJ; return jobject(j, depth); }
    if (*j->p == '[') { n->type = J_ARR; return jarray(j, depth); }
    if (*j->p == '"') { n->type = J_STR; return jstring(j, n->str, sizeof n->str) ? n : NULL; }
    if (*j->p == '-' || (*j->p >= '0' && *j->p <= '9')) {
        if (*j->p == '-') { j->err = "JSON 出现负数（反射计数/尺寸必须为非负整数）"; return NULL; }
        n->type = J_NUM;
        uint64_t v = 0;
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
            if (v > (UINT64_MAX - 9) / 10) { j->err = "JSON 整数溢出"; return NULL; }
            v = v * 10 + (uint64_t)(*j->p++ - '0');
        }
        n->num = v;
        return n;
    }
    if (j->end - j->p >= 4 && !strncmp(j->p, "true", 4)) { j->p += 4; n->type = J_BOOL; n->num = 1; return n; }
    if (j->end - j->p >= 5 && !strncmp(j->p, "false", 5)) { j->p += 5; n->type = J_BOOL; return n; }
    if (j->end - j->p >= 4 && !strncmp(j->p, "null", 4)) { j->p += 4; n->type = J_NULL; return n; }
    j->err = "JSON 值形态不支持";
    return NULL;
}
static jnode *jget(const jnode *obj, const char *key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (const jnode *n = obj->first; n; n = n->next) if (!strcmp(n->key, key)) return (jnode *)n;
    return NULL;
}
static size_t jarray_len(const jnode *a) { size_t n = 0; if (!a || a->type != J_ARR) return 0; for (const jnode *i = a->first; i; i = i->next) n++; return n; }

// ---- SHA-256：复用仓库已固定的 MbedTLS 3.6.4（nso/artifact 同源），不再手写实现。
// ---- SHA-256: reuse the pinned MbedTLS 3.6.4 already linked for nso/artifact; no hand-written implementation.
void dm_vk_sha256_hex(const void *data, size_t size, char out[65]) {
    uint8_t digest[32];
    if (!out) return;
    if (mbedtls_sha256((const unsigned char *)data, size, digest, 0) != 0) { out[0] = 0; return; }
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
}

// ---- 模块扫描 | module scan ----------------------------------------------------------------------------
static type_t *find_type(module_t *m, uint32_t id) {
    for (uint32_t i = 0; i < m->type_count; i++) if (m->types[i].id == id) return &m->types[i];
    return NULL;
}
static pointer_t *find_pointer(module_t *m, uint32_t id) {
    for (uint32_t i = 0; i < m->pointer_count; i++) if (m->pointers[i].id == id) return &m->pointers[i];
    return NULL;
}
static decor_t *find_decor(module_t *m, uint32_t id, uint32_t dec) {
    for (uint32_t i = 0; i < m->decor_count; i++)
        if (m->decors[i].id == id && m->decors[i].dec == dec) return &m->decors[i];
    return NULL;
}
static uint32_t type_bytes(module_t *m, uint32_t id, int depth) {
    if (depth > 4) return 0;
    type_t *t = find_type(m, id);
    if (!t) return 0;
    if (t->kind == OP_TYPE_INT || t->kind == OP_TYPE_FLOAT) return t->a ? t->a / 8 : 0;
    if (t->kind == OP_TYPE_VECTOR) {
        uint32_t comp = type_bytes(m, t->a, depth + 1);
        return comp ? comp * t->b : 0;   // b = component count
    }
    return 0;
}

static int scan_module(const uint8_t *data, size_t size, module_t *m, char *why, size_t cap) {
    if (size < 20 || size % 4 || size > MAX_MODULE_BYTES) { set_why(why, cap, "模块大小 %zu 非法（需 ≥20、4 字节对齐、≤256KiB）", size); return 0; }
    const uint32_t *w = (const uint32_t *)data;
    if (w[0] != SPV_MAGIC) { set_why(why, cap, "模块魔数 %#x 不是 SPIR-V", w[0]); return 0; }
    uint32_t major = w[1] >> 16, minor = (w[1] >> 8) & 0xff;
    if (major != 1 || minor > 3) { set_why(why, cap, "SPIR-V 版本 %u.%u 超出 1.0–1.3", major, minor); return 0; }
    if (w[4] != 0) { set_why(why, cap, "SPIR-V schema %u 非 0", w[4]); return 0; }
    if (!w[3]) { set_why(why, cap, "SPIR-V ID bound 为 0"); return 0; }
    uint32_t count = (uint32_t)(size / 4);
    memset(m, 0, sizeof *m);
    m->words = count;
    uint32_t i = 5, insts = 0, last_op = 0;
    while (i < count) {
        if (++insts > MAX_INSTRUCTIONS) { set_why(why, cap, "指令数超过 %u（有界解析）", MAX_INSTRUCTIONS); return 0; }
        uint32_t op = w[i] & 0xffff, wc = w[i] >> 16;
        if (wc < 1 || i + wc > count) { set_why(why, cap, "指令流在字 %u 处越界（op %u, wc %u）", i, op, wc); return 0; }
        last_op = op;
        const uint32_t *o = w + i + 1;
        uint32_t n = wc - 1;
        switch (op) {
        case OP_ENTRY_POINT: {
            if (n < 3) { set_why(why, cap, "OpEntryPoint 操作数不足"); return 0; }
            if (m->entry_count >= MAX_ENTRIES) { set_why(why, cap, "OpEntryPoint 多于 %u 个", MAX_ENTRIES); return 0; }
            entry_t *e = &m->entries[m->entry_count++];
            e->model = o[0]; e->id = o[1];
            uint32_t k = 0;
            for (uint32_t j = 2; j < n && k + 1 < sizeof e->name; j++) {
                uint32_t word = o[j];
                for (int b = 0; b < 4; b++) {
                    char c = (char)((word >> (8 * b)) & 0xff);
                    if (!c) { j = n; break; }
                    e->name[k++] = c;
                }
            }
            e->name[k] = 0;
            break;
        }
        case OP_EXECUTION_MODE:
            if (n >= 2 && o[1] == MODE_LOCAL_SIZE) {
                if (m->mode_count >= MAX_MODES) { set_why(why, cap, "LocalSize 重复次数超过 %u", MAX_MODES); return 0; }
                local_mode_t *md = &m->modes[m->mode_count++];                md->target = o[0];
                if (n != 5) { md->dims[0] = md->dims[1] = md->dims[2] = 0; break; }   // 维度个数错误 -> 全 0 拒绝 | wrong operand count -> reject
                md->dims[0] = o[2]; md->dims[1] = o[3]; md->dims[2] = o[4];
            }
            break;
        case OP_TYPE_POINTER:
            if (n < 3) { set_why(why, cap, "OpTypePointer 操作数不足"); return 0; }
            if (m->pointer_count >= MAX_TYPES) { set_why(why, cap, "指针类型过多"); return 0; }
            m->pointers[m->pointer_count++] = (pointer_t){o[0], o[1], o[2]};
            break;
        case OP_TYPE_STRUCT: {
            if (n < 2 || n - 1 > MAX_MEMBERS) { set_why(why, cap, "OpTypeStruct 成员数 %u 超出 0–%u", n ? n - 1 : 0, MAX_MEMBERS); return 0; }
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            type_t *t = &m->types[m->type_count++];
            memset(t, 0, sizeof *t);
            t->id = o[0]; t->kind = OP_TYPE_STRUCT; t->members = n - 1;
            for (uint32_t k = 0; k < t->members; k++) t->member_type[k] = o[k + 1];
            break;
        }
        case OP_TYPE_IMAGE: {
            if (n < 8) { set_why(why, cap, "OpTypeImage 操作数不足（需 8 个，含必填 Image Format）"); return 0; }
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            type_t *t = &m->types[m->type_count++];
            memset(t, 0, sizeof *t);
            t->id = o[0]; t->kind = OP_TYPE_IMAGE;
            t->a = o[2]; t->b = o[4]; t->c = o[5]; t->d = o[6]; t->e = o[7];   // dim, arrayed, ms, sampled, format
            break;
        }
        case OP_TYPE_SAMPLED_IMAGE:
        case OP_TYPE_INT:
        case OP_TYPE_FLOAT:
        case OP_TYPE_VECTOR: {
            if (n < 1) { set_why(why, cap, "类型指令缺少结果 id"); return 0; }
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            type_t *t = &m->types[m->type_count++];
            memset(t, 0, sizeof *t);
            t->id = o[0]; t->kind = op;
            if (op == OP_TYPE_INT || op == OP_TYPE_FLOAT) {
                if (n < 2) { set_why(why, cap, "OpTypeInt/Float 操作数不足（需宽度，实际 %u）", n); return 0; }
                t->a = o[1];
            } else if (op == OP_TYPE_VECTOR) {
                if (n < 3) { set_why(why, cap, "OpTypeVector 操作数不足（需分量类型与数量，实际 %u）", n); return 0; }
                t->a = o[1]; t->b = o[2];
            } else {
                if (n < 2) { set_why(why, cap, "OpTypeSampledImage 操作数不足（实际 %u）", n); return 0; }
                t->a = o[1];
            }
            break;
        }
        case OP_DECORATE:
            if (n >= 2 && (o[1] == DEC_BINDING || o[1] == DEC_DESCRIPTOR_SET || o[1] == DEC_BLOCK)) {
                if (m->decor_count >= MAX_DECOR) { set_why(why, cap, "装饰过多"); return 0; }
                m->decors[m->decor_count++] = (decor_t){o[0], o[1], n >= 3 ? o[2] : 0, 0};
            }
            break;
        case OP_MEMBER_DECORATE:
            if (n >= 3 && o[2] == DEC_OFFSET) {
                if (m->decor_count >= MAX_DECOR) { set_why(why, cap, "装饰过多"); return 0; }
                m->decors[m->decor_count++] = (decor_t){o[0], DEC_OFFSET, o[1], n >= 4 ? o[3] : 0};
            }
            break;
        case OP_VARIABLE:
            if (n >= 3 && (o[2] == SC_UNIFORM_CONSTANT || o[2] == SC_STORAGE_BUFFER || o[2] == SC_PUSH_CONSTANT || o[2] == SC_UNIFORM)) {
                if (m->var_count >= MAX_VARS) { set_why(why, cap, "变量过多"); return 0; }
                m->vars[m->var_count++] = (var_t){o[1], o[0], o[2]};
            }
            break;
        default:
            break;
        }
        i += wc;
    }
    if (last_op != 56) { set_why(why, cap, "模块最后一条指令不是 OpFunctionEnd(56)（可能被截断）"); return 0; }
    return 1;
}

// ---- 反射计划 | reflection plan -----------------------------------------------------------------------
static int add_binding(dm_vk_shader_plan *plan, uint32_t kind, uint32_t binding, char *why, size_t cap) {
    if (binding > 63) { set_why(why, cap, "绑定号 %u 超出本子集上限 63", binding); return 0; }
    for (uint32_t i = 0; i < plan->binding_count; i++)
        if (plan->bindings[i].binding == binding) { set_why(why, cap, "绑定 %u 重复出现", binding); return 0; }
    if (plan->binding_count >= DM_VK_REFLECT_MAX_BINDINGS) { set_why(why, cap, "描述符超过 %u 个", DM_VK_REFLECT_MAX_BINDINGS); return 0; }
    plan->bindings[plan->binding_count++] = (dm_vk_reflect_binding){0, binding, 1, kind};
    return 1;
}

static int build_plan(module_t *m, dm_vk_shader_plan *plan, char *why, size_t cap) {
    memset(plan, 0, sizeof *plan);
    if (m->entry_count != 1) { set_why(why, cap, "模块有 %u 个 OpEntryPoint，本子集要求恰好 1 个计算入口", m->entry_count); return 0; }
    entry_t *e = &m->entries[0];
    if (e->model != MODEL_GLCOMPUTE) { set_why(why, cap, "执行模型 %u 不是 GLCompute(5)", e->model); return 0; }
    if (strcmp(e->name, "main")) { set_why(why, cap, "入口名是 %s，本子集只接受 main", e->name); return 0; }
    if (m->mode_count != 1) { set_why(why, cap, "LocalSize 声明 %u 次，本子集要求恰好 1 次", m->mode_count); return 0; }
    if (m->modes[0].target != e->id) { set_why(why, cap, "LocalSize 目标 %u 不是入口函数 %u", m->modes[0].target, e->id); return 0; }
    for (int k = 0; k < 3; k++) {
        uint32_t d = m->modes[0].dims[k];
        if (!d || d > 1024) { set_why(why, cap, "LocalSize 维度必须为 1–1024，实为 %u,%u,%u", m->modes[0].dims[0], m->modes[0].dims[1], m->modes[0].dims[2]); return 0; }
        plan->local[k] = d;
    }
    if ((uint64_t)plan->local[0] * plan->local[1] * plan->local[2] > 1024) { set_why(why, cap, "LocalSize 乘积超过 1024"); return 0; }

    int push_seen = 0;
    for (uint32_t v = 0; v < m->var_count; v++) {
        var_t *var = &m->vars[v];
        pointer_t *ptr = find_pointer(m, var->type);
        if (!ptr) { set_why(why, cap, "变量 %%%u 的指针类型 %%%u 未定义", var->id, var->type); return 0; }
        if (ptr->storage != var->storage) { set_why(why, cap, "变量 %%%u 的存储类与指针类型不一致", var->id); return 0; }
        if (var->storage == SC_UNIFORM) { set_why(why, cap, "不支持 Uniform 常量块（cbuf）；本子集只做 storage buffer/image + push constant"); return 0; }
        if (var->storage == SC_UNIFORM_CONSTANT) {
            type_t *t = find_type(m, ptr->pointee);
            if (t && t->kind == OP_TYPE_SAMPLED_IMAGE) { set_why(why, cap, "采样图像/纹理（OpTypeSampledImage）不在本切片", var->id); return 0; }
            if (!t || t->kind != OP_TYPE_IMAGE) { set_why(why, cap, "UniformConstant 变量 %%%u 不是图像类型", var->id); return 0; }
            // 操作数区分：dim=2, arrayed=4, ms=5, sampled=6, format=7。先判定 Sampled，再看格式，避免把 Format 当 Sampled。
            // Operand layout: dim=2, arrayed=4, ms=5, sampled=6, format=7. Check Sampled first, never Format.
            if (t->d != 2) { set_why(why, cap, "图像 %%%u 的 Sampled 操作数为 %u，本切片只识别 storage image(2)", var->id, t->d); return 0; }
            if (t->a != DIM_2D) { set_why(why, cap, "图像 %%%u 的 Dim=%u，本切片只识别 2D(1)", var->id, t->a); return 0; }
            if (t->b) { set_why(why, cap, "图像 %%%u 是数组图像，本切片不支持", var->id); return 0; }
            if (t->c) { set_why(why, cap, "图像 %%%u 为多重采样，本切片不支持", var->id); return 0; }
            // 本切片没有 image 的自有 GPU 夹具，因此明确拒绝执行，而不是声称已验证的 image 支持。
            // This slice has no owned GPU fixture for images, so it rejects them explicitly instead of claiming support.
            set_why(why, cap, "storage image %%%u（sampled=2, format=%u）不在本切片执行范围（无自有 GPU 夹具）", var->id, t->e);
            return 0;
        }
        if (var->storage == SC_STORAGE_BUFFER) {
            type_t *t = find_type(m, ptr->pointee);
            if (!t || t->kind != OP_TYPE_STRUCT) { set_why(why, cap, "StorageBuffer 变量 %%%u 指向的不是结构体", var->id); return 0; }
            if (!find_decor(m, t->id, DEC_BLOCK)) { set_why(why, cap, "StorageBuffer 结构体 %%%u 缺少 Block 装饰", t->id); return 0; }
            decor_t *set = find_decor(m, var->id, DEC_DESCRIPTOR_SET);
            decor_t *bnd = find_decor(m, var->id, DEC_BINDING);
            if (!set || !bnd) { set_why(why, cap, "缓冲变量 %%%u 缺少 DescriptorSet 或 Binding 装饰", var->id); return 0; }
            if (set->a != 0) { set_why(why, cap, "描述符在 set %u，本子集只接受 set 0", set->a); return 0; }
            if (!add_binding(plan, DM_VK_REFLECT_SBUF, bnd->a, why, cap)) return 0;
            continue;
        }
        if (var->storage == SC_PUSH_CONSTANT) {
            if (push_seen) { set_why(why, cap, "出现多个 push constant 块"); return 0; }
            type_t *t = find_type(m, ptr->pointee);
            if (!t || t->kind != OP_TYPE_STRUCT) { set_why(why, cap, "push constant 变量 %%%u 指向的不是结构体", var->id); return 0; }
            if (!find_decor(m, t->id, DEC_BLOCK)) { set_why(why, cap, "push constant 结构体 %%%u 缺少 Block 装饰", t->id); return 0; }
            uint32_t size = 0, offs[MAX_MEMBERS], sizes[MAX_MEMBERS], spans = 0;
            for (uint32_t k = 0; k < t->members; k++) {
                decor_t *off = NULL;
                for (uint32_t d = 0; d < m->decor_count; d++)
                    if (m->decors[d].id == t->id && m->decors[d].dec == DEC_OFFSET && m->decors[d].a == k) { off = &m->decors[d]; break; }
                if (!off) { set_why(why, cap, "push constant 结构体成员 %u 缺少 Offset 装饰", k); return 0; }
                uint32_t msize = type_bytes(m, t->member_type[k], 0);
                if (!msize) { set_why(why, cap, "push constant 成员 %u 的类型大小无法确定（只支持标量/向量）", k); return 0; }
                if (off->b % 4) { set_why(why, cap, "push constant 成员偏移 %u 未 4 字节对齐", off->b); return 0; }
                uint64_t end64 = (uint64_t)off->b + msize;   // 64 位加法，避免 uint32 回绕成很小的区间
                if (end64 > DM_VK_REFLECT_MAX_PUSH) { set_why(why, cap, "push constant 成员 %u 的 offset+size=%llu 超出上限 %u", k, (unsigned long long)end64, DM_VK_REFLECT_MAX_PUSH); return 0; }
                for (uint32_t q = 0; q < spans; q++) {
                    uint64_t qa = offs[q], qe = (uint64_t)offs[q] + sizes[q];
                    if ((uint64_t)off->b < qe && qa < end64) { set_why(why, cap, "push constant 成员 %u 与成员 %u 重叠", k, q); return 0; }
                }
                offs[spans] = off->b; sizes[spans] = msize; spans++;
                if (end64 > size) size = (uint32_t)end64;
            }
            if (!size || size % 4 || size > DM_VK_REFLECT_MAX_PUSH) { set_why(why, cap, "push constant 大小 %u 非法（需 4 的倍数且 ≤%u）", size, DM_VK_REFLECT_MAX_PUSH); return 0; }
            plan->push_offset = 0; plan->push_size = size; push_seen = 1;
            continue;
        }
    }
    return 1;
}

static int sidecar_uint(const jnode *item, const char *key, uint32_t *out, char *why, size_t cap) {
    jnode *n = jget(item, key);
    if (!n || n->type != J_NUM || n->num > 0xffffffffu) { set_why(why, cap, "反射字段 %s 缺失或不是非负整数", key); return 0; }
    *out = (uint32_t)n->num;
    return 1;
}
static int cross_check(const jnode *root, const dm_vk_shader_plan *plan, const char *sha, int *bound, char *why, size_t cap) {
    if (!root || root->type != J_OBJ) { set_why(why, cap, "反射 JSON 顶层不是对象"); return 0; }
    jnode *stage = jget(root, "stage");
    if (!stage || stage->type != J_STR) { set_why(why, cap, "反射缺少 stage 字符串"); return 0; }
    if (strcmp(stage->str, "cs")) { set_why(why, cap, "反射 stage=%s，本消费方只接受 cs", stage->str); return 0; }
    // 任何不支持的种类出现即拒绝：不能因为消费方忽略就悄悄放行。
    // Any unsupported category is rejected: silent tolerance would let unsupported shaders through.
    const char *unsupported[2] = {"cbufs", "textures"};
    for (int i = 0; i < 2; i++) {
        jnode *a = jget(root, unsupported[i]);
        if (a && jarray_len(a)) { set_why(why, cap, "反射声明了 %s，本子集不支持（只做 storage buffer/image + push constant）", unsupported[i]); return 0; }
    }
    uint32_t want[DM_VK_REFLECT_MAX_BINDINGS], kinds[DM_VK_REFLECT_MAX_BINDINGS], n_want = 0;
    const char *cats[2] = {"sbufs", "images"};
    uint32_t kind_of[2] = {DM_VK_REFLECT_SBUF, DM_VK_REFLECT_IMAGE};
    for (int c = 0; c < 2; c++) {
        jnode *arr = jget(root, cats[c]);
        if (!arr) continue;
        if (arr->type != J_ARR) { set_why(why, cap, "反射 %s 不是数组", cats[c]); return 0; }
        for (const jnode *it = arr->first; it; it = it->next) {
            if (it->type != J_OBJ) { set_why(why, cap, "反射 %s 元素不是对象", cats[c]); return 0; }
            uint32_t b, cnt = 1;
            if (!sidecar_uint(it, "binding", &b, why, cap)) return 0;
            jnode *cn = jget(it, "count");
            if (cn && cn->type == J_NUM) cnt = (uint32_t)cn->num;
            if (cnt != 1) { set_why(why, cap, "反射绑定 %u 的 count=%u，本子集只支持 1", b, cnt); return 0; }
            if (n_want >= DM_VK_REFLECT_MAX_BINDINGS) { set_why(why, cap, "反射绑定过多"); return 0; }
            for (uint32_t k = 0; k < n_want; k++)
                if (want[k] == b) { set_why(why, cap, "反射绑定 %u 重复", b); return 0; }
            want[n_want] = b; kinds[n_want] = kind_of[c]; n_want++;
        }
    }
    if (n_want != plan->binding_count) { set_why(why, cap, "反射声明 %u 个绑定，模块有 %u 个", n_want, plan->binding_count); return 0; }
    for (uint32_t k = 0; k < plan->binding_count; k++) {
        int found = 0;
        for (uint32_t i = 0; i < n_want; i++)
            if (want[i] == plan->bindings[k].binding && kinds[i] == plan->bindings[k].kind) found = 1;
        if (!found) { set_why(why, cap, "绑定 %u（kind %u）在反射中不存在或种类不符", plan->bindings[k].binding, plan->bindings[k].kind); return 0; }
    }
    jnode *pc = jget(root, "push_constants");
    if (pc) {
        if (pc->type != J_ARR) { set_why(why, cap, "反射 push_constants 不是数组"); return 0; }
        size_t len = jarray_len(pc);
        if (len > 1) { set_why(why, cap, "反射声明 %zu 个 push constant 块，只支持 1 个", len); return 0; }
        if (len == 1) {
            jnode *it = pc->first;
            uint32_t off, size;
            if (!sidecar_uint(it, "offset", &off, why, cap)) return 0;
            if (!sidecar_uint(it, "size", &size, why, cap)) return 0;
            if (off != plan->push_offset || size != plan->push_size) {
                set_why(why, cap, "反射 push constant offset/size=%u/%u 与模块 %u/%u 不符", off, size, plan->push_offset, plan->push_size);
                return 0;
            }
        } else if (plan->push_size) { set_why(why, cap, "模块有 push constant 但反射未声明"); return 0; }
    } else if (plan->push_size) {
        set_why(why, cap, "模块有 push constant（%u 字节）但反射缺少 push_constants", plan->push_size);
        return 0;
    }
    jnode *hash = jget(root, "spirv_sha256");
    if (hash) {
        if (hash->type != J_STR || strlen(hash->str) != 64) { set_why(why, cap, "spirv_sha256 必须是 64 位十六进制字符串"); return 0; }
        if (strcmp(hash->str, sha)) { set_why(why, cap, "spirv_sha256 与模块内容不符（sidecar 与代码不配对）"); return 0; }
        *bound = 1;
    }
    return 1;
}

VkResult dm_vk_reflect_load(const char *spv_path, const char *res_json_path, dm_vk_shader_plan *plan, char *why, size_t why_cap) {
    if (why && why_cap) why[0] = 0;
    if (!spv_path || !res_json_path || !plan) { set_why(why, why_cap, "参数为空"); return VK_ERROR_INITIALIZATION_FAILED; }
    memset(plan, 0, sizeof *plan);
    FILE *f = fopen(spv_path, "rb");
    if (!f) { set_why(why, why_cap, "打不开 SPIR-V 模块"); return VK_ERROR_INITIALIZATION_FAILED; }
    long len = -1;
    if (!fseek(f, 0, SEEK_END)) len = ftell(f);
    if (len < 0 || len > (long)MAX_MODULE_BYTES || fseek(f, 0, SEEK_SET)) { fclose(f); set_why(why, why_cap, "模块大小读取失败或超出上限"); return VK_ERROR_INITIALIZATION_FAILED; }
    uint8_t *code = malloc((size_t)len ? (size_t)len : 1);
    if (!code) { fclose(f); set_why(why, why_cap, "模块内存不足"); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    if (fread(code, 1, (size_t)len, f) != (size_t)len) { free(code); fclose(f); set_why(why, why_cap, "模块读取不完整"); return VK_ERROR_INITIALIZATION_FAILED; }
    fclose(f);
    module_t *m = calloc(1, sizeof *m);
    if (!m) { free(code); set_why(why, why_cap, "模块解析内存不足"); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    int ok = scan_module(code, (size_t)len, m, why, why_cap) && build_plan(m, plan, why, why_cap);
    free(m);
    if (!ok) { free(code); return VK_ERROR_INITIALIZATION_FAILED; }
    plan->code_bytes = (uint32_t)len;
    plan->code_capacity = (uint32_t)len;
    plan->code = code;                 // plan 拥有；执行时用同一缓冲，绝不按路径重开 | owned; execution uses this buffer, never a reopened path
    dm_vk_sha256_hex(plan->code, (size_t)len, plan->code_sha256);

    f = fopen(res_json_path, "rb");
    if (!f) { set_why(why, why_cap, "打不开反射 JSON"); dm_vk_reflect_free(plan); return VK_ERROR_INITIALIZATION_FAILED; }
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0 || ftell(f) > (long)MAX_JSON_BYTES) { fclose(f); set_why(why, why_cap, "反射 JSON 大小非法或超过 64KiB"); dm_vk_reflect_free(plan); return VK_ERROR_INITIALIZATION_FAILED; }
    size_t jlen = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc(jlen + 1);
    if (!text) { fclose(f); set_why(why, why_cap, "反射 JSON 内存不足"); dm_vk_reflect_free(plan); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    if (fread(text, 1, jlen, f) != jlen) { free(text); fclose(f); set_why(why, why_cap, "反射 JSON 读取不完整"); dm_vk_reflect_free(plan); return VK_ERROR_INITIALIZATION_FAILED; }
    fclose(f);
    text[jlen] = 0;
    jnode *nodes = calloc(MAX_JSON_NODES, sizeof *nodes);
    if (!nodes) { free(text); set_why(why, why_cap, "反射 JSON 解析内存不足"); dm_vk_reflect_free(plan); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    jparser jp = {text, text + jlen, nodes, 0, MAX_JSON_NODES, NULL};
    jnode *root = jvalue(&jp, 0);
    int bad = 0;
    if (!root) { set_why(why, why_cap, "反射 JSON 解析失败：%s", jp.err ? jp.err : "未知错误"); bad = 1; }
    if (!bad) {
        jws(&jp);
        if (jp.p != jp.end) { set_why(why, why_cap, "反射 JSON 尾部有多余数据"); bad = 1; }
    }
    if (!bad) bad = !cross_check(root, plan, plan->code_sha256, &plan->sidecar_hash_bound, why, why_cap);
    free(nodes);
    free(text);
    if (bad) { dm_vk_reflect_free(plan); return VK_ERROR_INITIALIZATION_FAILED; }
    return VK_SUCCESS;
}

void dm_vk_reflect_free(dm_vk_shader_plan *plan) {
    if (!plan) return;
    free(plan->code);
    memset(plan, 0, sizeof *plan);
}

// plan 字段是否仍与其字节一致 + 是否满足设备上限与 1D 派发。纯 CPU，不创建任何 Vulkan 对象。
// Do the plan fields still match their bytes, and do they fit the device limits for a 1D dispatch? Pure CPU.
static int plan_fields_equal(const dm_vk_shader_plan *a, const dm_vk_shader_plan *b) {
    if (a->push_offset != b->push_offset || a->push_size != b->push_size) return 0;
    if (a->binding_count != b->binding_count) return 0;
    if (memcmp(a->local, b->local, sizeof a->local)) return 0;
    if (memcmp(a->bindings, b->bindings, sizeof(dm_vk_reflect_binding) * a->binding_count)) return 0;
    return 1;
}

int dm_vk_reflect_check(const dm_vk_shader_plan *plan, const dm_vk_reflect_limits *limits, uint32_t words,
                        char *why, size_t why_cap) {
    if (why && why_cap) why[0] = 0;
    if (!plan || !plan->code || !plan->code_bytes) { set_why(why, why_cap, "plan 没有模块字节（未经 load 或已释放）"); return 0; }
    if (plan->code_bytes < 20 || plan->code_bytes % 4 || plan->code_bytes > MAX_MODULE_BYTES ||
        plan->code_capacity < 20 || plan->code_capacity > MAX_MODULE_BYTES || plan->code_bytes > plan->code_capacity) {
        set_why(why, why_cap, "plan 的模块长度 %u / 拥有容量 %u 非法或超出上限", plan->code_bytes, plan->code_capacity);
        return 0;
    }
    char hash[65];
    dm_vk_sha256_hex(plan->code, plan->code_bytes, hash);
    if (strcmp(hash, plan->code_sha256)) { set_why(why, why_cap, "plan 的 sha256 与其字节不一致（公开结构被篡改）"); return 0; }
    module_t *m = calloc(1, sizeof *m);
    if (!m) { set_why(why, why_cap, "校验内存不足"); return 0; }
    dm_vk_shader_plan ref;
    int ok = scan_module(plan->code, plan->code_bytes, m, why, why_cap) && build_plan(m, &ref, why, why_cap);
    free(m);
    if (!ok) return 0;
    if (!plan_fields_equal(plan, &ref)) { set_why(why, why_cap, "plan 字段与其模块字节不一致（公开结构被篡改）"); return 0; }
    // 本切片只做 1D 线性派发；多维索引没有独立的数值 oracle。
    // This slice only does 1D linear dispatch; multidimensional indexing has no independent oracle here.
    if (ref.local[1] != 1 || ref.local[2] != 1) { set_why(why, why_cap, "LocalSize y/z 必须为 1（本切片只执行 1D 派发）"); return 0; }
    if (!words) { set_why(why, why_cap, "派发字数不能为 0"); return 0; }
    if (limits) {
        if (ref.local[0] > limits->max_workgroup_size[0] || ref.local[1] > limits->max_workgroup_size[1] ||
            ref.local[2] > limits->max_workgroup_size[2]) { set_why(why, why_cap, "LocalSize %u,%u,%u 超出设备 maxComputeWorkGroupSize", ref.local[0], ref.local[1], ref.local[2]); return 0; }
        uint64_t invocations = (uint64_t)ref.local[0] * ref.local[1] * ref.local[2];
        if (invocations > limits->max_invocations) { set_why(why, why_cap, "LocalSize 乘积 %llu 超出 maxComputeWorkGroupInvocations %u", (unsigned long long)invocations, limits->max_invocations); return 0; }
        uint64_t groups = (words + invocations - 1) / invocations;
        if (groups > limits->max_workgroup_count[0] || groups > UINT32_MAX) { set_why(why, why_cap, "派发工作组 %llu 超出 maxComputeWorkGroupCount[0]=%u（%u 字 / %llu 调用）", (unsigned long long)groups, limits->max_workgroup_count[0], words, (unsigned long long)invocations); return 0; }
        if (ref.push_size) {
            if (ref.push_size > limits->max_push_constants) { set_why(why, why_cap, "push constant %u 字节超出 maxPushConstantsSize %u", ref.push_size, limits->max_push_constants); return 0; }
            if ((uint64_t)ref.push_offset + ref.push_size > limits->max_push_constants) { set_why(why, why_cap, "push constant 范围 %u+%u 超出 maxPushConstantsSize %u", ref.push_offset, ref.push_size, limits->max_push_constants); return 0; }
        }
        if ((uint64_t)words * 4 > limits->max_storage_range) { set_why(why, why_cap, "画布 %llu 字节超出 maxStorageBufferRange %llu", (unsigned long long)words * 4, (unsigned long long)limits->max_storage_range); return 0; }
    }
    return 1;
}

// ---- 执行 | execution -----------------------------------------------------------------------------------
VkResult dm_vk_reflect_compute(dm_vk *vk, const dm_vk_shader_plan *plan, dm_vk_pixels *canvas,
                               const void *push, uint32_t push_bytes) {
    if (!vk || !vk->device || !plan || !canvas || !canvas->bytes) return VK_ERROR_INITIALIZATION_FAILED;
    if (!canvas->width || !canvas->height || canvas->width > 16384 || canvas->height > 16384) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t words64 = (uint64_t)canvas->width * canvas->height;
    if (words64 > UINT32_MAX) return VK_ERROR_INITIALIZATION_FAILED;
    dm_vk_reflect_limits limits;
    memset(&limits, 0, sizeof limits);
    limits.max_workgroup_size[0] = vk->properties.limits.maxComputeWorkGroupSize[0];
    limits.max_workgroup_size[1] = vk->properties.limits.maxComputeWorkGroupSize[1];
    limits.max_workgroup_size[2] = vk->properties.limits.maxComputeWorkGroupSize[2];
    limits.max_invocations = vk->properties.limits.maxComputeWorkGroupInvocations;
    limits.max_workgroup_count[0] = vk->properties.limits.maxComputeWorkGroupCount[0];
    limits.max_workgroup_count[1] = vk->properties.limits.maxComputeWorkGroupCount[1];
    limits.max_workgroup_count[2] = vk->properties.limits.maxComputeWorkGroupCount[2];
    limits.max_push_constants = vk->properties.limits.maxPushConstantsSize;
    limits.max_storage_range = vk->properties.limits.maxStorageBufferRange;
    char check_why[DM_VK_REFLECT_WHY] = {0};
    if (!dm_vk_reflect_check(plan, &limits, (uint32_t)words64, check_why, sizeof check_why))
        return VK_ERROR_INITIALIZATION_FAILED;   // plan 被篡改或超设备上限：GPU 之前拒绝 | mutated plan or device limit: reject before GPU
    if (plan->binding_count != 1 || plan->bindings[0].kind != DM_VK_REFLECT_SBUF || plan->bindings[0].set != 0)
        return VK_ERROR_FEATURE_NOT_PRESENT;   // 本切片只执行单一 storage buffer | this slice executes one storage buffer
    if (push_bytes != plan->push_size || (push_bytes && !push)) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t bytes = words64 * 4;
    if (bytes > canvas->capacity) return VK_ERROR_INITIALIZATION_FAILED;

    dm_vk_buffer buffer = {0};
    VkShaderModule shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkResult result;

    dm_vk_buffer_info info = {.size = bytes, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    result = dm_vk_buffer_create(vk, &info, &buffer);
    if (result != VK_SUCCESS) goto done;
    memset(buffer.mapped, 0, (size_t)bytes);
    VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = plan->code_bytes, .pCode = (const uint32_t *)(const void *)plan->code};
    result = vkCreateShaderModule(vk->device, &smci, NULL, &shader);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorSetLayoutBinding binding = {.binding = plan->bindings[0].binding,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    result = vkCreateDescriptorSetLayout(vk->device, &dci, NULL, &dsl);
    if (result != VK_SUCCESS) goto done;
    VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = plan->push_offset, .size = plan->push_size};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl,
        .pushConstantRangeCount = plan->push_size ? 1u : 0u, .pPushConstantRanges = plan->push_size ? &range : NULL};
    result = vkCreatePipelineLayout(vk->device, &lci, NULL, &layout);
    if (result != VK_SUCCESS) goto done;
    VkComputePipelineCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = layout, .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"}};
    result = vkCreateComputePipelines(vk->device, VK_NULL_HANDLE, 1, &cpi, NULL, &pipeline);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorPoolSize psz = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
    VkDescriptorPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &psz};
    result = vkCreateDescriptorPool(vk->device, &pci, NULL, &pool);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl};
    result = vkAllocateDescriptorSets(vk->device, &ai, &set);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorBufferInfo dbi = {.buffer = buffer.handle, .offset = 0, .range = VK_WHOLE_SIZE};
    VkWriteDescriptorSet wds = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
        .dstBinding = plan->bindings[0].binding, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi};
    vkUpdateDescriptorSets(vk->device, 1, &wds, 0, NULL);
    result = dm_vk_begin(vk, &command);
    if (result != VK_SUCCESS) goto done;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    if (plan->push_size) vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, plan->push_offset, plan->push_size, push);
    uint64_t words = (uint64_t)canvas->width * canvas->height;
    uint64_t group = (uint64_t)plan->local[0] * plan->local[1] * plan->local[2];
    if (!group) { result = VK_ERROR_INITIALIZATION_FAILED; goto done; }
    uint32_t groups = (uint32_t)((words + group - 1) / group);
    vkCmdDispatch(command, groups, 1, 1);
    VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer.handle, .offset = 0, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, NULL, 1, &barrier, 0, NULL);
    result = dm_vk_submit(vk, command);
    command = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_buffer_sync(vk, &buffer, 0);
    if (result != VK_SUCCESS) goto done;
    memcpy(canvas->bytes, buffer.mapped, (size_t)bytes);
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    if (pipeline) vkDestroyPipeline(vk->device, pipeline, NULL);
    if (pool) vkDestroyDescriptorPool(vk->device, pool, NULL);
    if (layout) vkDestroyPipelineLayout(vk->device, layout, NULL);
    if (dsl) vkDestroyDescriptorSetLayout(vk->device, dsl, NULL);
    if (shader) vkDestroyShaderModule(vk->device, shader, NULL);
    dm_vk_buffer_destroy(vk, &buffer);
    return result;
}
