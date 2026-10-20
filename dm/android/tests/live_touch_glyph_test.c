// #35 real glyph mapping presentation fixture with synthetic atlas fixtures.
#include "live_touch.h"
#include "live_touch_glyph.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

static dm_tg_snapshot make_snapshot(uint32_t main_id, uint32_t f1_id,
                                    uint32_t main_alt, uint64_t captured) {
    dm_tg_snapshot s;
    memset(&s, 0, sizeof s);
    s.supported = 1; s.valid = 1; s.ready = 1;
    s.source = DM_TG_SOURCE_NATIVE_GETTER;
    s.identity_scope = DM_TG_IDENTITY_NATIVE;
    s.captured_ns = captured;
    s.scene_epoch = 1; s.world_epoch = 1;
    for (int i = 0; i < DM_TG_FACETS; ++i) {
        s.mapping[i].logical = i == 0 ? 0x2000 : i == 1 ? 0x1000 :
                               i == 2 ? 0x8000 : i == 3 ? 0x4000 :
                               i == 4 ? 0x200 : 0x800;
        s.mapping[i].normal = (uint32_t)i;
        s.mapping[i].alt = (uint32_t)(6 + i);
    }
    for (int i = 0; i < DM_TG_SLOTS; ++i) s.raw_skill_id[i] = 999;
    s.raw_skill_id[0] = main_id;
    s.raw_skill_id[1] = f1_id;
    s.raw_skill_id[6] = main_alt;
    return s;
}

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    dm_live_touch *touch = dm_live_touch_create(667, 600);
    CHECK(touch != NULL);
    dm_live_touch_set_context(touch, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);

    dm_tg_snapshot snap = make_snapshot(100, 101, 102, 1000000000);
    dm_live_touch_glyph g[5];
    dm_live_touch_glyph_publish(touch, &snap, 1000000000);
    CHECK(dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(g[0].status == DM_LIVE_GLYPH_GLYPH && g[0].atlas == 0 &&
          g[0].normal_frame == 2 && g[0].pressed_frame == 3);
    CHECK(g[1].status == DM_LIVE_GLYPH_GLYPH && g[1].atlas == 1 &&
          g[1].normal_frame == 4 && g[1].pressed_frame == 5);
    CHECK(g[2].status == DM_LIVE_GLYPH_UNKNOWN);

    // Native general actions (weapon switch / map family) are real glyphs, not
    // empty slots; only the explicit empty sentinel and unknown IDs fall back.
    snap.raw_skill_id[1] = 357;
    dm_live_touch_glyph_publish(touch, &snap, 1000000000);
    CHECK(dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(g[1].status == DM_LIVE_GLYPH_GLYPH && g[1].atlas == 0 && g[1].normal_frame == 1);

    dm_ta_frame frame;
    CHECK(dm_ta_frame_at(0, 2, &frame) && frame.x == 136 && frame.y == 0 &&
          frame.width == 65 && frame.height == 65);
    CHECK(!dm_ta_frame_at(0, 99, &frame));
    CHECK(!dm_ta_frame_at(9, 0, &frame));

    uint64_t before = dm_live_touch_glyph_signature(NULL, NULL, 0);
    snap.raw_skill_id[0] = UINT32_MAX;
    dm_live_touch_glyph_publish(touch, &snap, 1000000000);
    CHECK(dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(g[0].status == DM_LIVE_GLYPH_EMPTY);
    CHECK(dm_live_touch_glyph_signature(NULL, NULL, 0) != before);

    snap.raw_skill_id[0] = 103; // synthetic row with an out-of-atlas frame
    dm_live_touch_glyph_publish(touch, &snap, 1000000000);
    CHECK(dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(g[0].status == DM_LIVE_GLYPH_UNKNOWN);

    snap.raw_skill_id[0] = 100;
    dm_live_touch_glyph_publish(touch, &snap, 2000000000);
    CHECK(dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(g[0].status == DM_LIVE_GLYPH_GLYPH && g[0].normal_frame == 2);

    // Second group: actual core swap target, alt slot 6 -> synthetic ID 102.
    snap.raw_skill_id[0] = 100;
    snap.raw_skill_id[6] = 102;
    float sx = layout.swap.x, sy = layout.swap.y;
    CHECK(event(touch, DM_LIVE_ACTION_DOWN, 1, sx, sy, 2100000000));
    CHECK(event(touch, DM_LIVE_ACTION_UP, 1, sx, sy, 2101000000));
    CHECK(dm_live_touch_skill_set(touch) == 1);
    dm_live_touch_glyph_publish(touch, &snap, 2101000000);
    CHECK(dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(g[0].status == DM_LIVE_GLYPH_GLYPH && g[0].atlas == 1 &&
          g[0].normal_frame == 4);

    dm_live_touch_destroy(touch);
    puts("PASS: synthetic production mapping yields atlas/frame, empty/unknown/missing/out-of-range, stale visual policy and alt-set selection");
    return 0;
}
