// #255 owner scheduler fixture: after the last UP, actual core deferred work
// must keep the owner step alive until the bounded pulse is released and the
// state is neutral. No game acknowledgement is fabricated.
#include "../native/live_touch.c"
#include <assert.h>
#include <stdio.h>

int main(void) {
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
        int needs = dm_live_touch_scheduler_step(t, now);
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
