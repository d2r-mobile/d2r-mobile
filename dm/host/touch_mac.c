#include "touch_mac.h"
#include "dm_input.h"
#include "dm_cursor.h"
#include "dm_actions.h"
#include "dm_hud.h"
#include "../ios/Sources/dm_touch_controls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t now_ns(void);
uint64_t guest_ns(void);
uint32_t dm_cur_tick(void);
int dm_loadscreen_down(void);
static uint32_t touch_cur_tick(void) { return dm_cur_tick(); }

typedef struct { uint64_t beat; int op, slot; float x, y; } touch_event;
enum { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };
enum { START_BEATS = 60, LATE_LIMIT_BEATS = 125, LATE_LIMIT_FAST_BEATS = 2000, TICKS_PER_SECOND = 25 };   // 快进（DM_DETCLOCK）下拍跑在加载前面，读取画面几百拍才撤，时限另给   // 读取画面晚撤可顺延，ToGame 后 125 拍（5 s 游戏时间；负载下最晚实测第 67 拍撤，用例共跑 240 拍）仍未撤报红 | in fast-forward (DM_DETCLOCK) ticks run ahead of loading and the loading screen takes hundreds of ticks to come down, so a separate limit is given   // a late loading screen may postpone the start; still up 125 ticks after ToGame (5 s game time; latest measured under load is tick 67, the case runs 240 ticks in total) is reported as a failure

static struct {
    int ready, enabled, prefix, started, based, drop, next, count, loaded, tick_set, late_reported, sample_set;
    int ui_drag_on;   // 这根手指的界面连续手势已交给宿主（dm_ui_drag_*） | this finger's continuous UI gesture has been handed to the host (dm_ui_drag_*)
    uint32_t to_game_tick, start_tick, sample_tick;
    uint64_t pulse_end_beat, pulse_epoch, pulse_resume_beat, pulse_hold_beat; int pulse_hold_left;
    uint64_t beat;
    touch_event event[256];
    dm_tc_state state;
} g_touch;

