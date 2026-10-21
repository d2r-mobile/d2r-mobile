// #125 VS/PS 图形接口反射：只解析固定 float2+float4 / float4 varying 子集，拒绝 descriptor/push 与任意图形 ABI。
// #125 VS/PS graphics interface reflection: only the fixed float2+float4 / float4 varying subset; reject descriptors,
// push constants and arbitrary graphics ABIs.
#include "vk_graphics_interface.h"
#include "vk_shader_reflect.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPV_MAGIC 0x07230203u
#define OP_ENTRY_POINT 15u
#define OP_EXECUTION_MODE 16u
#define OP_TYPE_VOID 19u
#define OP_TYPE_INT 21u
#define OP_TYPE_FLOAT 22u
#define OP_TYPE_VECTOR 23u
#define OP_TYPE_STRUCT 30u
#define OP_TYPE_POINTER 32u
#define OP_TYPE_FUNCTION 33u
#define OP_FUNCTION 54u
#define OP_FUNCTION_PARAMETER 55u
#define OP_VARIABLE 59u
#define OP_DECORATE 71u
#define OP_MEMBER_DECORATE 72u
#define DEC_BUILTIN 11u
#define DEC_BLOCK 2u
#define DEC_LOCATION 30u
#define STORAGE_INPUT 1u
#define STORAGE_OUTPUT 3u
#define STORAGE_FUNCTION 7u
#define MODEL_VERTEX 0u
#define MODEL_FRAGMENT 4u
#define MODE_ORIGIN_UPPER_LEFT 7u
#define MODE_LOCAL_SIZE 17u
#define BUILTIN_POSITION 0u
#define MAX_INSTRUCTIONS 4096u
#define MAX_TYPES 256u
#define MAX_VARS 64u
#define MAX_DECORS 512u
#define MAX_IFACE 64u
#define MAX_META_BYTES 4096u
#define MAX_DEFS 1024u
#define MAX_FUNCTIONS 16u

typedef struct { uint32_t id, kind, a, b; uint32_t members[8], member_count; } gtype;
typedef struct { uint32_t id, type, storage; } gvar;
typedef struct { uint32_t id, member, dec, a; } gdecor;
typedef struct { uint32_t id, result_type, control, function_type; } gfunc;
typedef struct { uint32_t target, mode, nargs, args[3]; } gmode;
typedef struct {
    gtype types[MAX_TYPES]; uint32_t type_count;
    gvar vars[MAX_VARS]; uint32_t var_count;
    gdecor decors[MAX_DECORS]; uint32_t decor_count;
    gfunc functions[MAX_FUNCTIONS]; uint32_t function_count;
    uint32_t defined[MAX_DEFS]; uint32_t defined_count;
    gmode modes[8]; uint32_t mode_count;
    uint32_t entry_model, entry_id, entry_count;
    uint32_t bound;
    char entry_name[64];
    uint32_t iface[MAX_IFACE], iface_count;
} gmodule;

