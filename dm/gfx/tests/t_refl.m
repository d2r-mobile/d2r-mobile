// 反射转换单测：真实 B 反射 json（Switch 原版翻译，build/cache/metal_shader_cache/<键>_b.json）→ C 数组，期望按 json 原文手抄。
// 哑贴图类型 MTLTextureType：1D=0 1DArray=1 2D=2 2DArray=3 Cube=5 CubeArray=6 3D=7
// Reflection conversion unit tests: real B reflection json (translation of the original Switch shaders, build/cache/metal_shader_cache/<key>_b.json) → C arrays, expectations hand-copied from the json text.
// Dummy texture type MTLTextureType: 1D=0 1DArray=1 2D=2 2DArray=3 Cube=5 CubeArray=6 3D=7
#import "../nvn/nvn_refl.h"
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include "../nvn/dmsh_chain.h"

enum { C = NVN_RB_CBUF, S = NVN_RB_SAMP, T = NVN_RB_TEX, I = NVN_RB_IMG, TB = NVN_RB_TBUF };
enum { F = NVN_DPF_FLOAT, U = NVN_DPF_UINT, D = NVN_DPF_DEPTH };
static int fail, n;
static void check(const char *what, long want, long got) {
    n++; int ok = want == got; fail += !ok;
    if (!ok) printf("  %-40s 期望 %ld → 真结果 %ld ✗\n", what, want, got);
}
static NSDictionary *load(const char *key) {
    NSString *p = [NSString stringWithFormat:@"%s%s/build/cache/metal_shader_cache/b-%s/%s.json", getenv("DM_ROOT") ?: getenv("HOME"), getenv("DM_ROOT") ? "" : "/dm-work", DMSH_CHAIN, key];   // 现行翻译链版本的那一份库 | the library of the current translation chain version
    NSData *d = [NSData dataWithContentsOfFile:p];
    if (!d) { printf("【红】缺反射样本 %s\n", p.UTF8String); fail++; return nil; }
    return [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
}

int main(void) { @autoreleasepool {
    nvn_refl r; char err[160] = "";
    {   int f0 = fail;   // A 路线已退役：PC 链反射（无 route = B）必须被拒 | Route A is retired: PC-chain reflection (no route = B) must be rejected
        NSDictionary *pc = @{@"arg_buffer_index": @0, @"bindings": @[@{@"type": @"cbuffer", @"reg": @10, @"arg_id": @0, @"access": @"constant", @"msl_type": @"constant cb1_struct*"}], @"vertex_inputs": @[]};
        check("PC 链反射（无 route = B）报失败", 1, nvn_refl_from_json(pc, &r, err, sizeof err) != 0);
        printf("%-60s %s\n", "负对照：PC 链反射被拒（A 路线已退役）", fail == f0 ? "✓" : "✗");
    }

    // B 路线（Switch 原版翻译，tools/dmsh_build.py 出）：真实反射 ps_models_p#0 / vs_models_v#0，期望按 json 原文手抄
    // Route B (translation of the original Switch shaders, produced by tools/dmsh_build.py): real reflection ps_models_p#0 / vs_models_v#0, expectations hand-copied from the json text
    {   NSDictionary *j = load("5c1ca4e7c3e4657c_b");
        if (j) { int f1 = fail;
            check("B 片元：转换成功", 0, nvn_refl_from_json(j, &r, err, sizeof err));
            check("B 片元：B 标记", 1, r.route_b); check("B 片元：参数缓冲槽", 0, r.abi); check("B 片元：绑定数", 44, r.nb); check("B 片元：顶点输入数", 0, r.nv);
            check("B 片元：有 c1", 1, r.has_c1); check("B 片元：c1 长 256（控制段字段）", 256, r.c1_len);
            uint32_t c1w; memcpy(&c1w, r.c1, 4); check("B 片元：c1 首字 = −√1.5（0xbf9cc471）", 0xbf9cc471u, c1w);
            struct { int i, kind, reg, aid, rw, dtt, dpf; } w[] = {
                {0, NVN_RB_SUPPORT, 0, 0, 0, -1, -1}, {1, C, 10, 1, 0, -1, -1}, {4, NVN_RB_C1, 0, 4, 0, -1, -1}, {7, NVN_RB_DRV0, 0, 7, 0, -1, -1},
                {8, NVN_RB_DRV2, 0, 8, 0, -1, -1}, {11, NVN_RB_SBUFP, 4, 11, 0, -1, -1}, {13, TB, 8, 13, 0, -1, -1}, {14, T, 41, 14, 0, 2, F},
                {15, S, 3, 15, 0, -1, -1}, {28, T, 39, 28, 0, 3, D}, {34, T, 33, 34, 0, 5, D}, {36, T, 46, 36, 0, 7, F}};
            for (unsigned k = 0; k < sizeof w / sizeof *w; k++) {
                const nvn_rbind *g = &r.b[w[k].i]; char m[64];
                snprintf(m, sizeof m, "B 绑定[%d] 种类", w[k].i); check(m, w[k].kind, g->kind);
                snprintf(m, sizeof m, "B 绑定[%d] 槽号", w[k].i); check(m, w[k].reg, g->reg);
                snprintf(m, sizeof m, "B 绑定[%d] 参数序号", w[k].i); check(m, w[k].aid, g->aid);
                snprintf(m, sizeof m, "B 绑定[%d] 读写", w[k].i); check(m, w[k].rw, g->rw);
                if (w[k].dtt >= 0) { snprintf(m, sizeof m, "B 绑定[%d] 哑贴图类型", w[k].i); check(m, w[k].dtt, g->dtt);
                                     snprintf(m, sizeof m, "B 绑定[%d] 哑贴图像素类", w[k].i); check(m, w[k].dpf, g->dpf); }
            }
            printf("%-60s %s\n", "B 片元 ps_models_p#0：44 个绑定（含 support / c1 / 驱动段 / 存储缓冲指针）", fail == f1 ? "✓" : "✗");
        } }
    {   NSDictionary *j = load("54f730a6c0efc118_b");
        if (j) { int f1 = fail;
            check("B 顶点：转换成功", 0, nvn_refl_from_json(j, &r, err, sizeof err));
            check("B 顶点：绑定数", 2, r.nb); check("B 顶点：输入数", 4, r.nv); check("B 顶点：c1 长 0（控制段 +0x704 − +0x700）→ 无 c1", 0, r.has_c1);
            for (int i = 0; i < 4 && i < r.nv; i++) { char m[48]; snprintf(m, sizeof m, "B 顶点输入[%d] 属性号 = 位置", i); check(m, i, r.v[i].nvn); check(m, i, r.v[i].loc); check("B 顶点分量 float", NVN_CT_FLOAT, r.v[i].ct); }
            check("B 顶点：绑定[0] uniform 槽 0", 0, r.b[0].reg); check("B 顶点：绑定[1] uniform 槽 3", 3, r.b[1].reg);
            printf("%-60s %s\n", "B 顶点 vs_models_v#0：4 个属性（属性号 = MSL attribute）", fail == f1 ? "✓" : "✗");
        } }
    {   int f1 = fail;
        NSDictionary *b1 = @{@"route": @"B", @"bindings": @[@{@"kind": @"foo", @"arg_id": @0, @"msl_name": @"x", @"msl_type": @"constant _16*"}]};
        NSDictionary *b2 = @{@"route": @"B", @"bindings": @[@{@"kind": @"tex", @"reg": [NSNull null], @"arg_id": @0, @"msl_name": @"fp_t_x", @"msl_type": @"texture2d<float>"}]};
        check("B 未知种类报失败", 1, nvn_refl_from_json(b1, &r, err, sizeof err) != 0);
        check("B 贴图没解出槽号报失败", 1, nvn_refl_from_json(b2, &r, err, sizeof err) != 0);
        printf("%-60s %s\n", "B 负对照：未知种类 / 槽号缺失", fail == f1 ? "✓" : "✗");
    }
    printf("== %d 项，失败 %d ==\n", n, fail);
    return fail != 0;
} }
