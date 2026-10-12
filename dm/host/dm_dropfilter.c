// 高价值掉落的光柱与一档掉落音。
// 原有的「掉落筛选」（地上被筛物品不建名字标签）已整体拆掉：掉落规则一律照红叶 mod，红叶不藏任何物品（它的「筛选」是改名字写法，归注释覆盖层）。
// 产物里入口 0x43b1e0 的观测门 9 已随 r38 从 patch_nso.py 删掉（编号 9 空着不复用）。
// 读物品（静态读码）：码 main+0x570260(单位)；是否某类型（含父类型）main+0x574bc0(单位, 类型号)；品质 = 数据块（单位 +0x10）+0 的 dword。前两个遇到空单位 / 非物品会断言退出，调用前自己核。
// Light beams for high-value drops and the top-tier drop sound.
// The former "drop filter" (no name label built for filtered items on the ground) has been removed entirely: drop rules follow the Hongye community mod
// throughout, and Hongye hides no items (its "filter" changes how names are written, which belongs to the annotation overlay).
// Observation gate 9 at entry 0x43b1e0 in the build output was removed from patch_nso.py with r38 (number 9 stays vacant and is not reused).
// Reading an item (read statically from the code): code main+0x570260(unit); whether it is of a type (including parent types) main+0x574bc0(unit, type
// id); quality = the dword at +0 of the data block (unit +0x10). The first two assert and exit on a null unit / non-item, so check before calling.
#include "dm_custom.h"
#include "hle.h"
#include "dm.h"
#include <stdlib.h>
#include <string.h>

static uint8_t *main_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }

// 3.3 itemtypes.txt 行序（Any = 0，跳过 Expansion 行；3.3 在 Druid Item 后加了 Warlock Item，67 起比 1.10 大 1）
// Row order of 3.3 itemtypes.txt (Any = 0, the Expansion row is skipped; 3.3 added Warlock Item after Druid Item, so from 67 on it is 1 larger than 1.10)
enum { T_RING = 10, T_AMUL = 12, T_SCHA = 83, T_MCHA = 84, T_LCHA = 85 };
static int code_in(uint32_t c, const char *const *l) { for (; *l; l++) if (!memcmp(&c, *l, 4)) return 1; return 0; }