static void set_why(char *why, size_t cap, const char *fmt, ...) {
    if (!why || !cap) return;
    va_list ap; va_start(ap, fmt); vsnprintf(why, cap, fmt, ap); va_end(ap);
}
static gtype *gtype_find(gmodule *m, uint32_t id) {
    for (uint32_t i = 0; i < m->type_count; i++) if (m->types[i].id == id) return &m->types[i];
    return NULL;
}
static int check_id(gmodule *m, uint32_t id, const char *what, char *why, size_t cap) {
    if (!id || id >= m->bound) {
        set_why(why, cap, "%s id %u 超出 header bound %u", what, id, m->bound);
        return 0;
    }
    return 1;
}
static int add_definition(gmodule *m, uint32_t id, const char *what, char *why, size_t cap) {
    if (!check_id(m, id, what, why, cap)) return 0;
    for (uint32_t i = 0; i < m->defined_count; i++)
        if (m->defined[i] == id) { set_why(why, cap, "%s 重复定义 id %u", what, id); return 0; }
    if (m->defined_count >= MAX_DEFS) { set_why(why, cap, "定义数量超过 %u", MAX_DEFS); return 0; }
    m->defined[m->defined_count++] = id;
    return 1;
}
static gfunc *function_find(gmodule *m, uint32_t id) {
    for (uint32_t i = 0; i < m->function_count; i++) if (m->functions[i].id == id) return &m->functions[i];
    return NULL;
}
static gdecor *decor_find(gmodule *m, uint32_t id, uint32_t dec) {
    for (uint32_t i = 0; i < m->decor_count; i++) if (m->decors[i].id == id && m->decors[i].dec == dec) return &m->decors[i];
    return NULL;
}
static int type_vec_float(gmodule *m, uint32_t id, uint32_t components) {
    gtype *t = gtype_find(m, id);
    if (!t || t->kind != OP_TYPE_VECTOR || t->b != components) return 0;
    gtype *c = gtype_find(m, t->a);
    return c && c->kind == OP_TYPE_FLOAT && c->a == 32;
}
static gdecor *member_decor_find(gmodule *m, uint32_t id, uint32_t member, uint32_t dec) {
    for (uint32_t i = 0; i < m->decor_count; i++)
        if (m->decors[i].id == id && m->decors[i].member == member && m->decors[i].dec == dec) return &m->decors[i];
    return NULL;
}
// gl_PerVertex 的标准输出块：所有成员都是 BuiltIn；只支持 Position/PointSize/ClipDistance/CullDistance，
// 且必须恰有一个 vec4 Position。其他 struct 输出拒绝。 | Standard gl_PerVertex output block; only the four standard
// built-ins and exactly one vec4 Position are accepted. Other struct outputs are rejected.
static int struct_has_position(gmodule *m, uint32_t id) {
    gtype *t = gtype_find(m, id);
    if (!t || t->kind != OP_TYPE_STRUCT || !t->member_count) return 0;
    if (!decor_find(m, id, DEC_BLOCK)) return 0;
    int positions = 0;
    for (uint32_t k = 0; k < t->member_count; k++) {
        gdecor *b = member_decor_find(m, id, k, DEC_BUILTIN);
        if (!b) return 0;
        if (b->a == BUILTIN_POSITION) {
            if (!type_vec_float(m, t->members[k], 4)) return 0;
            positions++;
        } else if (b->a != 1u && b->a != 3u && b->a != 4u) {
            return 0;   // PointSize / ClipDistance / CullDistance only
        }
    }
    return positions == 1;
}

