// #35 real item-route bind confirmation fixture: DOWN -> async query -> held
// UP -> answer/tick -> original core finish. It uses dm_live_touch_items and
// dm_live_touch, not the direct live_touch UP path.
#include "live_touch.h"
#include "live_touch_items.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct { int state, hit, calls; float nx, ny; } tap_fixture;
static int tap_hook(void *ctx, float *nx, float *ny) {
    tap_fixture *f = ctx;
    f->calls++;
    if (!f->hit) return 0;
    *nx = f->nx;
    *ny = f->ny;
    if (f->state == 2 && f->hit == 1) return 2;
    if ((f->state == 0 || f->state == 2) && (f->hit == 1 || f->hit == 2)) return 1;
    return 0;
}

typedef struct {
    unsigned seq;
    int item_answer;       // host class; -1 = no answer yet
    int drop_answer;
    int drop_point_ok;
    int origin_ok;
    int cursor_set, cursor_clear;
    float cursor_x, cursor_y;
} host_fixture;

static unsigned item_query(void *context, float nx, float ny) {
    (void)nx; (void)ny;
    host_fixture *f = context;
    return ++f->seq;
}
static int item_result(void *context, unsigned seq) {
    (void)seq;
    host_fixture *f = context;
    return f->item_answer;
}
static unsigned drop_query(void *context, float nx, float ny, float finger_ny) {
    (void)nx; (void)ny; (void)finger_ny;
    host_fixture *f = context;
    return ++f->seq;
}
static int drop_result(void *context, unsigned seq) { (void)seq; (void)context; return 5; }
static int drop_last(void *context) { (void)context; return 0; }
static int drop_point(void *context, float *nx, float *ny) {
    host_fixture *f = context;
    if (!f->drop_point_ok) return 0;
    *nx = 0.42f; *ny = 0.58f; return 1;
}
static int origin_point(void *context, float *nx, float *ny) {
    host_fixture *f = context;
    if (!f->origin_ok) return 0;
    *nx = 0.31f; *ny = 0.44f; return 1;
}
static void cursor_set(void *context, float x, float y, float w, float h) {
    host_fixture *f = context;
    f->cursor_set++;
    f->cursor_x = w > 0 ? x / w : 0;
    f->cursor_y = h > 0 ? y / h : 0;
}
static void cursor_clear(void *context) { host_fixture *f = context; f->cursor_clear++; }

static int event(dm_live_touch_items *items, int action, int32_t id,
                 float x, float y, int64_t at) {
    int32_t ids[1] = {id};
    float xs[1] = {x}, ys[1] = {y};
    return dm_live_touch_items_event(items, action, 0, 1, ids, xs, ys,
                                     667, 600, 0, 0, 667, 600, at);
}

