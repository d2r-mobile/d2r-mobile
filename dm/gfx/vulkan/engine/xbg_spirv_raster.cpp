// 全覆盖首绘证明要的着色器事实（见 xbg_spirv_raster.h）。顶点：着色器结束时 gl_Position 的 x、y、z 是否原样是同一个输入的
// 0、1、2 分量（w 为常数 1 或同输入的 .w）。只认单块、不调函数、不写 ClipDistance / CullDistance 的模块；每个分量取最后一次写，
// 写的值只跟穿 OpCopyObject、OpCompositeExtract 和整向量的 OpCompositeConstruct。认不出的一律不成立。片元：声明了哪些输出号、
// 是否声明 SampleMask 输出。
// Shader facts for the full-coverage first-draw proof (see xbg_spirv_raster.h). Vertex: whether gl_Position's x, y and z,
// when the shader ends, are components 0, 1 and 2 of one input as they are (w the constant 1 or the same input's .w). Only a
// module with one block, no function call and no ClipDistance or CullDistance write qualifies; each component takes its last
// store, and a stored value is followed only through OpCopyObject, OpCompositeExtract and a whole-vector
// OpCompositeConstruct. Anything else does not qualify. Fragment: which output locations are declared, and whether a
// SampleMask output is.
#include "xbg_spirv_raster.h"
#include <vector>

namespace {
enum : uint32_t {
    OpTypeStruct = 30, OpTypePointer = 32, OpConstant = 43, OpFunction = 54, OpFunctionCall = 57, OpVariable = 59, OpLoad = 61,
    OpStore = 62, OpCopyMemory = 63, OpCopyMemorySized = 64, OpAccessChain = 65, OpInBoundsAccessChain = 66, OpDecorate = 71,
    OpMemberDecorate = 72, OpCompositeConstruct = 80, OpCompositeExtract = 81, OpCopyObject = 83, OpLabel = 248,
    DecBuiltIn = 11, DecLocation = 30, BuiltInPosition = 0, BuiltInClipDistance = 3, BuiltInCullDistance = 4,
    BuiltInSampleMask = 20, StorageInput = 1, StorageOutput = 3,
};
// A value as the proof sees it: kind 1 = component comp of the input at location loc, 2 = the float constant 1.0,
// 3 = the whole input vector at loc, 0 = anything else.
struct Src { int kind = 0, loc = -1, comp = -1; };
}