static int scan_module(const uint8_t *data, size_t size, gmodule *m, char *why, size_t cap) {
    if (size < 20 || size % 4 || size > DM_VK_GRAPHICS_MAX_CODE) {
        set_why(why, cap, "模块大小 %zu 非法（需 ≥20、4 字节对齐、≤256KiB）", size); return 0;
    }
    const uint32_t *w = (const uint32_t *)data;
    if (w[0] != SPV_MAGIC) { set_why(why, cap, "SPIR-V 魔数 %#x 非法", w[0]); return 0; }
    uint32_t major = w[1] >> 16, minor = (w[1] >> 8) & 0xff;
    if (major != 1 || minor > 3) { set_why(why, cap, "SPIR-V 版本 %u.%u 超出 1.0–1.3", major, minor); return 0; }
    uint32_t bound = w[3];
    if (!bound || bound > (1u << 20)) { set_why(why, cap, "header bound %u 非法", bound); return 0; }
    uint32_t count = (uint32_t)(size / 4), i = 5, insts = 0, last_op = 0;
    memset(m, 0, sizeof *m);
    m->bound = bound;
    while (i < count) {
        if (++insts > MAX_INSTRUCTIONS) { set_why(why, cap, "指令数超过 %u", MAX_INSTRUCTIONS); return 0; }
        uint32_t op = w[i] & 0xffffu, wc = w[i] >> 16;
        if (!wc || i + wc > count) { set_why(why, cap, "指令流在字 %u 越界（op %u, wc %u）", i, op, wc); return 0; }
        last_op = op;
        const uint32_t *o = w + i + 1;
        uint32_t n = wc - 1;
        switch (op) {
        case OP_ENTRY_POINT: {
            if (m->entry_count) { set_why(why, cap, "出现多个 OpEntryPoint"); return 0; }
            if (n < 3) { set_why(why, cap, "OpEntryPoint 操作数不足"); return 0; }
            if (!check_id(m, o[1], "entry function", why, cap)) return 0;
            m->entry_model = o[0]; m->entry_id = o[1]; m->entry_count = 1;
            uint32_t k = 0, p = 2;
            while (p < n && k + 1 < sizeof m->entry_name) {
                uint32_t word = o[p++];
                for (int b = 0; b < 4; b++) {
                    char c = (char)((word >> (8 * b)) & 0xff);
                    if (!c) { p = n; break; }
                    if (k >= sizeof m->entry_name - 1) { set_why(why, cap, "OpEntryPoint 名称过长"); return 0; }
                    m->entry_name[k++] = c;
                }
                if (p == n) break;
            }
            m->entry_name[k] = 0;
            // p 指向字符串后的第一个 interface id（上面的循环在 NUL 时把 p 设为 n，因此重新定位）。
            // p points to the first interface id after the string; relocate after the NUL.
            p = 2;
            for (;;) {
                if (p >= n) { set_why(why, cap, "OpEntryPoint 名称缺少 NUL"); return 0; }
                uint32_t word = o[p++];
                int ended = 0;
                for (int b = 0; b < 4; b++) if (!((word >> (8 * b)) & 0xff)) { ended = 1; break; }
                if (ended) break;
            }
            for (; p < n; p++) {
                if (!check_id(m, o[p], "entry interface", why, cap)) return 0;
                if (m->iface_count >= MAX_IFACE) { set_why(why, cap, "接口变量超过 %u", MAX_IFACE); return 0; }
                for (uint32_t k = 0; k < m->iface_count; k++)
                    if (m->iface[k] == o[p]) { set_why(why, cap, "entry interface id %u 重复", o[p]); return 0; }
                m->iface[m->iface_count++] = o[p];
            }
            break;
        }
        case OP_EXECUTION_MODE:
            if (n < 2) { set_why(why, cap, "OpExecutionMode 操作数不足"); return 0; }
            if (!check_id(m, o[0], "execution mode target", why, cap)) return 0;
            if (m->mode_count >= 8) { set_why(why, cap, "执行模式过多"); return 0; }
            {
                gmode *md = &m->modes[m->mode_count++];
                md->target = o[0]; md->mode = o[1]; md->nargs = n > 5 ? 3 : n - 2;
                for (uint32_t k = 0; k < md->nargs; k++) md->args[k] = o[2 + k];
            }
            break;
        case OP_TYPE_VOID:
            if (n != 1) { set_why(why, cap, "OpTypeVoid 操作数个数非法"); return 0; }
            if (!add_definition(m, o[0], "type", why, cap)) return 0;
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            m->types[m->type_count++] = (gtype){o[0], OP_TYPE_VOID, 0, 0, {0}, 0};
            break;
        case OP_TYPE_INT:
        case OP_TYPE_FLOAT: {
            if (op == OP_TYPE_INT ? n != 3 : n != 2) { set_why(why, cap, "类型指令操作数个数非法"); return 0; }
            if (!add_definition(m, o[0], "type", why, cap)) return 0;
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            gtype *t = &m->types[m->type_count++];
            memset(t, 0, sizeof *t);
            t->id = o[0]; t->kind = op; t->a = o[1]; t->b = op == OP_TYPE_INT ? o[2] : 0;
            break;
        }
        case OP_TYPE_VECTOR: {
            if (n != 3) { set_why(why, cap, "OpTypeVector 操作数不足"); return 0; }
            if (!add_definition(m, o[0], "type", why, cap)) return 0;
            if (!check_id(m, o[1], "vector component type", why, cap)) return 0;
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            gtype *t = &m->types[m->type_count++];
            memset(t, 0, sizeof *t);
            t->id = o[0]; t->kind = op; t->a = o[1]; t->b = o[2];
            break;
        }
        case OP_TYPE_STRUCT:
            if (n < 1 || n - 1 > 8) { set_why(why, cap, "OpTypeStruct 成员数非法"); return 0; }
            if (!add_definition(m, o[0], "type", why, cap)) return 0;
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            {
                gtype *t = &m->types[m->type_count++];
                memset(t, 0, sizeof *t);
                t->id = o[0]; t->kind = OP_TYPE_STRUCT; t->member_count = n - 1;
                for (uint32_t k = 0; k < t->member_count; k++) {
                    if (!check_id(m, o[k + 1], "struct member type", why, cap)) return 0;
                    t->members[k] = o[k + 1];
                }
            }
            break;
        case OP_TYPE_POINTER: {
            if (n != 3) { set_why(why, cap, "OpTypePointer 操作数不足"); return 0; }
            if (!add_definition(m, o[0], "type", why, cap)) return 0;
            if (!check_id(m, o[2], "pointer pointee type", why, cap)) return 0;
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            gtype *t = &m->types[m->type_count++];
            memset(t, 0, sizeof *t);
            t->id = o[0]; t->kind = op; t->a = o[1]; t->b = o[2];
            break;
        }
        case OP_TYPE_FUNCTION:
            // 入口子集只接受无参数函数类型；参数个数在写入任何数组前拒绝。
            // The entry subset accepts only parameterless function types; reject the count before any array write.
            if (n != 2) { set_why(why, cap, "OpTypeFunction 参数个数 %u 不支持（仅无参数入口）", n > 2 ? n - 2 : 0); return 0; }
            if (!add_definition(m, o[0], "function type", why, cap)) return 0;
            if (!check_id(m, o[1], "function return type", why, cap)) return 0;
            if (m->type_count >= MAX_TYPES) { set_why(why, cap, "类型过多"); return 0; }
            {
                gtype *t = &m->types[m->type_count++];
                memset(t, 0, sizeof *t);
                t->id = o[0]; t->kind = OP_TYPE_FUNCTION; t->a = o[1]; t->member_count = 0;
            }
            break;
        case OP_FUNCTION:
            if (n != 4) { set_why(why, cap, "OpFunction 操作数个数非法"); return 0; }
            if (!add_definition(m, o[1], "function", why, cap)) return 0;
            if (!check_id(m, o[0], "function result type", why, cap)) return 0;
            if (!check_id(m, o[3], "function type", why, cap)) return 0;
            if (m->function_count >= MAX_FUNCTIONS) { set_why(why, cap, "函数过多"); return 0; }
            m->functions[m->function_count++] = (gfunc){o[1], o[0], o[2], o[3]};
            break;
        case OP_FUNCTION_PARAMETER:
            set_why(why, cap, "图形入口函数不支持参数"); return 0;
        case OP_VARIABLE:
            if (n < 3) { set_why(why, cap, "OpVariable 操作数不足"); return 0; }
            if (!add_definition(m, o[1], "variable", why, cap)) return 0;
            if (!check_id(m, o[0], "variable type", why, cap)) return 0;
            if (m->var_count >= MAX_VARS) { set_why(why, cap, "变量过多"); return 0; }
            m->vars[m->var_count++] = (gvar){o[1], o[0], o[2]};
            break;
        case OP_DECORATE:
            if (n >= 2 && (o[1] == DEC_LOCATION || o[1] == DEC_BUILTIN || o[1] == DEC_BLOCK)) {
                if (!check_id(m, o[0], "decoration target", why, cap)) return 0;
                if (m->decor_count >= MAX_DECORS) { set_why(why, cap, "装饰过多"); return 0; }
                if (o[1] != DEC_BLOCK && n < 3) { set_why(why, cap, "Location/BuiltIn 装饰缺少参数"); return 0; }
                if (decor_find(m, o[0], o[1])) { set_why(why, cap, "重复装饰 id %u dec %u", o[0], o[1]); return 0; }
                m->decors[m->decor_count++] = (gdecor){o[0], UINT32_MAX, o[1], o[1] == DEC_BLOCK ? 0 : o[2]};
            }
            break;
        case OP_MEMBER_DECORATE:
            if (n >= 4 && o[2] == DEC_BUILTIN) {
                if (!check_id(m, o[0], "member decoration target", why, cap)) return 0;
                if (m->decor_count >= MAX_DECORS) { set_why(why, cap, "装饰过多"); return 0; }
                if (member_decor_find(m, o[0], o[1], o[2])) { set_why(why, cap, "重复成员装饰"); return 0; }
                m->decors[m->decor_count++] = (gdecor){o[0], o[1], DEC_BUILTIN, o[3]};
            }
            break;
        default: break;
        }
        i += wc;
    }
    if (last_op != 56) { set_why(why, cap, "模块最后一条指令不是 OpFunctionEnd"); return 0; }
    return 1;
}

