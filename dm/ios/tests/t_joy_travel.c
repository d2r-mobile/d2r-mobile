/* 移动摇杆：输入行程不跟大屏视觉放大走；iPad 屏幕底边归系统的那一条不起摇杆。期望值按公式手算：幅度 = (位移 − 6) / (60 − 6)，满幅 32767。 | Movement stick: the input travel does not follow the visual scale-up on large screens; the strip at the iPad screen bottom edge that belongs to the system does not start the stick. Expected values are hand-computed from the formula: magnitude = (displacement − 6) / (60 − 6), full scale 32767. */
#include "dm_touch_controls.h"
#include "../../host/dm_input.h"
#include <stdio.h>
#include <stdlib.h>
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); exit(1); } } while (0)
static int push(dm_tc_state *s, float w, float h, float d) {   /* 在底座圆心起手（底座固定），向右推 d 点，返回横轴输出 | start on the base center (the base is fixed), push d points to the right, return the horizontal axis output */
    dm_tc_init(s, w, h); float cx = s->layout.joy_x, cy = s->layout.joy_y; dm_tc_begin(s, 0, cx, cy); CHECK(s->joy_slot == 0);
    dm_tc_move(s, 0, cx + d, cy); return s->lx;
}
int main(void) {
    dm_tc_state s; dm_tc_set_tall_scale(1);   /* 前端的口径：比手机高的画面控件按 高 / 440 放大（1366×768 → 1.745 倍） | The front end's convention: on displays taller than a phone, widgets are scaled up by height / 440 (1366×768 → 1.745×) */
    static const float size[][2] = {{956, 440}, {1194, 672}, {1366, 768}};   /* 手机、11 寸 iPad 的 16:9 视图、12.9 寸 iPad 的 16:9 视图 | Phone, the 16:9 view on the 11-inch iPad, the 16:9 view on the 12.9-inch iPad */
    for (int i = 0; i < 3; ++i) {
        float w = size[i][0], h = size[i][1];
        CHECK(push(&s, w, h, 6) == 0);                                   /* 死区 6 点 | Dead zone 6 points */
        CHECK(abs(push(&s, w, h, 20) - 8495) <= 1);                      /* 14 / 54 × 32767 = 8495.1 */
        CHECK(abs(push(&s, w, h, 33) - 16384) <= 1);                     /* 27 / 54 = 一半 | 27 / 54 = half */
        CHECK(push(&s, w, h, 60) == DM_STICK_MAX);                       /* 60 点满偏 | 60 points for full deflection */
        CHECK(push(&s, w, h, 105) == DM_STICK_MAX);
        CHECK(abs(push(&s, w, h, -20) + 8495) <= 1);
    }
    dm_tc_init(&s, 1366, 768); CHECK(s.layout.joy_radius > 104 && s.layout.joy_radius < 105);   /* 画出来的底盘仍按视觉倍数：60 × 1.745 | The drawn base still follows the visual multiplier: 60 × 1.745 */

    /* 底边那一条：手机（没设）照旧能在最底起摇杆 | Bottom-edge strip: a phone (not set) can still start the stick at the very bottom as before */
    dm_tc_init(&s, 956, 440); dm_tc_begin(&s, 0, 90, 425); CHECK(s.joy_slot == 0); dm_tc_cancel_all(&s);   /* 圆心 90,340、可起手半径 90：(90,425) 在内 | center 90,340, grab radius 90: (90,425) is inside */
    /* iPad：底部 20 点里起手不当摇杆、松手不当点地 | iPad: a touch starting in the bottom 20 points is not treated as the stick, and its release is not treated as a ground tap */
    dm_tc_set_edge_bottom(20); dm_tc_init(&s, 1366, 768);
    dm_tc_begin(&s, 0, 100, 760); CHECK(s.joy_slot == DM_TC_NONE && s.finger[0].target == DM_TC_EDGE);
    dm_tc_move(&s, 0, 100, 700); CHECK(s.lx == 0 && s.ly == 0 && s.joy_slot == DM_TC_NONE);   /* 划上去也不变成摇杆 | Swiping upward does not turn into the stick either */
    dm_tc_end(&s, 0); CHECK(s.world_tap_seq == 0 && s.buttons == 0);
    dm_tc_begin(&s, 0, 100, 762); dm_tc_end(&s, 0); CHECK(s.world_tap_seq == 0);               /* 原地点一下也不当点地 | A tap in place is not treated as a ground tap either */
    dm_tc_begin(&s, 0, 150, 745); CHECK(s.joy_slot == 0);                                      /* 这一条的上沿（768 − 20）不算在内 | The top edge of this strip (768 − 20) is not included */
    dm_tc_move(&s, 0, 160, 766); CHECK(s.joy_slot == 0 && s.lx > 0 && s.ly < 0);               /* 已起手的摇杆拖进这一条照常持有 | A stick already started is still held as usual when dragged into this strip */
    dm_tc_begin(&s, 1, 200, 765); CHECK(s.joy_slot == 0 && s.finger[1].target == DM_TC_EDGE);  /* 另一根手指落在这一条：不抢摇杆 | Another finger lands in this strip: it does not grab the stick */
    dm_tc_end(&s, 1); CHECK(s.joy_slot == 0 && s.lx > 0);
    dm_tc_end(&s, 0); CHECK(s.lx == 0 && s.ly == 0 && s.joy_slot == DM_TC_NONE);
    dm_tc_begin(&s, 0, 800, 760); CHECK(s.finger[0].target != DM_TC_EDGE); dm_tc_cancel_all(&s);   /* 右半屏不受影响 | The right half of the screen is unaffected */
    dm_tc_set_edge_bottom(0);
    printf("JOY_TRAVEL OK %d 项\n", checks); return 0;
}