// ---------- 高价值掉落的光（第二批）----------
// 地上物品取模型路径：所有单位都经 main+0xa14dc4(资源管理器, 单位, 输出字符串 x8) 取外观定义路径；物品走调用点 0x8fa590（存储 / 穿戴 / 腰带 / 镶嵌 / 地上共用），
// 剪口把那条 bl 换成 svc #0x87，这里原样代调后，只对「模式 3 在地上 / 5 掉落中」的物品按规则把路径改指到覆盖层里的带光变体
// （dm/ios/tools/drop_light.py：data/hd/items/<类别>/zswg/<档>__<模型名，/ 换 __>.json）。穿戴 / 背包 / 腰带不碰 ⇒ 身上手里不亮；
// 外观只在建的那一刻取路径（新掉落在模式 5 建一次），所以开关切换后新掉落 / 重进区域的物品才按新设置。
// 档位：一档 = 暗金（未鉴定也算：按品质，不靠 uniques.json）、高符文 23–33；二档 = 套装、戒指 / 项链 / 护身符、三把钥匙、四种精华、赦免之证。
// 开关：「自定义」页 drop_light；DM_DROP_LIGHT=0 / 1 强制。DM_DROPF_LOG=1 时每次改指记一行。
// ---------- Light on high-value drops (second batch) ----------
// Model path of items on the ground: all units get their appearance definition path via main+0xa14dc4(resource manager, unit, output string x8); items go through call site 0x8fa590 (shared by stored / worn / belt / socketed / on the ground),
// the cut replaces that bl with svc #0x87, and here, after calling through unchanged, only for items in 'mode 3 on the ground / 5 dropping' the path is redirected by rule to the lit variant in the overlay
// (dm/ios/tools/drop_light.py: data/hd/items/<category>/zswg/<tier>__<model name, / replaced by __>.json). Worn / inventory / belt are not touched => nothing lights up on the body or in hand;
// the appearance takes its path only at the moment it is built (a new drop is built once in mode 5), so after the switch is toggled only new drops / items in a re-entered area follow the new setting.
// Tiers: tier 1 = uniques (unidentified ones count too: by quality, not relying on uniques.json), high runes 23–33; tier 2 = sets, rings / amulets / charms, the three keys, the four essences, Token of Absolution.
// Switch: drop_light on the 「自定义」(Custom) page; DM_DROP_LIGHT=0 / 1 forces it. With DM_DROPF_LOG=1 every redirect is logged as one line.
static int light_tier(uint8_t *b, uint64_t u) {
    static int force = -2; if (force == -2) { const char *e = getenv("DM_DROP_LIGHT"); force = e ? strcmp(e, "0") != 0 : -1; }
    if (!(force >= 0 ? force : dm_custom_get("drop_light"))) return 0;
    int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(b + 0x574bc0);
    uint32_t code = ((uint32_t (*)(uint64_t))(b + 0x570260))(u);
    const uint8_t *d = (const uint8_t *)(uintptr_t)*(uint64_t *)(u + 0x10); uint32_t q = d ? *(const uint32_t *)d : 0;
    static const char *const high_rune[] = {"r23 ", "r24 ", "r25 ", "r26 ", "r27 ", "r28 ", "r29 ", "r30 ", "r31 ", "r32 ", "r33 ", 0},
        *const quest[] = {"pk1 ", "pk2 ", "pk3 ", "tes ", "ceh ", "bet ", "fed ", "toa ", 0};
    if (q == 7 || code_in(code, high_rune)) return 1;
    if (q == 5 || is(u, T_RING) || is(u, T_AMUL) || is(u, T_SCHA) || is(u, T_MCHA) || is(u, T_LCHA) || code_in(code, quest)) return 2;
    return 0;
}
void svc_ground_asset(uint64_t *regs) {   // svc #0x87（剪口 0x8fa590：bl 0xa14dc4 → svc）：原样代调，地上物品按规则改指带光变体 | svc #0x87 (cut 0x8fa590: bl 0xa14dc4 -> svc): called through unchanged, and items on the ground are redirected by rule to the lit variant
    uint8_t *b = main_base(); uint64_t u = regs[1]; uint8_t *out = (uint8_t *)(uintptr_t)regs[8];
    {   register uint64_t x0 __asm__("x0") = regs[0], x1 __asm__("x1") = u; register uint64_t x8 __asm__("x8") = regs[8]; register uint64_t x16 __asm__("x16") = (uint64_t)(uintptr_t)(b + 0xa14dc4);
        __asm__ volatile("blr x16" : "+r"(x0), "+r"(x1), "+r"(x8), "+r"(x16) : : "x2", "x3", "x4", "x5", "x6", "x7", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x17", "x30",
            "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31", "cc", "memory"); }
    if (!u || *(uint32_t *)u != 4 || (*(uint32_t *)(u + 0xc) != 3 && *(uint32_t *)(u + 0xc) != 5)) return;
    int tier = light_tier(b, u); if (!tier) return;
    // 输出字符串 {数据指针, 长度, 容量（最高位是标志）, 内联缓冲}（0xa152f4–0xa15348 的写法）；新路径只在容量够时原地写
    // Output string {data pointer, length, capacity (top bit is a flag), inline buffer} (as written at 0xa152f4–0xa15348); the new path is written in place only when the capacity is sufficient
    char *s = *(char **)out; uint64_t len = *(uint64_t *)(out + 8), cap = *(uint64_t *)(out + 0x10) & 0x7FFFFFFFFFFFFFFFull;
    static const char pre[] = "data/hd/items/"; if (!s || len < sizeof pre + 5 || strncmp(s, pre, sizeof pre - 1) || strcmp(s + len - 5, ".json")) return;
    const char *root = s + sizeof pre - 1, *slash = strchr(root, '/'); if (!slash || slash >= s + len - 5) return;
    char nw[256]; int n = snprintf(nw, sizeof nw, "%.*szswg/%d__", (int)(slash + 1 - s), s, tier);
    for (const char *p = slash + 1; p < s + len - 5 && n < (int)sizeof nw - 8; p++) {
        if (*p == '/') { nw[n++] = '_'; nw[n++] = '_'; } else nw[n++] = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p); }
    n += snprintf(nw + n, sizeof nw - n, ".json");
    static int log = -1; if (log < 0) log = getenv("DM_DROPF_LOG") != NULL;
    if ((uint64_t)n >= cap) { if (log) LOG("掉落光：%s 改指 %s 放不下（容量 %llu）", s, nw, (unsigned long long)cap); return; }
    if (log) LOG("掉落光：物品 id %u 模式 %u 第 %d 档 %s → %s", *(uint32_t *)(u + 8), *(uint32_t *)(u + 0xc), tier, s, nw);
    memcpy(s, nw, (size_t)n + 1); *(uint64_t *)(out + 8) = (uint64_t)n;
}

