// #33 difficulty selector adapter fixture: real live_touch core plus a
// counting host request_players consumer. No game files or host execution.
#include "../native/live_touch.c"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct {
    int calls;
    int last_players;
    int invalid;
} players_fixture;

static void on_players(void *ctx, int players) {
    players_fixture *f = ctx;
    if (players < 1 || players > 8) f->invalid++;
    f->calls++;
    f->last_players = players;
}

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

static int open_selector(dm_live_touch *t, const dm_tc_layout *l, int id, int64_t at) {
    if (!event(t, DM_LIVE_ACTION_DOWN, id, l->diff.x, l->diff.y, at)) return 0;
    if (t->state.diff_open != 1) return 0;
    if (!event(t, DM_LIVE_ACTION_UP, id, l->diff.x, l->diff.y, at + 1000000)) return 0;
    return 1;
}

static int select_level(dm_live_touch *t, const dm_tc_layout *l, int index, int id, int64_t at) {
    if (!event(t, DM_LIVE_ACTION_DOWN, id, l->pick[index].x, l->pick[index].y, at)) return 0;
    if (!event(t, DM_LIVE_ACTION_UP, id, l->pick[index].x, l->pick[index].y, at + 1000000)) return 0;
    return 1;
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);

    players_fixture calls;
    memset(&calls, 0, sizeof(calls));
    dm_live_touch_difficulty_actions actions = {&calls, on_players};

    // Actual core selector: open then pick level 1.
    CHECK(open_selector(t, &layout, 10, 1000000000));
    CHECK(select_level(t, &layout, 0, 11, 1010000000));
    CHECK(t->state.diff_request == 1);
    CHECK(dm_live_touch_consume_difficulty_request(t, 1, 1, 1, &actions));
    CHECK(calls.calls == 1 && calls.last_players == 1 && calls.invalid == 0);
    CHECK(!dm_live_touch_consume_difficulty_request(t, 1, 1, 1, &actions)); // one-shot

    // Level 8 boundary.
    CHECK(open_selector(t, &layout, 12, 1020000000));
    CHECK(select_level(t, &layout, 7, 13, 1030000000));
    CHECK(t->state.diff_request == 8);
    CHECK(dm_live_touch_consume_difficulty_request(t, 1, 1, 1, &actions));
    CHECK(calls.calls == 2 && calls.last_players == 8 && calls.invalid == 0);

    // Already-produced request is discarded when the context changes.
    CHECK(open_selector(t, &layout, 14, 1040000000));
    CHECK(select_level(t, &layout, 3, 15, 1050000000));
    CHECK(t->state.diff_request == 4);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 2, 2, 2, 0);
    CHECK(!dm_live_touch_consume_difficulty_request(t, 2, 2, 2, &actions));
    CHECK(calls.calls == 2);

    // In-flight selection across a context change cannot replay on release.
    CHECK(open_selector(t, &layout, 16, 1060000000));
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 17, layout.pick[5].x, layout.pick[5].y, 1070000000));
    CHECK(t->state.diff_request == 6);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 3, 3, 3, 0);
    CHECK(!event(t, DM_LIVE_ACTION_UP, 17, layout.pick[5].x, layout.pick[5].y, 1080000000));
    CHECK(!dm_live_touch_consume_difficulty_request(t, 3, 3, 3, &actions));
    CHECK(calls.calls == 2);

    // Focus/cancel clears a request that was already produced.
    CHECK(open_selector(t, &layout, 18, 1090000000));
    CHECK(select_level(t, &layout, 2, 19, 1100000000));
    CHECK(t->state.diff_request == 3);
    CHECK(dm_live_touch_cancel_all(t, 1110000000));
    CHECK(!dm_live_touch_consume_difficulty_request(t, 3, 3, 3, &actions));
    CHECK(calls.calls == 2);

    // Navigation/menu/panel mode does not select difficulty.
    dm_live_touch_set_context(t, 1, dm_tc_nav_mask(1), 4, 4, 4, 0);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 20, layout.diff.x, layout.diff.y, 1120000000));
    CHECK(t->state.diff_open == 0 && t->state.diff_request == 0);
    (void)event(t, DM_LIVE_ACTION_UP, 20, layout.diff.x, layout.diff.y, 1121000000);
    CHECK(!dm_live_touch_consume_difficulty_request(t, 4, 4, 4, &actions));

    // Recover to gameplay and accept a fresh selection.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 5, 5, 5, 0);
    CHECK(open_selector(t, &layout, 21, 1130000000));
    CHECK(select_level(t, &layout, 6, 22, 1140000000));
    CHECK(dm_live_touch_consume_difficulty_request(t, 5, 5, 5, &actions));
    CHECK(calls.calls == 3 && calls.last_players == 7 && calls.invalid == 0);

    // Invalid/unknown levels are rejected and never reach the host callback.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 6, 6, 6, 0);
    t->state.diff_request = 0;
    CHECK(!dm_live_touch_consume_difficulty_request(t, 6, 6, 6, &actions));
    t->state.diff_request = 9;
    CHECK(!dm_live_touch_consume_difficulty_request(t, 6, 6, 6, &actions));
    t->state.diff_request = -1;
    CHECK(!dm_live_touch_consume_difficulty_request(t, 6, 6, 6, &actions));
    CHECK(calls.calls == 3 && calls.invalid == 0);

    // Unknown context cannot consume even a valid level.
    dm_live_touch_cancel_all(t, 1150000000);
    t->state.diff_request = 8;
    CHECK(!dm_live_touch_consume_difficulty_request(t, 6, 6, 6, &actions));
    CHECK(calls.calls == 3 && calls.invalid == 0);

    dm_live_touch_destroy(t);
    puts("PASS: actual core difficulty selector drives 1..8 host requests once, rejects unknown/invalid/context/cancel, and recovers");
    return 0;
}