static void release(dm_live_touch_items *items, int32_t id, float x, float y,
                    int64_t down, int64_t up, int64_t tick, int answer) {
    (void)answer;
    (void)event(items, DM_LIVE_ACTION_DOWN, id, x, y, down);
    (void)event(items, DM_LIVE_ACTION_UP, id, x, y, up);
    // The answer is not visible until the tick; this is the production async
    // held-release boundary.
    dm_live_touch_items_tick(items, tick);
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    float x = 333, y = 300;
    CHECK(dm_tc_hit(&layout, x, y) == DM_TC_NONE);

    dm_live_touch *touch = dm_live_touch_create(667, 600);
    CHECK(touch != NULL);
    dm_live_touch_set_context(touch, 1, dm_tc_nav_mask(1), 1, 1, 1, 0);
    tap_fixture tap = {2, 1, 0, 0.25f, 0.35f};
    dm_live_touch_set_tap_hook(touch, tap_hook, &tap);

    host_fixture host;
    memset(&host, 0, sizeof host);
    host.item_answer = 0; // known ordinary panel widget / skill-tree hit
    host.drop_answer = 5;
    host.drop_point_ok = 1;
    host.origin_ok = 1;
    dm_live_touch_items_host h = {&host, item_query, item_result, drop_query,
                                  drop_result, drop_last, drop_point, origin_point, 0};
    dm_live_touch_items *items = dm_live_touch_items_create(touch, &h);
    CHECK(items != NULL);
    CHECK(dm_live_touch_items_set_context(items, 1, 1, 1, 1) == 0);

    // State 2 + skill hit through the real async item route: class 0 is a
    // known widget and must reach the original tap_button branch.
    release(items, 1, x, y, 1000000000, 1001000000, 1002000000, 0);
    CHECK(tap.calls == 1);
    CHECK(dm_live_touch_buttons(touch) & DM_BTN_Y);
    CHECK(!(dm_live_touch_buttons(touch) & DM_BTN_A));
    float px, py, pw, ph;
    CHECK(dm_live_touch_pointer_position(touch, &px, &py, &pw, &ph));
    CHECK(px == 0.25f * 667 && py == 0.35f * 600);
    // Production order: publish the host cursor before pad Y.
    dm_live_touch_cursor_publish(touch, 1, cursor_set, cursor_clear, &host);
    CHECK(host.cursor_set == 1);
    CHECK(fabsf(host.cursor_x - 0.25f) < 0.001f && fabsf(host.cursor_y - 0.35f) < 0.001f);
    CHECK(dm_live_touch_end_released_pulse(touch));

    // Normal state 0 skill: known class 0 + hit1 must be move/select only.
    tap.state = 0; tap.hit = 1; tap.calls = 0;
    unsigned before = dm_live_touch_tap_move_seq(touch);
    release(items, 2, x, y, 1003000000, 1004000000, 1005000000, 0);
    CHECK(tap.calls == 1);
    CHECK(!(dm_live_touch_buttons(touch) & (DM_BTN_A | DM_BTN_Y)));
    CHECK(dm_live_touch_tap_move_seq(touch) == before + 1);

    // Normal state 0 stat-plus behaves the same.
    tap.hit = 2;
    before = dm_live_touch_tap_move_seq(touch);
    release(items, 3, x, y, 1006000000, 1007000000, 1008000000, 0);
    CHECK(!(dm_live_touch_buttons(touch) & (DM_BTN_A | DM_BTN_Y)));
    CHECK(dm_live_touch_tap_move_seq(touch) == before + 1);

    // Authoritative class 6 (blocked/covered) wins over a stale skill hit.
    tap.state = 2; tap.hit = 1; tap.calls = 0;
    host.item_answer = 6;
    release(items, 4, x, y, 1009000000, 1010000000, 1011000000, 6);
    CHECK(tap.calls == 1);
    CHECK(!(dm_live_touch_buttons(touch) & DM_BTN_Y));

    // Unsupported/unknown-timeout class is no-click, even with a stale hit.
    host.item_answer = -1;
    release(items, 5, x, y, 1012000000, 1013000000, 1313000000, -1);
    CHECK(tap.calls == 2);
    CHECK(!(dm_live_touch_buttons(touch) & DM_BTN_Y));

    // A tab/close classification keeps the original lawful route and no Y.
    host.item_answer = 2;
    tap.state = 2; tap.hit = 0; tap.calls = 0;
    release(items, 6, x, y, 1317000000, 1318000000, 1319000000, 2);
    CHECK(!(dm_live_touch_buttons(touch) & DM_BTN_Y));

    // Cancel/context discard the in-flight pointer and do not replay.
    host.item_answer = 0; tap.state = 2; tap.hit = 1; tap.calls = 0;
    (void)event(items, DM_LIVE_ACTION_DOWN, 7, x, y, 1320000000);
    dm_live_touch_items_cancel(items);
    CHECK(!(dm_live_touch_buttons(touch) & DM_BTN_Y));
    CHECK(tap.calls == 0);
    dm_live_touch_set_context(touch, 1, dm_tc_nav_mask(1), 2, 2, 2, 0);
    CHECK(dm_live_touch_items_set_context(items, 2, 2, 2, 1));
    CHECK(!(dm_live_touch_buttons(touch) & DM_BTN_Y));

    // Fresh route after the rejected context still produces one Y.
    dm_live_touch_set_context(touch, 1, dm_tc_nav_mask(1), 3, 3, 3, 0);
    CHECK(dm_live_touch_items_set_context(items, 3, 3, 3, 1));
    tap.calls = 0;
    release(items, 8, x, y, 1322000000, 1323000000, 1324000000, 0);
    CHECK(dm_live_touch_buttons(touch) & DM_BTN_Y);
    CHECK(tap.calls == 1);

    dm_live_touch_items_destroy(items);
    dm_live_touch_destroy(touch);
    puts("PASS: real async item route carries known class0 to original tap confirmation, preserves class6/timeout no-click, and has no A/Y point-spend regressions");
    return 0;
}
