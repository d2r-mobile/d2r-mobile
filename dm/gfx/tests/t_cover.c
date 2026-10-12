// 全覆盖首绘几何证明单测（nvn_cover.h）：期望值按几何手算，不跑被测实现。
// 正例：全屏大三角形（-1,-1)(3,-1)(-1,3)、全屏四边形条带；负例：视口只占附件一部分、剪裁只占一部分、三角形缺一角、
// 被背面剔除、z 越界、w ≠ 1、两三角形各包一半（单个三角形都包不住四角，按规则不成立——保守）。
// Unit tests for the geometric proof of a full-coverage first draw (nvn_cover.h): expected values are hand-calculated from geometry, without running the implementation under test.
// Positive cases: fullscreen big triangle (-1,-1)(3,-1)(-1,3), fullscreen quad strip; negative cases: viewport covers only part of the attachment, scissor covers only part, triangle missing a corner,
// back-face culled, z out of range, w ≠ 1, two triangles each covering half (no single triangle covers all four corners, so by the rule it does not hold - conservative).
#include "../nvn/nvn_cover.h"
#include <stdio.h>

static int fail, total;
static void t(const char *name, int want, int got) {
    total++; fail += want != got;
    printf("%-44s 期望 %s → 真结果 %s %s\n", name, want ? "成立" : "不成立", got ? "成立" : "不成立", want == got ? "✓" : "✗");
}
int main(void) {
    const float big[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};        // 逆时针（NDC y 向上） | counterclockwise (NDC y up)
    const float big_cw[3][4] = {{-1, -1, 0, 1}, {-1, 3, 0, 1}, {3, -1, 0, 1}};     // 同一三角形，顺时针 | same triangle, clockwise
    const float quad[4][4] = {{-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}, {-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}};   // 条带：两三角形各半屏 | strip: two triangles each covering half the screen
    const float small[3][4] = {{-1, -1, 0, 1}, {1.9f, -1, 0, 1}, {-1, 1.9f, 0, 1}}; // 斜边 x+y=0.9 切掉 (1,1) 角 | hypotenuse x+y=0.9 cuts off the (1,1) corner
    const float zout[3][4] = {{-1, -1, 1.5f, 1}, {3, -1, 1.5f, 1}, {-1, 3, 1.5f, 1}};
    const float w2[3][4] = {{-2, -2, 0, 2}, {6, -2, 0, 2}, {-2, 6, 0, 2}};
    const float vp_full[4] = {0, 0, 1600, 900}, vp_half[4] = {0, 0, 800, 900}, vp_off[4] = {0, 450, 1600, 900};
    const int sc_full[4] = {0, 0, 1600, 900}, sc_part[4] = {0, 0, 1600, 450};
    t("全屏三角形，视口 = 整块，不剔除", 1, nvn_cover(big, 3, 4, 0, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("全屏三角形，无视口设置（缺省整块）", 1, nvn_cover(big, 3, 4, 0, 1, 0, NULL, 0, NULL, 1600, 900));
    t("全屏三角形，剔背面，逆时针为正面", 1, nvn_cover(big, 3, 4, 2, 1, 1, vp_full, 1, sc_full, 1600, 900));
    t("负：同一三角形顺时针，剔背面（被剔）", 0, nvn_cover(big_cw, 3, 4, 2, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("负：视口只占附件左半", 0, nvn_cover(big, 3, 4, 0, 1, 1, vp_half, 0, NULL, 1600, 900));
    t("负：视口下移（不从 0 起）", 0, nvn_cover(big, 3, 4, 0, 1, 1, vp_off, 0, NULL, 1600, 900));
    t("负：视口整块 1600x900，附件是 1920x1080", 0, nvn_cover(big, 3, 4, 0, 1, 1, vp_full, 0, NULL, 1920, 1080));
    t("负：剪裁只占上半", 0, nvn_cover(big, 3, 4, 0, 1, 1, vp_full, 1, sc_part, 1600, 900));
    t("负：三角形缺 (1,1) 角", 0, nvn_cover(small, 3, 4, 0, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("负：z 超出 [0,1]", 0, nvn_cover(zout, 3, 4, 0, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("负：w ≠ 1", 0, nvn_cover(w2, 3, 4, 0, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("负：全屏四边形条带（单个三角形包不住，保守）", 0, nvn_cover(quad, 4, 5, 0, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("负：正背面都剔", 0, nvn_cover(big, 3, 4, 3, 1, 1, vp_full, 0, NULL, 1600, 900));
    t("负：图元是线（非三角形）", 0, nvn_cover(big, 3, 1, 0, 1, 1, vp_full, 0, NULL, 1600, 900));
    printf("== %d 项，失败 %d ==\n", total, fail);
    return fail != 0;
}