// 一档掉落音：落地时游戏用 main+0x470c20(物品, &触发帧) 取掉落音（物品表 +0x134 / 触发帧 +0x138，暗金 / 套装有自己的就用自己的），
// 地上落地那一处调用点是 0x468f2c（宿主私有产物实测：掉 r30 / rin 时只有它、单位模式 5；其余 24 处是界面放置音）。剪口把它换成 svc #0x88，
// 这里原样代调，一档物品把返回的声音号换成 TIER1_SOUND（sounds 表序号；40 = quest_horadric_staff_light 赫拉迪克法杖光，经表里的重定向播高清版。
// 缺省：不用任务完成提示音 14，它在原版里有确定含义）。其余物品原样。开关同光柱（light_tier）。DM_TEST_DROP_SOUND=<序号> 换候选（试听用）。
// Tier-1 drop sound: on landing the game uses main+0x470c20(item, &trigger frame) to get the drop sound (item table +0x134 / trigger frame +0x138; uniques / sets use their own if they have one),
// and the call site for landing on the ground is 0x468f2c (measured with the host-private build output: when r30 / rin drop only this one fires, unit mode 5; the other 24 sites are UI placement sounds). The cut replaces it with svc #0x88,
// which is called through unchanged here, and for tier-1 items the returned sound number is replaced with TIER1_SOUND (index into the sounds table; 40 = quest_horadric_staff_light, the Horadric Staff light, which plays the HD version through the redirect in the table.
// Default: the quest-complete cue 14 is not used, since it has a definite meaning in the original game). Other items are unchanged. Same switch as the light beam (light_tier). DM_TEST_DROP_SOUND=<index> swaps in a candidate (for auditioning).
enum { TIER1_SOUND = 40 };
void svc_drop_sound(uint64_t *regs) {   // svc #0x88（剪口 0x468f2c：bl 0x470c20 → svc） | svc #0x88 (cut 0x468f2c: bl 0x470c20 -> svc)
    uint8_t *b = main_base(); uint64_t u = regs[0];
    uint32_t snd = ((uint32_t (*)(uint64_t, uint64_t))(b + 0x470c20))(u, regs[1]); regs[0] = snd;
    { void dm_drop_haptic_land(uint8_t *, uint64_t); dm_drop_haptic_land(b, u); }   // 高价值掉落震动（dm_drophaptic.c）：落地这一刻 | High-value drop haptics (dm_drophaptic.c): the moment of landing
    if (!u || *(uint32_t *)u != 4 || light_tier(b, u) != 1) return;
    static int alt = -2; if (alt == -2) { const char *e = getenv("DM_TEST_DROP_SOUND"); alt = e ? atoi(e) : -1; }
    regs[0] = (uint32_t)(alt > 0 ? alt : TIER1_SOUND);
    static int log = -1; if (log < 0) log = getenv("DM_DROPF_LOG") != NULL;
    if (log) LOG("掉落音：物品 id %u 第 1 档 原音 %u → 新音 %u", *(uint32_t *)(u + 8), snd, (uint32_t)regs[0]);
}
