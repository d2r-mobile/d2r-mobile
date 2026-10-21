// #255 owner scheduler fixture: after the last UP, actual core deferred work
// must keep the owner step alive until the bounded pulse is released and the
// state is neutral. No game acknowledgement is fabricated.
#include "../native/live_touch.c"
#include <assert.h>
#include <stdio.h>

static void pickup_waits_for_game_receipt(void) {
    dm_live_touch *t = dm_live_touch_create(667, 375);
    assert(t);
    dm_live_touch_set_mode(t, 1, dm_tc_nav_mask(1));
    dm_tc_begin_at(&t->state, 0, 200, 150, 1.0);
    dm_tc_pointer_item(&t->state, 1);
    dm_tc_move_at(&t->state, 0, 220, 170, 1.01);
    assert(t->state.pointer_pulse_active == 1);
    // Actual input callback publishes, then attempts its short-pulse shortcut.
    // A pointer pickup must survive that callback and wait for owner receipts.
    assert(!dm_live_touch_end_released_pulse(t));
    assert(t->state.pulse_button == DM_BTN_A && !t->state.pointer_pickup_done);
    dm_live_touch_scheduler_step(t, 1010000000, 10, 1);
    dm_live_touch_scheduler_step(t, 1360000000, 10, 1);
    if (t->state.pulse_button != DM_BTN_A) {
        fprintf(stderr, "FAIL: pickup released and cursor lifted before any game receipt\n");
        abort();
    }
    dm_tc_pointer_release(&t->state, 1);
    dm_tc_end_at(&t->state, 0, 1.37);
    assert(t->state.pointer_pulse_pending);
    dm_live_touch_scheduler_step(t, 1380000000, 11, 1);
    dm_live_touch_scheduler_step(t, 1530000000, 20, 1);
    assert(t->state.pulse_button == DM_BTN_A); // polls alone do not free cursor-held A
    dm_live_touch_scheduler_step(t, 1540000000, 20, 0);
    assert(t->state.pulse_button == DM_BTN_A); // withheld polls are not delivered A
    assert(!t->state.pointer_pickup_done);
    dm_live_touch_scheduler_step(t, 1670000000, 23, 0);
    assert(!t->state.pulse_button && t->state.pointer_pulse_pending);
    dm_live_touch_scheduler_step(t, 1680000000, 23, 0);
    assert(!t->state.pulse_button);
    dm_live_touch_scheduler_step(t, 1690000000, 24, 0);
    assert(!t->state.pulse_button);
    dm_live_touch_scheduler_step(t, 1700000000, 25, 0);
    assert(t->state.pulse_button == DM_BTN_A); // two real released polls separate pickup/drop
    dm_live_touch_cancel_all(t, 1710000000);
    dm_tc_begin_at(&t->state, 0, 200, 150, 2.0);
    dm_tc_pointer_item(&t->state, 1);
    dm_tc_move_at(&t->state, 0, 220, 170, 2.01);
    dm_live_touch_scheduler_step(t, 2010000000, 30, 1);
    assert(dm_live_touch_scheduler_step(t, 4020000000LL, 30, 1) == -1);
    assert(!t->state.pulse_button && !t->state.pointer_pulse_pending);
    assert(!t->state.pointer_pickup_done); // timeout is cancellation, never a fabricated pickup
    dm_live_touch_destroy(t);
}

static void held_item_tap_uses_drop_path(void) {
    dm_live_touch *t = dm_live_touch_create(667, 375);
    assert(t);
    dm_live_touch_set_mode(t, 1, dm_tc_nav_mask(1));
    dm_live_touch_set_holding(t, 1, 0);
    dm_tc_begin_at(&t->state, 0, 200, 150, 1.0);
    dm_tc_pointer_item(&t->state, 1);
    dm_tc_end_at(&t->state, 0, 1.05);
    assert(t->state.pointer_pulse_active == 2);
    assert(t->state.pulse_button == DM_BTN_A);
    assert(!t->state.item_tap_seq); // drop, not a new tooltip selection
    dm_live_touch_cancel_all(t, 1060000000);
    dm_live_touch_set_holding(t, 1, 1);
    assert(!t->state.holding); // identification scroll is not a dragged item
    dm_live_touch_set_holding(t, 1, 0);
    dm_live_touch_set_holding(t, 0, 0);
    assert(!t->state.holding);
    dm_live_touch_destroy(t);
}

int main(void) {
    held_item_tap_uses_drop_path();
    pickup_waits_for_game_receipt();
    dm_live_touch *t = dm_live_touch_create(667, 375);
    assert(t);
    dm_live_touch_set_mode(t, 0, (1u << DM_TC_COUNT) - 1);
    t->state.skill_set = 1;

    int32_t id = 42;
    float x = t->state.layout.target[DM_TC_SKILL1].x;
    float y = t->state.layout.target[DM_TC_SKILL1].y;
    assert(dm_live_touch_event(t, 0, 0, 1, &id, &x, &y, 667, 375, 0, 0, 667, 375, 1000000000));
    assert(dm_live_touch_event(t, 1, 0, 1, &id, &x, &y, 667, 375, 0, 0, 667, 375, 1010000000));

    printf("after UP: active=%d needs=%d defer=%llu buttons=%llu\n",
           dm_live_touch_active(t), dm_live_touch_needs_work(t),
           (unsigned long long)t->state.defer_button,
           (unsigned long long)dm_live_touch_buttons(t));
    assert(!dm_live_touch_active(t));
    assert(dm_live_touch_needs_work(t));
    assert(t->state.defer_button != 0);

    int64_t now = 1010000000;
    int saw_deferred_pulse = 0, steps = 0;
    while (dm_live_touch_needs_work(t) && steps < 100) {
        now += 16000000;
        int needs = dm_live_touch_scheduler_step(t, now, (uint64_t)(now / 16000000), 0);
        if (dm_live_touch_buttons(t) > 256) saw_deferred_pulse = 1;
        printf("step %d t=%.3f needs=%d defer=%llu buttons=%llu\n", steps, now / 1e9,
               needs, (unsigned long long)t->state.defer_button,
               (unsigned long long)dm_live_touch_buttons(t));
        steps++;
    }

    assert(saw_deferred_pulse);          // the deferred skill actually reached the pad edge
    assert(steps > 0 && steps < 100);    // bounded, no endless timer spin
    assert(!dm_live_touch_needs_work(t));
    assert(dm_live_touch_buttons(t) == 0);
    assert(!dm_live_touch_active(t));
    puts("PASS: actual owner step advanced deferred core work, bounded pulse release, final neutral");
    dm_live_touch_destroy(t);
    return 0;
}
