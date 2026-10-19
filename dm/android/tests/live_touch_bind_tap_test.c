// #35 bindTapSync confirmation fixture: real navigation core pointer end with
// the front-end tap hook. It checks the original Y/move verdict, not a fake
// host identity or a synthetic bind.
#include "live_touch.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct { int state, hit, calls; float nx, ny; } bind_fixture;

static int tap_hook(void *ctx, float *nx, float *ny) {
    bind_fixture *f = ctx;
    f->calls++;
    if (!f->hit) return 0;
    *nx = f->nx;
    *ny = f->ny;
    if (f->state == 2 && f->hit == 1) return 2;
    if ((f->state == 0 || f->state == 2) && (f->hit == 1 || f->hit == 2)) return 1;
    return 0;
}

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    // A skill-tree tap is not a core layout target; use a panel point so the
    // navigation pointer slot (the original cursor owner) is active.
    float x = 333, y = 300;
    CHECK(dm_tc_hit(&layout, x, y) == DM_TC_NONE);

    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 1, dm_tc_nav_mask(1), 1, 1, 1, 0);
    bind_fixture f = {2, 1, 0, 0.25f, 0.35f};
    dm_live_touch_set_tap_hook(t, tap_hook, &f);

    // State 2 + skill icon: one original Y, cursor at the snapped point.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 1, x, y, 1000000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 1, x, y, 1001000000));
    CHECK(dm_live_touch_buttons(t) & DM_BTN_Y);
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_A));
    CHECK(f.calls == 1);
    CHECK(dm_live_touch_end_released_pulse(t)); // host releases the one Y pulse
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_Y));
    float px, py, pw, ph;
    CHECK(dm_live_touch_pointer_position(t, &px, &py, &pw, &ph));
    CHECK(px == 0.25f * 667 && py == 0.35f * 600);
    CHECK(pw == 667 && ph == 600);

    // Hit 2 (stat plus) in state 2: move only, never Y.
    f.calls = 0; f.hit = 2;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 2, x, y, 1002000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 2, x, y, 1003000000));
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_Y));
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_A));
    CHECK(dm_live_touch_tap_move_seq(t) > 0);

    // Unknown hit 0 in state 2: no Y and no guessed skill.
    f.calls = 0; f.hit = 0;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 3, x, y, 1004000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 3, x, y, 1005000000));
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_Y));
    CHECK(f.calls == 1);

    // Normal state 0 skill icon: tap_move/select only, no A or Y spend.
    f.calls = 0; f.state = 0; f.hit = 1;
    unsigned before = dm_live_touch_tap_move_seq(t);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 4, x, y, 1006000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 4, x, y, 1007000000));
    CHECK(!(dm_live_touch_buttons(t) & (DM_BTN_A | DM_BTN_Y)));
    CHECK(dm_live_touch_tap_move_seq(t) == before + 1);

    // Normal state 0 stat-plus behaves the same: move/select, no spend.
    f.hit = 2;
    before = dm_live_touch_tap_move_seq(t);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 5, x, y, 1008000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 5, x, y, 1009000000));
    CHECK(!(dm_live_touch_buttons(t) & (DM_BTN_A | DM_BTN_Y)));
    CHECK(dm_live_touch_tap_move_seq(t) == before + 1);

    // Cancel discards the pointer before the hook can produce a Y.
    f.calls = 0; f.state = 2; f.hit = 1;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 6, x, y, 1010000000));
    CHECK(dm_live_touch_cancel_all(t, 1010100000));
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_Y));
    CHECK(f.calls == 0);

    // A context change cancels the in-flight pointer; no Y in the new context.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 7, x, y, 1011000000));
    dm_live_touch_set_context(t, 1, dm_tc_nav_mask(1), 2, 2, 2, 0);
    CHECK(!(dm_live_touch_buttons(t) & DM_BTN_Y));

    // Fresh recovery after the rejected context still binds once.
    dm_live_touch_set_context(t, 1, dm_tc_nav_mask(1), 3, 3, 3, 0);
    f.calls = 0; f.state = 2; f.hit = 1;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 8, x, y, 1012000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 8, x, y, 1013000000));
    CHECK(dm_live_touch_buttons(t) & DM_BTN_Y);
    CHECK(f.calls == 1);

    dm_live_touch_destroy(t);
    puts("PASS: real navigation pointer end applies one original Y only for state2+skill, moves without A/Y for state0 skill/stat, and rejects unknown/cancel/context");
    return 0;
}
