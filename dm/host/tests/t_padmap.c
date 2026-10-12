// 实体手柄面键映射（dm_padmap.h）：各类型 × 四个面键 → 游戏键。期望值按规则手写：印什么字母就是游戏的什么键；任天堂键帽 右 A 下 B 上 X 左 Y，Xbox 键帽 下 A 右 B 左 X 上 Y；别的按 Xbox 排。
// Physical controller face button mapping (dm_padmap.h): each type x four face buttons -> game keys. Expected values are hand-written from
// the rule: whatever letter is printed is that game key; Nintendo glyphs are right A, bottom B, top X, left Y, Xbox glyphs are bottom A,
// right B, left X, top Y; everything else is laid out as Xbox.
#include <stdio.h>
#include "../dm_padmap.h"
static int checks, fails;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) {
    struct { const char *cat, *ven; int kind; } id[] = {
        { "Switch Pro Controller", "Pro Controller", DM_PADKIND_NINTENDO }, { "Nintendo Switch Joy-Con (L/R)", "Joy-Con (L/R)", DM_PADKIND_NINTENDO },
        { "Nintendo Switch Joy-Con (L)", "Joy-Con (L)", DM_PADKIND_NINTENDO }, { "MFi", "NINTENDO compatible pad", DM_PADKIND_NINTENDO },
        { "Xbox One", "Xbox Wireless Controller", DM_PADKIND_XBOX }, { "DualSense", "DualSense Wireless Controller", DM_PADKIND_XBOX },
        { "DualShock 4", "DUALSHOCK 4 Wireless Controller", DM_PADKIND_XBOX }, { "MFi", "SteelSeries Nimbus", DM_PADKIND_XBOX }, { NULL, NULL, DM_PADKIND_XBOX }, { "", "", DM_PADKIND_XBOX } };
    for (unsigned i = 0; i < sizeof id / sizeof *id; i++) CHECK(dm_padmap_kind(id[i].cat, id[i].ven) == id[i].kind, "类别「%s」厂商名「%s」→ %s 排法", id[i].cat ? id[i].cat : "(空)", id[i].ven ? id[i].ven : "(空)", id[i].kind ? "任天堂" : "Xbox");
    // 下 右 左 上
    // bottom right left top
    CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 1, 0, 0, 0) == DM_BTN_B, "任天堂：下 = B"); CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 1, 0, 0) == DM_BTN_A, "任天堂：右 = A");
    CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 0, 1, 0) == DM_BTN_Y, "任天堂：左 = Y"); CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 0, 0, 1) == DM_BTN_X, "任天堂：上 = X");
    CHECK(dm_padmap_face(DM_PADKIND_XBOX, 1, 0, 0, 0) == DM_BTN_A, "Xbox：下 = A"); CHECK(dm_padmap_face(DM_PADKIND_XBOX, 0, 1, 0, 0) == DM_BTN_B, "Xbox：右 = B");
    CHECK(dm_padmap_face(DM_PADKIND_XBOX, 0, 0, 1, 0) == DM_BTN_X, "Xbox：左 = X"); CHECK(dm_padmap_face(DM_PADKIND_XBOX, 0, 0, 0, 1) == DM_BTN_Y, "Xbox：上 = Y");
    CHECK(dm_padmap_face(DM_PADKIND_XBOX, 1, 1, 0, 0) == (DM_BTN_A | DM_BTN_B), "两颗同时按：各算各的"); CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 0, 0, 0) == 0, "都没按 = 0");
    printf("== %d 项，失败 %d ==\n", checks, fails);
    return fails != 0;
}