// ---- 真鼠标（Mac App，front_mac.m）：事件来自前端而不是脚本表，进同一套状态机。界面用原版手柄 HUD，屏上没有触屏圆钮与摇杆：
// 非面板态一个按钮目标都不开、左下角不起摇杆（点哪里都只是「点世界」：点到目标走过去交互，点空地不动）；面板态照旧（点控件、页签、物品格）。
// 前端页（主菜单 / 选角 / 建角，不在局内）不经状态机，触点直接当界面点击（同 iOS 前端的 frontendMode）。
// ---- Real mouse (Mac App, front_mac.m): events come from the front end rather than the script table and enter the same state machine. The UI is the original gamepad HUD, with no touch buttons or stick on screen:
// outside panels no button target is opened and no stick is raised at the bottom left (a click anywhere is just "click the world": clicking a target walks over and interacts, clicking empty ground does nothing); in panels it works as before (clicking controls, tabs, item cells).
// Front-end pages (main menu / character select / character creation, not in a game) bypass the state machine and the touch point is taken directly as a UI click (same as frontendMode in the iOS front end).
#include <stdatomic.h>
static int g_live_on; static uint32_t g_live_tick0; static int g_live_based;
static struct { _Atomic unsigned head, tail; struct { int op; float nx, ny; } q[64]; } g_live;
void dm_touch_host_live(int op, float nx, float ny) {   // 前端线程：op 0 按下 1 移动 2 抬起；坐标 = 游戏画面归一化 [0,1] | Front-end thread: op 0 down, 1 move, 2 up; coordinates = normalized to the game picture [0,1]
    if (!g_live_on) return; unsigned t = atomic_load(&g_live.tail); if (t - atomic_load(&g_live.head) >= 64) return;
    g_live.q[t % 64].op = op; g_live.q[t % 64].nx = nx; g_live.q[t % 64].ny = ny; atomic_store(&g_live.tail, t + 1);
}
static void touch_uimsg(const char *cat, const char *act, const char *arg) {
    static dm_tc_scene scene;
    if (!strcmp(cat, "DM") && !strcmp(act, "ReplayDone") && g_touch.prefix)
        scene.game = 1;
    dm_tc_set_panel_state(dm_ui_panel_state());
    int navigation = dm_tc_navigation(&scene, cat, act, arg);
    if (g_touch.enabled) {
        dm_tc_set_navigation_mode(&g_touch.state, navigation);
        static int pad = -1; if (pad < 0) { const char *e = getenv("DM_TOUCH_NAV_PAD"); pad = e && e[0] == '1'; }   // 缺省不给圆钮（同 front_ios.m） | no round button by default (same as front_ios.m)
        uint32_t mask = navigation ? (dm_tc_nav_mask(pad && !dm_tc_scene_dead()) | (dm_tc_scene_dialog_only(&scene) ? 1u << DM_TC_MENU : 0)) : g_live_on ? 0 : ((1u << DM_TC_COUNT) - 1);
        dm_tc_enable_targets(&g_touch.state, mask);
    }
    if (!strcmp(cat, "DM") && !strcmp(act, "ReplayDone") && g_touch.prefix && !g_touch.started) {
        g_touch.started = 1;
        g_touch.based = 0;
        g_touch.tick_set = 0;
        g_touch.beat = 0;
        fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：DM:ReplayDone，等读取画面撤下后起局\n");
        // 前缀局是快进的：拍跑得比客户端加载快时，读取画面还没撤就把本局的拍数预算跑完、脚本一条都没发（每拍钩子变快后 skill_main 必现）。
        // 读取画面还开着就冻结客体时钟到它撤下（与按键脚本、按拍截图同一机制）；本来就先撤时不触发
        // Prefix runs are fast-forwarded: when ticks run faster than the client loads, the run's tick budget is used up before the loading screen is down and the script sends nothing (always reproduces with skill_main after the per-tick hook got faster).
        // While the loading screen is still up, freeze the guest clock until it is taken down (same mechanism as the key script and per-tick screenshots); it does not trigger when it is taken down first anyway
        { int dm_loadscreen_up(void); void dm_hold_for_loadscreen(void); if (dm_loadscreen_up()) dm_hold_for_loadscreen(); }
    }
    // 起算点 = ToGame 后满 START_BEATS 个服务端拍、且读取画面已撤之后的第一个拍（按状态，不写死拍号）。
    // 产品节奏下拍在加载期间照走，负载重时读取画面会晚于第 60 拍撤（accept ⑨ inv_tap 第 67 拍撤、写死拍号报红）；顺延只记一行，断言按状态判
    // Starting point = the first tick after START_BEATS server ticks have passed since ToGame and the loading screen is down (by state, no hard-coded tick number).
    // At product pacing ticks keep running during loading, and under heavy load the loading screen comes down later than tick 60 (accept (9) inv_tap: down at tick 67, the hard-coded tick number failed); a postponement is only logged as one line, and assertions judge by state
    if (!strcmp(cat, "FrontEndNavigation") && !strcmp(act, "ToGame") && !g_touch.prefix && !g_touch.started) {
        g_touch.started = 1; g_touch.based = 0; g_touch.tick_set = 0; g_touch.loaded = 0; g_touch.late_reported = 0;
        fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：ToGame，第 %d 拍起局\n", START_BEATS);
    }
    if (!strcmp(cat, "PanelManager") && (!strcmp(act, "UnloadPanel") || !strcmp(act, "ClosePanel")) && !strcmp(arg, "LoadScreenPanel") && !g_touch.loaded) {
        g_touch.loaded = 1;
        if (g_touch.started && g_touch.tick_set) {
            uint32_t elapsed = touch_cur_tick() - g_touch.to_game_tick;
            fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：读取画面撤下于 ToGame 后第 %u 拍\n", elapsed);
        } else fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：读取画面撤下（等待首个服务端拍号）\n");
    }
}

static int event_cmp(const void *a, const void *b) {
    const touch_event *ea = a, *eb = b;
    return ea->beat > eb->beat ? 1 : ea->beat < eb->beat ? -1 : 0;
}

