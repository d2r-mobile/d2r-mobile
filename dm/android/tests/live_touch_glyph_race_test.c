// #35 TSAN regression: owner thread mutates the real core and publishes
// immutable glyph records; UI reader only copies. No UI reader touches core.
#include "live_touch.h"
#include "live_touch_glyph.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

static dm_live_touch *shared_touch;
static dm_tc_layout shared_layout;
static dm_tg_snapshot shared_snapshot;
static int failed;

static dm_tg_snapshot snapshot(void) {
    dm_tg_snapshot s;
    memset(&s, 0, sizeof s);
    s.supported = 1; s.valid = 1; s.ready = 1;
    s.source = DM_TG_SOURCE_NATIVE_GETTER;
    s.identity_scope = DM_TG_IDENTITY_NATIVE;
    s.captured_ns = 1000000000;
    s.scene_epoch = 1; s.world_epoch = 1;
    for (int i = 0; i < DM_TG_FACETS; ++i) {
        s.mapping[i].logical = i == 0 ? 0x2000 : i == 1 ? 0x1000 :
                               i == 2 ? 0x8000 : i == 3 ? 0x4000 :
                               i == 4 ? 0x200 : 0x800;
        s.mapping[i].normal = (uint32_t)i;
        s.mapping[i].alt = (uint32_t)(6 + i);
    }
    for (int i = 0; i < DM_TG_SLOTS; ++i) s.raw_skill_id[i] = 999;
    s.raw_skill_id[0] = 100;
    s.raw_skill_id[1] = 101;
    s.raw_skill_id[6] = 102;
    return s;
}

static void event(int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    (void)dm_live_touch_event(shared_touch, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

static void *owner(void *unused) {
    (void)unused;
    for (int i = 0; i < 20000; ++i) {
        event(DM_LIVE_ACTION_DOWN, 1, shared_layout.swap.x, shared_layout.swap.y,
              2100000000LL + i * 2000000LL);
        event(DM_LIVE_ACTION_UP, 1, shared_layout.swap.x, shared_layout.swap.y,
              2101000000LL + i * 2000000LL);
        dm_live_touch_glyph_publish(shared_touch, &shared_snapshot,
                                    2101000000LL + i * 2000000LL);
    }
    return NULL;
}

static void *ui(void *unused) {
    (void)unused;
    dm_live_touch_glyph g[5];
    for (int i = 0; i < 20000; ++i) {
        if (!dm_live_touch_skill_glyphs(NULL, NULL, 0, g)) { failed = 1; break; }
    }
    return NULL;
}

int main(void) {
    shared_touch = dm_live_touch_create(667, 600);
    CHECK(shared_touch != NULL);
    dm_tc_layout_make(&shared_layout, 667, 600);
    dm_live_touch_set_context(shared_touch, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    shared_snapshot = snapshot();
    dm_live_touch_glyph_publish(shared_touch, &shared_snapshot, 1000000000);
    pthread_t a, b;
    CHECK(pthread_create(&a, NULL, owner, NULL) == 0);
    CHECK(pthread_create(&b, NULL, ui, NULL) == 0);
    pthread_join(a, NULL); pthread_join(b, NULL);
    CHECK(!failed);

    dm_live_touch_glyph g[5];
    dm_live_touch_glyph_invalidate();
    CHECK(!dm_live_touch_skill_glyphs(NULL, NULL, 0, g));
    CHECK(dm_live_touch_glyph_signature(NULL, NULL, 0) == 0);

    dm_live_touch_destroy(shared_touch);
    puts("PASS: owner publish/UI copy path is race-free; invalidation clears unknown/scene glyphs");
    return 0;
}