// 入口必须指向本模块真实定义的、无参数、返回 void 的函数，且函数类型一致。
// The entry must reference a real, parameterless void function defined in this module with a matching function type.
static int check_entry_function(gmodule *m, char *why, size_t cap) {
    if (!check_id(m, m->entry_id, "entry function", why, cap)) return 0;
    gfunc *f = function_find(m, m->entry_id);
    if (!f) { set_why(why, cap, "入口 %u 没有对应的 OpFunction 定义", m->entry_id); return 0; }
    if (f->control != 0) { set_why(why, cap, "入口函数 control %u 非零", f->control); return 0; }
    gtype *rt = gtype_find(m, f->result_type);
    if (!rt || rt->kind != OP_TYPE_VOID) { set_why(why, cap, "入口函数返回类型不是 void"); return 0; }
    gtype *ft = gtype_find(m, f->function_type);
    if (!ft || ft->kind != OP_TYPE_FUNCTION) { set_why(why, cap, "入口函数类型不是 OpTypeFunction"); return 0; }
    if (ft->a != f->result_type) { set_why(why, cap, "入口函数返回类型与 OpTypeFunction 不一致"); return 0; }
    if (ft->member_count != 0) { set_why(why, cap, "入口函数类型带参数"); return 0; }
    return 1;
}