static void touch_init(uint64_t now) {
    (void)now;
    g_touch.ready = 1;
    const char *path = getenv("DM_TOUCH_SCRIPT");
    if (!path || !*path) return;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[dm] DM_TOUCH_SCRIPT 打不开 %s\n", path); return; }
    char line[256];
    while (g_touch.count < 256 && fgets(line, sizeof line, f)) {
        double seconds; char op[8]; int slot; float x, y;
        int fields = sscanf(line, "%lf %7s %d %f,%f", &seconds, op, &slot, &x, &y);
        if (line[0] == '#' || fields < 3) continue;
        touch_event *e = &g_touch.event[g_touch.count];
        e->beat = seconds <= 0 ? 0 : (uint64_t)(seconds * TICKS_PER_SECOND);
        e->slot = slot;
        if (!strcmp(op, "down")) e->op = TOUCH_DOWN;
        else if (!strcmp(op, "move")) e->op = TOUCH_MOVE;
        else if (!strcmp(op, "up")) e->op = TOUCH_UP;
        else continue;
        if (e->op != TOUCH_UP && fields != 5) { fprintf(stderr, "[dm] DM_TOUCH_SCRIPT 忽略坏行：%s", line); continue; }
        if (e->op != TOUCH_UP) { e->x = x; e->y = y; }
        g_touch.count++;
    }
    fclose(f);
    qsort(g_touch.event, g_touch.count, sizeof g_touch.event[0], event_cmp);
    dm_tc_init(&g_touch.state, 2868, 1320); g_touch.state.mouse_click_mode = 1;
    g_touch.enabled = 1;
    g_touch.prefix = getenv("DM_TOUCH_PREFIX") && *getenv("DM_TOUCH_PREFIX");
    g_touch.drop = getenv("DM_TOUCH_TEST_DROP") && *getenv("DM_TOUCH_TEST_DROP");
    void uimsg_subscribe(void (*fn)(const char *, const char *, const char *));
    uimsg_subscribe(touch_uimsg);
    fprintf(stderr, "[dm] DM_TOUCH_SCRIPT ← %s：%d 条，按服务端游戏拍推进（脚本 25 拍/秒）%s\n", path, g_touch.count, g_touch.drop ? "（测试丢弃输出）" : "");
}

