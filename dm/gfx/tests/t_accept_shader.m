// 着色器界面验收（B0 #11）：用本文件自己写的反射 json（不是游戏着色器、不含游戏内容）驱动生产的
// nvn_refl_from_json，逐字段对照手算期望；负对照保证「种类写错」「超上限」「槽号缺失」「c1 不成对」都会被拒。
// 这证明的是反射→绑定的接口契约（#12 的输入），不是着色器翻译或 Vulkan 兼容性。
// Shader-interface acceptance (B0 #11): a reflection json written here (not a game shader, no game content) drives the
// production nvn_refl_from_json, with hand-computed expectations field by field; negative controls prove that a wrong
// binding kind, going over a fixed limit, a missing slot number and an odd-length c1 are all rejected.
// What this proves is the reflection-to-binding interface contract (the input side of #12), not shader translation or
// Vulkan compatibility.
#import "../nvn/nvn_refl.h"
#import <Foundation/Foundation.h>
#include <stdio.h>

static int cases, failed;
#define EXPECT(name, want, got) do { long long w_ = (long long)(want), g_ = (long long)(got); cases++;         \
    printf("%-46s 期望 %-8lld → 真结果 %-8lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); failed += w_ != g_; } while (0)

static NSDictionary *binding(NSString *kind, int reg, int aid, NSString *msl) {
    NSMutableDictionary *b = [@{@"kind": kind, @"reg": @(reg), @"arg_id": @(aid)} mutableCopy];
    if (msl) b[@"msl_type"] = msl;
    return b;
}

int main(void) { @autoreleasepool {
    nvn_refl r; char err[160];

    // 1. 自写反射：10 个绑定覆盖所有已实现种类，外加顶点输入、c1、线程组、驱动段读取标记
    // 1. Self-written reflection: 10 bindings covering every implemented kind, plus vertex inputs, c1, threadgroup size and driver-segment read flags
    NSArray *bs = @[
        binding(@"cbuf", 10, 3, nil),                        // 0  cbuffer → r10
        binding(@"tex", 5, 1, @"texture2d_array<float>"),    // 1  → 2DArray、FLOAT
        binding(@"samp", 2, 2, nil),                         // 2  → sampler r2
        binding(@"samp_tex", 6, 3, nil),                     // 3  → 合并句柄
        binding(@"img", 7, 4, @"texture3d<uint>"),           // 4  → 3D、UINT
        binding(@"tbuf", 9, 5, @"texture2d<int>"),           // 5  → 缓冲贴图、SINT
        binding(@"sbuf", 4, 6, nil),                         // 6  → 存储缓冲 r4
        binding(@"support", -1, 7, nil),                     // 7  无槽号
        binding(@"c1", -1, 8, nil),                          // 8  无槽号
        binding(@"drvcb", -1, 9, nil),                       // 9  bank 缺省 0 → DRV0
    ];
    NSMutableArray *mbs = [bs mutableCopy];
    mbs[6] = @{@"kind": @"sbuf", @"reg": @4, @"arg_id": @6, @"rw": @YES};
    mbs[9] = @{@"kind": @"drvcb", @"reg": @0, @"arg_id": @9, @"bank": @2};   // bank 2 → DRV2
    NSDictionary *j = @{@"route": @"B", @"bindings": mbs,
                        @"vertex_inputs": @[@{@"location": @0, @"msl_type": @"uint2"},
                                            @{@"location": @1, @"msl_type": @"float3"}],
                        @"threadgroup_size": @[@8, @4, @1], @"c1": @"0A0B"};
    EXPECT("自写反射转换成功", 0, nvn_refl_from_json(j, &r, err, sizeof err));
    EXPECT("绑定数", 10, r.nb);
    EXPECT("B 路线", 1, r.route_b);
    EXPECT("参数缓冲槽 = 0", 0, r.abi);
    EXPECT("[0] cbuf 种类/槽号/序号", NVN_RB_CBUF * 1000000 + 10 * 1000 + 3, r.b[0].kind * 1000000 + r.b[0].reg * 1000 + r.b[0].aid);
    EXPECT("[1] tex 种类/槽号", NVN_RB_TEX * 1000 + 5, r.b[1].kind * 1000 + r.b[1].reg);
    EXPECT("[1] tex 哑贴图类型 2DArray=3", 3, r.b[1].dtt);
    EXPECT("[1] tex 像素类 FLOAT", NVN_DPF_FLOAT, r.b[1].dpf);
    EXPECT("[2] samp 种类", NVN_RB_SAMP, r.b[2].kind);
    EXPECT("[3] samp_tex 种类", NVN_RB_SAMPT, r.b[3].kind);
    EXPECT("[4] img 种类/像素类 UINT", NVN_RB_IMG * 10 + NVN_DPF_UINT, r.b[4].kind * 10 + r.b[4].dpf);
    EXPECT("[4] img 哑贴图类型 3D=7", 7, r.b[4].dtt);
    EXPECT("[5] tbuf 种类/像素类 SINT", NVN_RB_TBUF * 10 + NVN_DPF_SINT, r.b[5].kind * 10 + r.b[5].dpf);
    EXPECT("[6] sbuf 种类/读写", NVN_RB_SBUFP * 10 + 1, r.b[6].kind * 10 + r.b[6].rw);
    EXPECT("[7] support 种类", NVN_RB_SUPPORT, r.b[7].kind);
    EXPECT("[8] c1 种类", NVN_RB_C1, r.b[8].kind);
    EXPECT("[9] drvcb bank 2 → DRV2", NVN_RB_DRV2, r.b[9].kind);
    EXPECT("有 c1、长度 2", 1 * 1000 + 2, r.has_c1 * 1000 + r.c1_len);
    EXPECT("c1 字节 = 0x0A 0x0B", 0x0A * 256 + 0x0B, r.c1[0] * 256 + r.c1[1]);
    EXPECT("顶点输入数", 2, r.nv);
    EXPECT("顶点输入[0] 属性号 = 位置 = uint", 0 * 10000 + 0 * 100 + NVN_CT_UINT, r.v[0].nvn * 10000 + r.v[0].loc * 100 + r.v[0].ct);
    EXPECT("顶点输入[1] 属性号 = 位置 = float", 1 * 10000 + 1 * 100 + NVN_CT_FLOAT, r.v[1].nvn * 10000 + r.v[1].loc * 100 + r.v[1].ct);
    EXPECT("线程组 8×4×1", 8 * 10000 + 4 * 100 + 1, r.tg[0] * 10000 + r.tg[1] * 100 + r.tg[2]);

    // 2. 负对照：契约外的输入必须被拒（返回非 0），不能静默产出可用的绑定表
    // 2. Negative controls: input outside the contract must be rejected (non-zero return); it must not silently produce a usable binding table
    {   NSDictionary *bad = @{@"route": @"B", @"bindings": @[binding(@"light", 1, 1, nil)]};
        EXPECT("未知绑定种类被拒", 1, nvn_refl_from_json(bad, &r, err, sizeof err) != 0); }
    {   NSDictionary *bad = @{@"route": @"B", @"bindings": @[binding(@"tex", -1, 1, @"texture2d<float>")]};
        EXPECT("贴图缺槽号被拒", 1, nvn_refl_from_json(bad, &r, err, sizeof err) != 0); }
    {   NSMutableArray *many = [NSMutableArray array];
        for (int i = 0; i < NVN_REFL_MAXB + 1; i++) [many addObject:binding(@"cbuf", i, i, nil)];
        NSDictionary *bad = @{@"route": @"B", @"bindings": many};
        EXPECT("绑定超上限被拒", 1, nvn_refl_from_json(bad, &r, err, sizeof err) != 0); }
    {   NSDictionary *bad = @{@"route": @"B", @"bindings": @[], @"c1": @"0A0"};
        EXPECT("c1 不成对被拒", 1, nvn_refl_from_json(bad, &r, err, sizeof err) != 0); }
    {   NSDictionary *bad = @{@"route": @"A", @"bindings": @[]};
        EXPECT("非 B 路线被拒", 1, nvn_refl_from_json(bad, &r, err, sizeof err) != 0); }

    printf("== %d 项，失败 %d ==\n", cases, failed);
    return failed != 0;
} }