// 检查每个全局输入/输出变量：必须出现在 entry interface；拒绝 descriptor/push 和其他存储类。
// Check each global input/output variable: must be in the entry interface; reject descriptors/push and other storage classes.
static int check_vars(gmodule *m, uint32_t expected_model, char *why, size_t cap) {
    if (m->entry_count != 1) { set_why(why, cap, "模块没有恰好一个 OpEntryPoint"); return 0; }
    if (m->entry_model != expected_model) { set_why(why, cap, "执行模型 %u 不是期望的 %u", m->entry_model, expected_model); return 0; }
    if (!check_entry_function(m, why, cap)) return 0;
    if (strcmp(m->entry_name, "main")) { set_why(why, cap, "入口名 %s 不是 main", m->entry_name); return 0; }
    for (uint32_t i = 0; i < m->mode_count; i++) {
        if (m->modes[i].target != m->entry_id) { set_why(why, cap, "执行模式目标不是入口"); return 0; }
        if (m->modes[i].mode == MODE_LOCAL_SIZE) { set_why(why, cap, "图形模块出现 compute LocalSize"); return 0; }
        if (!(expected_model == MODEL_FRAGMENT && m->modes[i].mode == MODE_ORIGIN_UPPER_LEFT)) {
            set_why(why, cap, "不支持的图形执行模式 %u", m->modes[i].mode); return 0;
        }
    }
    for (uint32_t i = 0; i < m->var_count; i++) {
        gvar *v = &m->vars[i];
        gtype *p = gtype_find(m, v->type);
        if (!p || p->kind != OP_TYPE_POINTER || p->a != v->storage) { set_why(why, cap, "变量 %%%u 指针/存储类非法", v->id); return 0; }
        if (v->storage == STORAGE_FUNCTION) continue;
        if (v->storage != STORAGE_INPUT && v->storage != STORAGE_OUTPUT) {
            set_why(why, cap, "变量 %%%u 的存储类 %u 是 descriptor/push/其他，图形接口子集不支持", v->id, v->storage);
            return 0;
        }
        int listed = 0;
        for (uint32_t k = 0; k < m->iface_count; k++) if (m->iface[k] == v->id) listed = 1;
        if (!listed) { set_why(why, cap, "接口变量 %%%u 未出现在 OpEntryPoint interface", v->id); return 0; }
    }
    for (uint32_t k = 0; k < m->iface_count; k++) {
        int found = 0;
        for (uint32_t i = 0; i < m->var_count; i++) if (m->vars[i].id == m->iface[k] && m->vars[i].storage != STORAGE_FUNCTION) found = 1;
        if (!found) { set_why(why, cap, "entry interface id %u 不是输入/输出变量", m->iface[k]); return 0; }
    }
    return 1;
}