XbgRasterFacts xbg_spirv_raster_facts(const uint32_t *in, size_t n, uint32_t stage) {
    XbgRasterFacts f;
    if (!in || n < 5 || in[0] != 0x07230203u || (stage != 0 && stage != 4)) return f;
    const uint32_t bound = in[3];
    std::vector<int> loc(bound, -1), builtin(bound, -1), var_sc(bound, -1), var_type(bound, 0), ptr_pointee(bound, 0);
    std::vector<int> pos_member(bound, -1), clip_member(bound, 0);
    std::vector<int64_t> konst(bound, -1);
    std::vector<uint8_t> one(bound, 0);
    size_t first_func = 0, functions = 0;
    for (size_t i = 5; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        if (!wc || i + wc > n) return f;
        const uint32_t *a = in + i + 1;
        if (op == OpDecorate && wc >= 4 && a[0] < bound) { if (a[1] == DecLocation) loc[a[0]] = int(a[2]); if (a[1] == DecBuiltIn) builtin[a[0]] = int(a[2]); }
        if (op == OpMemberDecorate && wc >= 5 && a[0] < bound && a[2] == DecBuiltIn) {
            if (a[3] == BuiltInPosition) pos_member[a[0]] = int(a[1]);
            if ((a[3] == BuiltInClipDistance || a[3] == BuiltInCullDistance) && a[1] < 32) clip_member[a[0]] |= 1 << a[1];
        }
        if (op == OpTypePointer && wc == 4 && a[0] < bound) ptr_pointee[a[0]] = int(a[2]);
        if (op == OpConstant && wc == 4 && a[1] < bound) { konst[a[1]] = a[2]; if (a[2] == 0x3f800000u) one[a[1]] = 1; }
        if (op == OpVariable && wc >= 4 && a[1] < bound && a[0] < bound) { var_sc[a[1]] = int(a[2]); var_type[a[1]] = ptr_pointee[a[0]]; }
        if (op == OpFunction) { ++functions; if (!first_func) first_func = i; }
        i += wc;
    }
    if (stage == 4) {
        for (uint32_t v = 0; v < bound; ++v) if (var_sc[v] == int(StorageOutput)) {
            if (loc[v] >= 0 && loc[v] < 32) f.outputs |= 1u << loc[v];
            if (builtin[v] == int(BuiltInSampleMask)) f.sample_mask = true;
        }
        return f;
    }
    // The position: an Output variable that is BuiltIn Position, or an Output block whose member is.
    int pvar = -1, pmem = -1, cmask = 0; std::vector<uint8_t> clipvar(bound, 0);
    for (uint32_t v = 0; v < bound; ++v) if (var_sc[v] == int(StorageOutput)) {
        if (builtin[v] == int(BuiltInPosition)) { if (pvar >= 0) return f; pvar = int(v); pmem = -1; }
        if (builtin[v] == int(BuiltInClipDistance) || builtin[v] == int(BuiltInCullDistance)) clipvar[v] = 1;
        uint32_t t = uint32_t(var_type[v]);
        if (t && t < bound && pos_member[t] >= 0) { if (pvar >= 0) return f; pvar = int(v); pmem = pos_member[t]; cmask = clip_member[t]; }
    }
    if (pvar < 0 || functions != 1) return f;
    std::vector<Src> src(bound);
    std::vector<uint32_t> chain_base(bound, 0); std::vector<std::vector<uint32_t>> chain_idx(bound);
    std::vector<std::vector<uint32_t>> parts(bound);
    auto value = [&](uint32_t id) { Src s; if (id < bound) { s = src[id]; if (one[id]) s.kind = 2; } return s; };
    Src comp[4];
    size_t labels = 0;
    for (size_t i = first_func; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff; const uint32_t *a = in + i + 1;
        if (op == OpLabel && ++labels > 1) return f;
        if (op == OpFunctionCall || op == OpCopyMemory || op == OpCopyMemorySized) return f;
        if ((op == OpAccessChain || op == OpInBoundsAccessChain) && wc >= 4 && a[1] < bound) {
            uint32_t base = a[2]; std::vector<uint32_t> idx(a + 3, a + wc - 1);
            if (base < bound && chain_base[base]) { std::vector<uint32_t> pre = chain_idx[base]; pre.insert(pre.end(), idx.begin(), idx.end()); idx.swap(pre); base = chain_base[base]; }
            chain_base[a[1]] = base; chain_idx[a[1]] = idx;
        }
        if (op == OpCopyObject && wc == 4 && a[1] < bound && a[2] < bound) { src[a[1]] = src[a[2]]; one[a[1]] = one[a[2]]; chain_base[a[1]] = chain_base[a[2]]; chain_idx[a[1]] = chain_idx[a[2]]; parts[a[1]] = parts[a[2]]; }
        if (op == OpLoad && wc >= 4 && a[1] < bound && a[2] < bound) {
            uint32_t p = a[2], base = chain_base[p] ? chain_base[p] : p;
            if (base < bound && var_sc[base] == int(StorageInput) && loc[base] >= 0) {
                if (!chain_base[p]) src[a[1]] = Src{3, loc[base], -1};
                else if (chain_idx[p].size() == 1 && chain_idx[p][0] < bound && konst[chain_idx[p][0]] >= 0 && konst[chain_idx[p][0]] < 4) src[a[1]] = Src{1, loc[base], int(konst[chain_idx[p][0]])};
            }
        }
        if (op == OpCompositeExtract && wc == 5 && a[1] < bound && a[2] < bound && src[a[2]].kind == 3 && a[3] < 4) src[a[1]] = Src{1, src[a[2]].loc, int(a[3])};
        if (op == OpCompositeConstruct && wc >= 4 && a[1] < bound) parts[a[1]].assign(a + 2, a + wc - 1);
        if (op == OpStore && wc >= 3) {
            uint32_t p = a[0], v = a[1];
            uint32_t base = p < bound && chain_base[p] ? chain_base[p] : p;
            if (base < bound && clipvar[base]) return f;
            if (int(base) == pvar) {
                std::vector<uint32_t> idx = p < bound && chain_base[p] ? chain_idx[p] : std::vector<uint32_t>();
                for (uint32_t x : idx) if (x >= bound || konst[x] < 0) return f;
                size_t at = 0;
                if (pmem >= 0) {
                    if (idx.empty()) return f;
                    int64_t m = konst[idx[0]];
                    if (m < 32 && (cmask >> m & 1)) return f;
                    if (m != pmem) { i += wc; continue; }
                    at = 1;
                }
                if (idx.size() == at + 1) { int64_t c = konst[idx[at]]; if (c > 3) return f; comp[c] = value(v); }
                else if (idx.size() == at) {
                    Src s = value(v);
                    if (s.kind == 3) for (int c = 0; c < 4; ++c) comp[c] = Src{1, s.loc, c};
                    else if (v < bound && parts[v].size() == 4) for (int c = 0; c < 4; ++c) comp[c] = value(parts[v][c]);
                    else for (int c = 0; c < 4; ++c) comp[c] = Src{};
                } else return f;
            }
        }
        i += wc;
    }
    int L = comp[0].loc;
    for (int c = 0; c < 3; ++c) if (comp[c].kind != 1 || comp[c].loc != L || comp[c].comp != c) return f;
    if (comp[3].kind == 2) f.pos_w1 = true;
    else if (!(comp[3].kind == 1 && comp[3].loc == L && comp[3].comp == 3)) return f;
    f.pos_input = L;
    return f;
}