#include <pthread.h>
#include <unistd.h>
static void *live_test_thread(void *arg) {
    FILE *f = fopen((const char *)arg, "r"); if (!f) { fprintf(stderr, "[dm] 【红】DM_TEST_LIVE 打不开 %s\n", (const char *)arg); return NULL; }
    char line[160]; double t0 = (double)now_ns() / 1e9; int n = 0;
    while (fgets(line, sizeof line, f)) { double sec; char op[8]; float x = 0, y = 0; if (line[0] == '#' || sscanf(line, "%lf %7s %f,%f", &sec, op, &x, &y) < 2) continue;
        double wait = sec - ((double)now_ns() / 1e9 - t0); if (wait > 0) usleep((useconds_t)(wait * 1e6));
        dm_touch_host_live(!strcmp(op, "down") ? 0 : !strcmp(op, "move") ? 1 : 2, x / 2868, y / 1320); n++; }
    fclose(f); fprintf(stderr, "[dm] DM_TEST_LIVE：%d 条发完\n", n); return NULL;
}
void dm_touch_host_step(uint64_t now, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
void dm_touch_host_live_enable(void) {   // 前端开窗后、客体线程起来之前调一次 | Called once after the front end opens the window and before the guest threads start
    if (g_live_on) return;
    if (!g_touch.ready) touch_init(0);   // 同时带了触点脚本（测试）就照常装载 | If a touch script (test) is given as well, it is loaded as usual
    if (!g_touch.enabled) { dm_tc_init(&g_touch.state, 2868, 1320); g_touch.state.mouse_click_mode = 1; g_touch.enabled = 1;
        void uimsg_subscribe(void (*fn)(const char *, const char *, const char *)); uimsg_subscribe(touch_uimsg); }
    dm_tc_set_joy_block(&g_touch.state, 1); dm_tc_enable_targets(&g_touch.state, 0);
    g_live_on = 1; dm_pad_hook = dm_touch_host_step; fprintf(stderr, "[dm] 鼠标：接到触屏状态机（点目标 / 点界面；没有触屏圆钮与摇杆）\n");
    // 测试开关 DM_TEST_LIVE=<文件>（没人操作窗口时验这条路）：每行「秒 down|move|up x,y」，坐标按 2868×1320，秒 = 从这里起的墙钟；到点调上面的 dm_touch_host_live，
    // 与真鼠标只差「系统鼠标事件 → 归一化坐标」那三行
    // Test switch DM_TEST_LIVE=<file> (to verify this path when nobody operates the window): each line is "seconds down|move|up x,y", coordinates on a 2868×1320 basis, seconds = wall clock from here; at that time the dm_touch_host_live above is called,
    // differing from a real mouse only by the three lines "system mouse event -> normalized coordinates"
    if (getenv("DM_TEST_LIVE")) { pthread_t th; pthread_create(&th, NULL, live_test_thread, strdup(getenv("DM_TEST_LIVE"))); pthread_detach(th); }
}
int dm_touch_host_enabled(void) {
    return getenv("DM_TOUCH_SCRIPT") && *getenv("DM_TOUCH_SCRIPT");
}

void dm_touch_host_game_start(uint64_t now) {
    if (!g_touch.ready) touch_init(now);
    if (g_touch.enabled && !g_touch.prefix) { g_touch.started = 1; g_touch.based = 1; g_touch.loaded = 1; g_touch.tick_set = 1; g_touch.to_game_tick = touch_cur_tick() - START_BEATS; g_touch.start_tick = touch_cur_tick(); }
}

void dm_touch_host_step(uint64_t now, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    if (!g_touch.ready) touch_init(now);
    if (g_live_on) {   // 真鼠标：前端页直通界面点击；局内并进事件表（拍 = 此刻），与脚本事件同一段处理 | Real mouse: on front-end pages it goes straight to a UI click; in game it joins the event table (tick = now) and is handled in the same pass as script events
        int dm_uiscreen_ingame(void); int in = dm_uiscreen_ingame(); static float px, py; unsigned h;
        if (in && !g_live_based) { g_live_based = 1; g_live_tick0 = touch_cur_tick(); if (!g_touch.based) { g_touch.started = 1; g_touch.based = 1; g_touch.loaded = 1; g_touch.tick_set = 1; g_touch.start_tick = g_live_tick0; g_touch.to_game_tick = g_live_tick0; } }
        if (!in) g_live_based = 0;
        while ((h = atomic_load(&g_live.head)) != atomic_load(&g_live.tail)) {
            int op = g_live.q[h % 64].op; float nx = g_live.q[h % 64].nx, ny = g_live.q[h % 64].ny; if (op != 2) { px = nx; py = ny; }
            if (!in) { dm_ui_touch(op, px, py); atomic_store(&g_live.head, h + 1); continue; }
            if (g_touch.next == g_touch.count) g_touch.next = g_touch.count = 0;   // 表里的都处理完了：从头复用 | Everything in the table has been handled: reuse from the start
            if (g_touch.count >= 256) break;
            touch_event *e = &g_touch.event[g_touch.count++]; e->beat = 0; e->slot = 0; e->op = op == 0 ? TOUCH_DOWN : op == 1 ? TOUCH_MOVE : TOUCH_UP; e->x = px * 2868; e->y = py * 1320;
            atomic_store(&g_live.head, h + 1);
        }
    }
    if (!g_touch.enabled || !g_touch.started) return;
    uint32_t tick = touch_cur_tick();
    if (!g_touch.prefix && !g_touch.tick_set) { g_touch.to_game_tick = tick; g_touch.tick_set = 1; }
    if (!g_touch.based) {
        uint32_t elapsed = tick - g_touch.to_game_tick;
        if (g_touch.prefix) {   // 前缀局：回放结束（ReplayDone）且读取画面已撤才起局——快进（DM_DETCLOCK）时服务端拍跑在客户端加载前面，回放末拍时读取画面还在，按键不生效 | prefix run: start the run only when the replay has ended (ReplayDone) and the loading screen is down -- in fast-forward (DM_DETCLOCK) server ticks run ahead of client loading, the loading screen is still up at the replay's last tick, and key presses have no effect
            if (!dm_loadscreen_down()) return;
            g_touch.start_tick = tick;
            g_touch.tick_set = 1;
            g_touch.based = 1;
            g_touch.beat = 0;
            fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：前缀回放结束且读取画面已撤，第 %u 拍起局\n", tick);
        }
        if (g_touch.based) goto apply_input;
        if (elapsed < START_BEATS) return;
        if (!dm_loadscreen_down()) {   // 与截图前置同一判定（diag.c），不再自己跟 UI 消息 | same check as the screenshot precondition (diag.c), no longer tracking UI messages itself
            // 快进的拍比客户端加载快时，读取画面还没撤拍数预算就跑完、脚本一条都没发（每拍钩子变快后 move 6 局 6 红「没有移动包」；
            // 前缀局同日已这样修）。到点读取画面还开着就冻结客体时钟到它撤下；本来就先撤时不触发
            // When fast-forward ticks run faster than the client loads, the tick budget runs out before the loading screen is taken down and the script sends nothing (after the per-tick hook got faster, move was red in 6 of 6 runs with 「没有移动包」(no move packet);
            // the prefix runs were fixed this way the same day). If the loading screen is still up when the time comes, freeze the guest clock until it is taken down; it does not trigger when it is taken down first anyway
            { int dm_loadscreen_up(void); void dm_hold_for_loadscreen(void); if (dm_loadscreen_up()) dm_hold_for_loadscreen(); }
            if (!g_touch.late_reported) { fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：第 %d 拍读取画面未撤，顺延到撤下后的第一个拍起局\n", START_BEATS); g_touch.late_reported = 1; }
            if (elapsed >= (getenv("DM_DETCLOCK") ? LATE_LIMIT_FAST_BEATS : LATE_LIMIT_BEATS) && g_touch.late_reported == 1) { fprintf(stderr, "[dm] 【红】DM_TOUCH_SCRIPT：读取画面未撤：ToGame 后 %d 拍仍未撤，不起局\n", LATE_LIMIT_BEATS); g_touch.late_reported = 2; }
            return;
        }
        g_touch.start_tick = tick;
        fprintf(stderr, "[dm] DM_TOUCH_SCRIPT：起局于 ToGame 后第 %u 拍\n", elapsed);
        g_touch.beat = 0;
        g_touch.based = 1;
    }
apply_input:
    g_touch.beat = tick - g_touch.start_tick;
    if (g_touch.sample_set && g_touch.sample_tick == tick) {
        if (g_touch.drop) return;
        *buttons |= g_touch.state.buttons;
        if (g_touch.state.lx) *lx = g_touch.state.lx;
        if (g_touch.state.ly) *ly = g_touch.state.ly;
        if (g_touch.state.rx) *rx = g_touch.state.rx;
        if (g_touch.state.ry) *ry = g_touch.state.ry;
        return;
    }
    while (g_touch.next < g_touch.count && g_touch.event[g_touch.next].beat <= g_touch.beat) {
        touch_event *e = &g_touch.event[g_touch.next++];
        if (e->slot < 0 || e->slot >= DM_TC_MAX_TOUCH) continue;
        if (e->op == TOUCH_DOWN) { dm_tc_begin(&g_touch.state, e->slot, e->x, e->y); if (g_touch.state.navigation_mode && e->slot == g_touch.state.pointer_slot) dm_tc_pointer_item(&g_touch.state, dm_ui_item_at_now(e->x / 2868, e->y / 1320)); }
        else if (e->op == TOUCH_MOVE) { dm_tc_move(&g_touch.state, e->slot, e->x, e->y);
            // 界面连续手势（同 iOS 前端 front_ios.m）：手势层定型后把起手点与当前点交给宿主；起拖时报一次物品图的抬起量（同上面拖放落点的 DM_TC_POINTER_LIFT_PT / 1320）
            // Continuous UI gestures (same as the iOS front end front_ios.m): once the gesture layer has settled the gesture, hand the start point and current point to the host; when a drag starts report the lift amount of the item image once (same as DM_TC_POINTER_LIFT_PT / 1320 of the drag-and-drop landing point above)
            dm_tc_finger *f = &g_touch.state.finger[e->slot];
            if (g_touch.state.navigation_mode && e->slot == g_touch.state.pointer_slot && f->ui_drag) { if (!g_touch.ui_drag_on) { g_touch.ui_drag_on = 1; dm_ui_drag_lift(DM_TC_POINTER_LIFT_PT / 1320); dm_ui_drag_begin(f->ui_drag, f->aim_origin_x / 2868, f->aim_origin_y / 1320, 44.0f / 1320); }
                dm_ui_drag_move(e->x / 2868, e->y / 1320); } }
        else {
            if (g_touch.ui_drag_on && e->slot == g_touch.state.pointer_slot) { g_touch.ui_drag_on = 0; dm_ui_drag_end(); }   // 松手前告诉手势层这根手指是否落在物品区（这里就在游戏主线程，直接问） | before release tell the gesture layer whether this finger landed on an item area (this is already the game main thread, so ask directly)
            if (g_touch.state.navigation_mode && e->slot == g_touch.state.pointer_slot) { dm_tc_finger *f = &g_touch.state.finger[e->slot];
                if (f->shop_drag) { if (dm_ui_drop_at_now(f->x / 2868, (f->y - DM_TC_POINTER_LIFT_PT) / 1320, f->y / 1320) == 5) { dm_ui_shop_buy_at(f->aim_origin_x / 2868, f->aim_origin_y / 1320); g_touch.state.pointer_x = f->aim_origin_x; g_touch.state.pointer_y = f->aim_origin_y; } }   // 从货架起拖、落在背包类物品区 = 买（光标先放回那件货上） | drag started from the shelf and dropped on an inventory-type item area = buy (the cursor is first put back on that piece of merchandise)
                else if (f->pointer_dragged) { int dk = dm_ui_drop_at_now(f->x / 2868, (f->y - DM_TC_POINTER_LIFT_PT) / 1320, f->y / 1320); float dx, dy;
                    if ((dk == 5 && dm_ui_drop_point(&dx, &dy)) || (dk == 0 && dm_ui_origin_point(&dx, &dy))) dm_tc_pointer_release_at(&g_touch.state, dx * 2868, dy * 1320);
                    else { if (dk == 7) { dm_shop_sell_cursor(); dk = 0; } dm_tc_pointer_release(&g_touch.state, dk); } } }   // 7 = 松在商人货架上：卖；随后那下「放回原处」只在没卖掉时起作用 | 7 = released on the vendor's shelf: sell; the following "put back in place" only takes effect if it was not sold
            dm_tc_set_tap_box(dm_ui_tap_box()); dm_tc_set_cur_stack(dm_ui_cur_stack_id()); dm_tc_set_hover_unid(dm_ui_hover_unid());
            { dm_hud_info hi; dm_hud_get(&hi); dm_tc_set_holding(&g_touch.state, hi.valid && hi.holding); }
            unsigned wt = g_touch.state.world_tap_seq;
            unsigned mc = g_touch.state.mouse_click_seq; unsigned tt=g_touch.state.tab_tap_seq; unsigned us = g_touch.state.use_seq; unsigned ids = g_touch.state.ident_seq; unsigned lps = g_touch.state.long_press_seq; unsigned its = g_touch.state.item_tap_seq;
            dm_tc_end(&g_touch.state, e->slot);
            if (g_touch.state.tab_tap_seq != tt) dm_ui_tab_go();
            if (g_touch.state.long_press_seq != lps) dm_ui_long_press();
            if (g_touch.state.item_tap_seq != its) dm_ui_item_tap();
            if (g_touch.state.ident_seq != ids) dm_ui_identify_hovered();
            if (g_touch.state.use_seq != us) dm_ui_use_item();
            if (g_touch.state.world_tap_seq != wt) { unsigned dm_world_tap(float, float); dm_world_tap(g_touch.state.world_tap_x / 2868, g_touch.state.world_tap_y / 1320); }
            if (g_touch.state.mouse_click_seq != mc) { float nx = g_touch.state.mouse_click_x / 2868, ny = g_touch.state.mouse_click_y / 1320; dm_ui_touch(0, nx, ny); dm_ui_touch(2, nx, ny); }
        }
    }
    if (g_touch.state.diff_request) { dm_hud_request_players(g_touch.state.diff_request); g_touch.state.diff_request = 0; }   // 难度选择条选中档位 → 下一拍走原版路径 | a level is selected on the difficulty selector bar -> take the original game's path on the next tick
    dm_tc_tick(&g_touch.state, (double)g_touch.beat / 30.0);
    if (g_touch.state.pulse_button && !g_touch.pulse_end_beat) {
        g_touch.pulse_end_beat = g_touch.beat + 4;
        g_touch.pulse_epoch = g_touch.state.pulse_epoch; g_touch.pulse_hold_left = (int)(g_touch.state.pulse_hold_s * 30 + .999);
    }
    // 脉冲按「游戏已取走」收（与 iOS 前端 pulseTick 同一规则）：满 4 次采样、且扣着的 A 已送达（上限再等 90 次）；排着的下一个脉冲隔 2 次采样（让游戏看到一次松开）再开始
    // 「按住」脉冲（双击 = 按住 A / Y）：按键真正送达（不再扣着）之后再按满时长才收
    // A pulse ends on "the game has taken it" (same rule as pulseTick in the iOS front end): 4 full samples, and the held-back A has been delivered (wait at most 90 more samples); the next queued pulse starts 2 samples later (so the game sees one release)
    // "Hold" pulses (double tap = hold A / Y): end only after the button has really been delivered (no longer held back) and then held for the full duration
    if (g_touch.pulse_end_beat && g_touch.pulse_hold_left > 0 && !dm_cursor_pending() && g_touch.beat != g_touch.pulse_hold_beat) { g_touch.pulse_hold_beat = g_touch.beat; g_touch.pulse_hold_left--; }
    if (g_touch.pulse_end_beat && g_touch.beat >= g_touch.pulse_end_beat && ((!dm_cursor_pending() && g_touch.pulse_hold_left <= 0) || g_touch.beat >= g_touch.pulse_end_beat + 90)) {
        dm_tc_pulse_end(&g_touch.state, g_touch.pulse_epoch);
        g_touch.pulse_end_beat = 0; g_touch.pulse_resume_beat = dm_tc_pulse_pending(&g_touch.state) ? g_touch.beat + 2 : 0;
    }
    if (g_touch.pulse_resume_beat && g_touch.beat >= g_touch.pulse_resume_beat) { dm_tc_pulse_resume(&g_touch.state); g_touch.pulse_resume_beat = 0; }
    if (g_touch.state.pointer_valid)
        dm_cursor_set_screen(g_touch.state.pointer_x, g_touch.state.pointer_y, 2868, 1320);
    else dm_cursor_clear();
    g_touch.sample_tick = tick;
    g_touch.sample_set = 1;
    if (g_touch.drop) return;
    *buttons |= g_touch.state.buttons;
    if (g_touch.state.lx) *lx = g_touch.state.lx;
    if (g_touch.state.ly) *ly = g_touch.state.ly;
    if (g_touch.state.rx) *rx = g_touch.state.rx;
    if (g_touch.state.ry) *ry = g_touch.state.ry;
}

__attribute__((constructor)) static void touch_mac_register(void) { if (dm_touch_host_enabled()) dm_pad_hook = dm_touch_host_step; }