static int collect_interface(gmodule *m, uint32_t expected_model, uint32_t *pos_loc, uint32_t *color_loc,
                             uint32_t *pos_comp, uint32_t *color_comp, uint32_t *varying, char *why, size_t cap) {
    if (!check_vars(m, expected_model, why, cap)) return 0;
    uint32_t in_loc[8], in_comp[8], in_count = 0, out_loc[8], out_comp[8], out_count = 0, pos_builtin = 0;
    for (uint32_t i = 0; i < m->var_count; i++) {
        gvar *v = &m->vars[i];
        if (v->storage != STORAGE_INPUT && v->storage != STORAGE_OUTPUT) continue;
        gtype *p = gtype_find(m, v->type);
        gtype *t = gtype_find(m, p->b);
        gdecor *loc = decor_find(m, v->id, DEC_LOCATION);
        gdecor *builtin = decor_find(m, v->id, DEC_BUILTIN);
        if (v->storage == STORAGE_OUTPUT && t && t->kind == OP_TYPE_STRUCT) {
            if (expected_model != MODEL_VERTEX || !struct_has_position(m, p->b)) {
                set_why(why, cap, "不支持的 struct 输出块（仅 VS gl_PerVertex Position）"); return 0;
            }
            pos_builtin++;
            continue;
        }
        if (loc && builtin) { set_why(why, cap, "变量 %%%u 同时带 Location 与 BuiltIn", v->id); return 0; }
        if (!loc && !builtin) { set_why(why, cap, "接口变量 %%%u 既无 Location 也无 BuiltIn", v->id); return 0; }
        if (builtin) {
            if (!(expected_model == MODEL_VERTEX && v->storage == STORAGE_OUTPUT && builtin->a == BUILTIN_POSITION)) {
                set_why(why, cap, "不支持的 BuiltIn %u（仅 VS gl_Position）", builtin->a); return 0;
            }
            if (!type_vec_float(m, p->b, 4)) { set_why(why, cap, "gl_Position 类型不是 vec4 float"); return 0; }
            pos_builtin++;
            continue;
        }
        if (loc->a > 7) { set_why(why, cap, "Location %u 超出本子集", loc->a); return 0; }
        if (!t || t->kind != OP_TYPE_VECTOR || !type_vec_float(m, p->b, t->b) || (t->b != 2 && t->b != 4)) {
            set_why(why, cap, "Location %u 的接口类型不是 vec2/vec4 float", loc->a); return 0;
        }
        if (v->storage == STORAGE_INPUT) {
            if (in_count >= 8) { set_why(why, cap, "输入 location 过多"); return 0; }
            for (uint32_t k = 0; k < in_count; k++) if (in_loc[k] == loc->a) { set_why(why, cap, "输入 Location %u 重复", loc->a); return 0; }
            in_loc[in_count] = loc->a; in_comp[in_count] = t->b; in_count++;
        } else {
            if (out_count >= 8) { set_why(why, cap, "输出 location 过多"); return 0; }
            for (uint32_t k = 0; k < out_count; k++) if (out_loc[k] == loc->a) { set_why(why, cap, "输出 Location %u 重复", loc->a); return 0; }
            out_loc[out_count] = loc->a; out_comp[out_count] = t->b; out_count++;
        }
    }
    if (expected_model == MODEL_VERTEX) {
        if (in_count != 2 || in_comp[0] != 2 || in_comp[1] != 4 || in_loc[0] != 0 || in_loc[1] != 1) {
            set_why(why, cap, "VS 输入必须恰好 location 0 vec2 + location 1 vec4"); return 0;
        }
        if (out_count != 1 || out_comp[0] != 4) { set_why(why, cap, "VS 必须恰好一个 float4 varying"); return 0; }
        if (pos_builtin != 1) { set_why(why, cap, "VS 必须恰好一个 gl_Position"); return 0; }
        *pos_loc = 0; *color_loc = 1; *pos_comp = in_comp[0]; *color_comp = in_comp[1]; *varying = out_loc[0];
    } else {
        if (in_count != 1 || in_comp[0] != 4) { set_why(why, cap, "FS 必须恰好一个 float4 varying 输入"); return 0; }
        if (out_count != 1 || out_comp[0] != 4 || out_loc[0] != 0) { set_why(why, cap, "FS 必须恰好一个 location 0 float4 输出"); return 0; }
        if (pos_builtin) { set_why(why, cap, "FS 不允许 BuiltIn"); return 0; }
        *pos_loc = 0; *color_loc = 1; *pos_comp = 2; *color_comp = 4; *varying = in_loc[0];
    }
    return 1;
}

static int read_file(const char *path, uint8_t **out, size_t *size, size_t limit, char *why, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { set_why(why, cap, "打不开文件"); return 0; }
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0 || (size_t)ftell(f) > limit) { fclose(f); set_why(why, cap, "文件大小非法或超限"); return 0; }
    long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) { fclose(f); set_why(why, cap, "内存不足"); return 0; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); set_why(why, cap, "文件读取不完整"); return 0; }
    fclose(f); *out = buf; *size = (size_t)len; return 1;
}

