// #35 empty skill slot tap fixture: real live_touch/core hit test with the
// production snapshot shape and a counting dm_ui_bind_begin adapter.
#include "live_touch.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct { int calls; uint64_t key; } bind_fixture;
static void begin_bind(void *ctx, uint64_t key) {
    bind_fixture *f = ctx;
    f->calls++;
    f->key = key;
}

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

static dm_tg_snapshot snapshot(uint64_t captured, int ready, int native,
                               uint64_t scene, uint64_t world) {
    dm_tg_snapshot s;
    memset(&s, 0, sizeof s);
    s.supported = 1;
    s.valid = 1;
    s.ready = ready;
    s.source = DM_TG_SOURCE_NATIVE_GETTER;
    s.identity_scope = native ? DM_TG_IDENTITY_NATIVE : DM_TG_IDENTITY_UNCONFIRMED;
    s.captured_ns = captured;
    s.scene_epoch = scene;
    s.world_epoch = world;
    s.selected_bank = 0;
    for (int i = 0; i < DM_TG_FACETS; ++i) {
        s.mapping[i].logical = i == 0 ? 0x2000 : i == 1 ? 0x1000 :
                               i == 2 ? 0x8000 : i == 3 ? 0x4000 :
                               i == 4 ? 0x200 : 0x800;
        s.mapping[i].normal = (uint32_t)i;
        s.mapping[i].alt = (uint32_t)(6 + i);
    }
    for (int i = 0; i < DM_TG_SLOTS; ++i) s.raw_skill_id[i] = 42;
    return s;
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    float mx = layout.target[DM_TC_MAIN].x, my = layout.target[DM_TC_MAIN].y;
    float fx = layout.target[DM_TC_SKILL1].x, fy = layout.target[DM_TC_SKILL1].y;
    float sx = layout.swap.x, sy = layout.swap.y;

    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    dm_live_touch_set_skill_bind_active(t, 0);
    bind_fixture calls = {0};
    dm_live_touch_skill_bind_actions actions = {&calls, begin_bind};

    dm_tg_snapshot s = snapshot(1000000000, 1, 1, 1, 1);
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);

    // Main empty slot: consumed before the gesture layer, one tap binds A.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 1, mx, my, 1000000000));
    CHECK(!dm_live_touch_buttons(t));
    CHECK(dm_live_touch_has_pointer(t, 1));
    CHECK(event(t, DM_LIVE_ACTION_UP, 1, mx, my, 1001000000));
    CHECK(dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));
    CHECK(calls.calls == 1 && calls.key == layout.target[DM_TC_MAIN].button);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions)); // no replay

    // Bound main slot is not empty: normal core path, no binding callback.
    s.raw_skill_id[0] = 42;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 2, mx, my, 1002000000));
    CHECK(dm_live_touch_buttons(t) & layout.target[DM_TC_MAIN].button);
    (void)event(t, DM_LIVE_ACTION_UP, 2, mx, my, 1003000000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));
    CHECK(calls.calls == 1);

    // Moving beyond 14 pt is not a tap and is not replayed.
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 3, mx, my, 1004000000));
    CHECK(event(t, DM_LIVE_ACTION_MOVE, 3, mx + 20, my, 1004100000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 3, mx + 20, my, 1004200000));
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));
    CHECK(calls.calls == 1);

    // Unknown/not-ready, stale and non-native snapshots are never empty.
    s = snapshot(1005000000, 0, 1, 1, 1);
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 4, mx, my, 1005000000);
    (void)event(t, DM_LIVE_ACTION_UP, 4, mx, my, 1005100000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));

    s = snapshot(700000000, 1, 1, 1, 1);
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 5, mx, my, 1006000000);
    (void)event(t, DM_LIVE_ACTION_UP, 5, mx, my, 1006100000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));

    s = snapshot(1007000000, 1, 0, 1, 1);
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 6, mx, my, 1007000000);
    (void)event(t, DM_LIVE_ACTION_UP, 6, mx, my, 1007100000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));

    // Scene/world mismatch is not this scene and is never bound.
    s = snapshot(1008000000, 1, 1, 1, 1);
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 2, 2);
    (void)event(t, DM_LIVE_ACTION_DOWN, 7, mx, my, 1008000000);
    (void)event(t, DM_LIVE_ACTION_UP, 7, mx, my, 1008100000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 2, 2, &actions));
    CHECK(calls.calls == 1);

    // Cancel/focus after a down must discard the pending empty-slot gesture.
    s = snapshot(1009000000, 1, 1, 1, 1);
    s.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 8, mx, my, 1009000000);
    (void)dm_live_touch_cancel_all(t, 1009100000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));

    // F1 empty in the first group binds B; after the original swap gesture the
    // second group reads alt slot 6 and carries ZL.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    s = snapshot(1010000000, 1, 1, 1, 1);
    s.raw_skill_id[1] = UINT32_MAX;
    s.raw_skill_id[7] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 9, sx, sy, 1010000000);
    (void)event(t, DM_LIVE_ACTION_UP, 9, sx, sy, 1010100000); // toggle skill_set
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 10, fx, fy, 1010200000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 10, fx, fy, 1010300000));
    CHECK(dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));
    CHECK(calls.calls == 2 && calls.key == (layout.target[DM_TC_SKILL1].button | DM_BTN_ZL));

    // Second group must not use the normal empty slot if its alt slot is bound.
    s.raw_skill_id[7] = 42;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 11, fx, fy, 1010400000);
    (void)event(t, DM_LIVE_ACTION_UP, 11, fx, fy, 1010500000);
    CHECK(!dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));

    // Fresh recovery with a new empty F1 gesture still binds once.
    s.raw_skill_id[7] = UINT32_MAX;
    dm_live_touch_set_skill_snapshot(t, &s, 1, 1);
    (void)event(t, DM_LIVE_ACTION_DOWN, 12, fx, fy, 1010600000);
    (void)event(t, DM_LIVE_ACTION_UP, 12, fx, fy, 1010700000);
    CHECK(dm_live_touch_consume_skill_bind_request(t, 1, 1, &actions));
    CHECK(calls.calls == 3);

    dm_live_touch_destroy(t);
    puts("PASS: real core empty-slot tap binds once through the host adapter; bound/unknown/stale/non-native/scene/group mismatches are rejected without replay");
    return 0;
}