static const char *find_sub(const char *p, const char *end, const char *needle) {
    size_t n = strlen(needle);
    if (!n || (size_t)(end - p) < n) return NULL;
    for (const char *q = p; q + n <= end; q++) if (!memcmp(q, needle, n)) return q;
    return NULL;
}

static int meta_get(const char *text, size_t len, const char *key, char *out, size_t cap, int *seen) {
    char pattern[32]; snprintf(pattern, sizeof pattern, "\"%s\"", key);
    const char *p = text, *end = text + len;
    int hits = 0;
    while (p < end) {
        const char *q = find_sub(p, end, pattern);
        if (!q) break;
        p = q + strlen(pattern); hits++;
        if (hits > 1) return 0;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end || *p++ != ':') return 0;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end || *p++ != '"') return 0;
        size_t n = 0;
        while (p < end && *p != '"') {
            if (*p == '\\' || n + 1 >= cap) return 0;
            out[n++] = *p++;
        }
        if (p >= end) return 0;
        out[n] = 0; *seen = 1; return 1;
    }
    return 0;
}

static int meta_shape_ok(const char *text, size_t len) {
    const char *p = text, *end = text + len;
    int seen = 0;
    int after_comma = 0;
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    if (p >= end || *p++ != '{') return 0;
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p < end && *p == '}') { if (after_comma) return 0; p++; break; }
        if (p >= end || *p++ != '"') return 0;
        after_comma = 0;
        const char *key = p;
        while (p < end && *p != '"') p++;
        if (p >= end) return 0;
        size_t key_len = (size_t)(p - key); p++;
        int bit = -1;
        if (key_len == 8 && !memcmp(key, "vs_stage", 8)) bit = 1;
        else if (key_len == 8 && !memcmp(key, "ps_stage", 8)) bit = 2;
        else if (key_len == 9 && !memcmp(key, "vs_sha256", 9)) bit = 4;
        else if (key_len == 9 && !memcmp(key, "ps_sha256", 9)) bit = 8;
        if (bit < 0 || (seen & bit)) return 0;
        seen |= bit;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end || *p++ != ':') return 0;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end || *p++ != '"') return 0;
        while (p < end && *p != '"') { if (*p == '\\') return 0; p++; }
        if (p >= end) return 0;
        p++;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p < end && *p == ',') { p++; after_comma = 1; continue; }
        if (p < end && *p == '}') { p++; break; }
        return 0;
    }
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p == end && seen == 15;
}

static int load_meta(const char *path, const dm_vk_graphics_plan *plan, char *why, size_t cap) {
    uint8_t *raw; size_t len;
    if (!read_file(path, &raw, &len, MAX_META_BYTES, why, cap)) return 0;
    char *text = (char *)raw;
    char vs_stage[16], ps_stage[16], vs_hash[65], ps_hash[65];
    int a = 0, b = 0, c = 0, d = 0, ok = 1;
    if (!meta_shape_ok(text, len)) ok = 0;
    if (ok && (!meta_get(text, len, "vs_stage", vs_stage, sizeof vs_stage, &a) ||
        !meta_get(text, len, "ps_stage", ps_stage, sizeof ps_stage, &b) ||
        !meta_get(text, len, "vs_sha256", vs_hash, sizeof vs_hash, &c) ||
        !meta_get(text, len, "ps_sha256", ps_hash, sizeof ps_hash, &d))) ok = 0;
    if (ok && (!a || !b || !c || !d || strcmp(vs_stage, "vs") || strcmp(ps_stage, "ps") ||
        strcmp(vs_hash, plan->vs_sha256) || strcmp(ps_hash, plan->ps_sha256))) ok = 0;
    free(raw);
    if (!ok) { set_why(why, cap, "stage/hash sidecar 与模块不一致或格式非法"); return 0; }
    return 1;
}

static int build_plan(const uint8_t *vs, size_t vs_size, const uint8_t *ps, size_t ps_size,
                      dm_vk_graphics_plan *plan, char *why, size_t cap) {
    gmodule *vm = calloc(1, sizeof *vm), *pm = calloc(1, sizeof *pm);
    if (!vm || !pm) { free(vm); free(pm); set_why(why, cap, "解析内存不足"); return 0; }
    int ok = scan_module(vs, vs_size, vm, why, cap) && scan_module(ps, ps_size, pm, why, cap);
    uint32_t p = 0, c = 0, v = 0, pc = 0, cc = 0;
    if (ok) ok = collect_interface(vm, MODEL_VERTEX, &p, &c, &pc, &cc, &v, why, cap);
    uint32_t fp = 0, fc = 0, fv = 0, fpc = 0, fcc = 0;
    if (ok) ok = collect_interface(pm, MODEL_FRAGMENT, &fp, &fc, &fpc, &fcc, &fv, why, cap);
    if (ok && (v != fv || v > 7)) { set_why(why, cap, "VS output location %u 与 FS input %u 不匹配", v, fv); ok = 0; }
    if (ok) {
        memset(plan, 0, sizeof *plan);
        plan->position_location = p; plan->color_location = c; plan->varying_location = v;
        plan->position_components = pc; plan->color_components = cc;
    }
    free(vm); free(pm);
    return ok;
}

VkResult dm_vk_graphics_plan_load(const char *vs_spv, const char *ps_spv, const char *metadata_path,
                                  dm_vk_graphics_plan *plan, char *why, size_t why_cap) {
    if (why && why_cap) why[0] = 0;
    if (!vs_spv || !ps_spv || !plan) { set_why(why, why_cap, "参数为空"); return VK_ERROR_INITIALIZATION_FAILED; }
    memset(plan, 0, sizeof *plan);
    uint8_t *vs = NULL, *ps = NULL; size_t vs_size = 0, ps_size = 0;
    if (!read_file(vs_spv, &vs, &vs_size, DM_VK_GRAPHICS_MAX_CODE, why, why_cap)) return VK_ERROR_INITIALIZATION_FAILED;
    if (!read_file(ps_spv, &ps, &ps_size, DM_VK_GRAPHICS_MAX_CODE, why, why_cap)) { free(vs); return VK_ERROR_INITIALIZATION_FAILED; }
    dm_vk_graphics_plan tmp;
    int ok = build_plan(vs, vs_size, ps, ps_size, &tmp, why, why_cap);
    if (ok) {
        dm_vk_sha256_hex(vs, vs_size, tmp.vs_sha256);
        dm_vk_sha256_hex(ps, ps_size, tmp.ps_sha256);
        if (metadata_path && !load_meta(metadata_path, &tmp, why, why_cap)) ok = 0;
    }
    if (ok) {
        tmp.vs_code = vs; tmp.ps_code = ps; tmp.vs_code_bytes = (uint32_t)vs_size; tmp.ps_code_bytes = (uint32_t)ps_size;
        tmp.sidecar_hash_bound = metadata_path != NULL;
        *plan = tmp; vs = ps = NULL;
    }
    free(vs); free(ps);
    return ok ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

void dm_vk_graphics_plan_free(dm_vk_graphics_plan *plan) {
    if (!plan) return;
    free(plan->vs_code); free(plan->ps_code);
    memset(plan, 0, sizeof *plan);
}

int dm_vk_graphics_plan_check(const dm_vk_graphics_plan *plan, char *why, size_t why_cap) {
    if (why && why_cap) why[0] = 0;
    if (!plan || !plan->vs_code || !plan->ps_code || !plan->vs_code_bytes || !plan->ps_code_bytes ||
        plan->vs_code_bytes > DM_VK_GRAPHICS_MAX_CODE || plan->ps_code_bytes > DM_VK_GRAPHICS_MAX_CODE) {
        set_why(why, why_cap, "plan 缺少自有模块字节"); return 0;
    }
    char h[65];
    dm_vk_sha256_hex(plan->vs_code, plan->vs_code_bytes, h);
    if (strcmp(h, plan->vs_sha256)) { set_why(why, why_cap, "VS sha256 与字节不一致"); return 0; }
    dm_vk_sha256_hex(plan->ps_code, plan->ps_code_bytes, h);
    if (strcmp(h, plan->ps_sha256)) { set_why(why, why_cap, "PS sha256 与字节不一致"); return 0; }
    dm_vk_graphics_plan ref;
    if (!build_plan(plan->vs_code, plan->vs_code_bytes, plan->ps_code, plan->ps_code_bytes, &ref, why, why_cap)) return 0;
    if (ref.position_location != plan->position_location || ref.color_location != plan->color_location ||
        ref.position_components != plan->position_components || ref.color_components != plan->color_components ||
        ref.varying_location != plan->varying_location) { set_why(why, why_cap, "plan 字段与模块字节不一致"); return 0; }
    return 1;
}
