#include "../app/touch_bridge.h"
#include "../runtime/touch_game_bridge.h"
#include "dm_input.h"
#include "dm_touch_controls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

typedef struct {
    uint64_t now;
    uint64_t scene_epoch;
    uint32_t module_id;
    uint64_t generation;
    uint32_t snapshot_abi;
    uint64_t object_generation;
    uint64_t hit_object_generation;
    uint64_t world_epoch;
    uint64_t ui_identity;
    int scene;
    int available;
    uint32_t extra_panel;
    int hit_kind;
    uint32_t hit_offset;
    uint32_t hit_size;
    int action_result;
    int acked, rejected;
    int last_action;
} fixture;

static int snap(void *context, dm_tgp_snapshot *out) {
    fixture *f = context;
    memset(out, 0, sizeof(*out));
    out->abi_version = f->snapshot_abi;
    out->module_id = f->module_id;
    out->generation = f->generation;
    out->scene_epoch = f->scene_epoch;
    out->world_epoch = f->world_epoch;
    out->object_generation = f->object_generation;
    out->captured_ns = f->now;
    out->ui_identity = f->ui_identity;
    out->panel_bits = f->scene == DM_TGP_SCENE_PANEL ? 1u : 0u;
    out->panel_bits |= f->extra_panel;
    out->available = f->available;
    out->scene = f->scene;
    out->dialogue_only = f->scene == DM_TGP_SCENE_DIALOG;
    out->dead = f->scene == DM_TGP_SCENE_DEAD;
    return 1;
}

static int hit(void *context, float x, float y, dm_tgp_hit *out) {
    fixture *f = context;
    (void)x; (void)y;
    memset(out, 0, sizeof(*out));
    out->valid = 1;
    out->kind = f->hit_kind;
    out->drop_kind = 0;
    out->object_id = f->hit_kind == DM_TGP_HIT_NONE ? 0 : 0x1234;
    out->object_generation = f->hit_object_generation;
    out->scene_epoch = f->scene_epoch;
    out->object_address = 0x20000 + f->hit_offset;
    out->object_size = f->hit_size;
    return 1;
}

static int action(void *context, const dm_tgp_action *out, uint64_t now_ns) {
    fixture *f = context;
    (void)now_ns;
    f->last_action = out->kind;
    if (!f->action_result) { f->rejected++; return 0; }
    f->acked++;
    return 1;
}

static dm_tgp_provider provider(fixture *f) {
    dm_tgp_provider p;
    memset(&p, 0, sizeof(p));
    p.abi_version = DM_TGP_ABI_VERSION;
    p.module_id = f->module_id;
    p.generation = f->generation;
    p.module_base = 0x10000;
    p.module_size = 0x1000;
    p.object_range_base = 0x20000;
    p.object_range_size = 0x1000;
    p.context = f;
    p.snapshot = snap;
    p.hit = hit;
    p.action = action;
    return p;
}

static dm_touch_event event_at(int action, float x, float y, int64_t now) {
    dm_touch_event e;
    memset(&e, 0, sizeof(e));
    e.action = (dm_touch_action)action;
    e.action_index = 0;
    e.pointer_count = 1;
    e.time_ns = now;
    e.points[0].id = 42;
    e.points[0].x_px = x;
    e.points[0].y_px = y;
    return e;
}

static void world_point(float *x, float *y) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 375);
    for (int py = 20; py < 350; py += 10) {
        for (int px = 20; px < 640; px += 10) {
            if (dm_tc_hit(&layout, (float)px, (float)py) == DM_TC_NONE) {
                *x = (float)px;
                *y = (float)py;
                return;
            }
        }
    }
    *x = 1.0f;
    *y = 1.0f;
}

static void reset_fixture(fixture *f) {
    memset(f, 0, sizeof(*f));
    f->now = 1000000000;
    f->scene_epoch = 1;
    f->module_id = 7;
    f->generation = 10;
    f->snapshot_abi = DM_TGP_ABI_VERSION;
    f->object_generation = f->generation;
    f->hit_object_generation = f->generation;
    f->world_epoch = 1;
    f->scene = DM_TGP_SCENE_GAME;
    f->available = 1;
    f->hit_kind = DM_TGP_HIT_NONE;
    f->hit_offset = 0x20;
    f->hit_size = 0x20;
    f->action_result = 1;
}

static void transition_case(int which, uint64_t value) {
    fixture f;
    reset_fixture(&f);
    dm_tgp_provider p = provider(&f);
    dm_touch_bridge *bridge = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(bridge != NULL);
    CHECK(dm_touch_bridge_attach_game(bridge, &p));
    float wx, wy;
    world_point(&wx, &wy);
    dm_touch_event down = event_at(DM_TOUCH_ACTION_DOWN, wx, wy, 2000000000);
    f.now = 2000000000;
    CHECK(dm_touch_bridge_event(bridge, &down));
    if (which == 0) f.scene_epoch = value;
    else if (which == 1) f.world_epoch = value;
    else if (which == 2) f.ui_identity = value;
    else if (which == 3) { f.object_generation = value; f.hit_object_generation = value; }
    else if (which == 4) f.scene = DM_TGP_SCENE_MENU;
    else if (which == 5) f.extra_panel = (uint32_t)value;
    dm_touch_event move = down;
    move.action = DM_TOUCH_ACTION_MOVE;
    move.time_ns = 2000000100;
    f.now = 2000000100;
    CHECK(!dm_touch_bridge_event(bridge, &move));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);
    dm_touch_event stale_up = down;
    stale_up.action = DM_TOUCH_ACTION_UP;
    stale_up.time_ns = 2000000150;
    f.now = 2000000150;
    CHECK(!dm_touch_bridge_event(bridge, &stale_up));
    dm_touch_event again = down;
    again.time_ns = 2000000200;
    f.now = 2000000200;
    CHECK(dm_touch_bridge_event(bridge, &again));
    CHECK(dm_touch_bridge_cancel_all(bridge, 2000000300));
    dm_touch_bridge_destroy(bridge, NULL);
}

static void provider_loss_case(void) {
    fixture f;
    reset_fixture(&f);
    dm_tgp_provider p = provider(&f);
    dm_touch_bridge *bridge = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(bridge != NULL);
    CHECK(dm_touch_bridge_attach_game(bridge, &p));
    float wx, wy;
    world_point(&wx, &wy);
    dm_touch_event down = event_at(DM_TOUCH_ACTION_DOWN, wx, wy, 2100000000);
    f.now = 2100000000;
    CHECK(dm_touch_bridge_event(bridge, &down));
    f.available = 0;
    f.now = 2100000100;
    CHECK(dm_touch_bridge_tick(bridge, 2100000100));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);
    f.available = 1;
    f.now = 2100000200;
    dm_touch_event old_up = event_at(DM_TOUCH_ACTION_UP, wx, wy, 2100000200);
    CHECK(!dm_touch_bridge_event(bridge, &old_up));
    dm_touch_event again = down;
    again.time_ns = 2100000300;
    f.now = 2100000300;
    CHECK(dm_touch_bridge_event(bridge, &again));
    CHECK(dm_touch_bridge_cancel_all(bridge, 2100000400));
    dm_touch_bridge_destroy(bridge, NULL);
}

static void replace_failure_case(void) {
    fixture f;
    reset_fixture(&f);
    dm_tgp_provider p = provider(&f);
    dm_touch_bridge *bridge = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(bridge != NULL);
    CHECK(dm_touch_bridge_attach_game(bridge, &p));
    float wx, wy;
    world_point(&wx, &wy);
    dm_touch_event down = event_at(DM_TOUCH_ACTION_DOWN, wx, wy, 2200000000);
    f.now = 2200000000;
    CHECK(dm_touch_bridge_event(bridge, &down));
    dm_tgp_provider bad = provider(&f);
    bad.module_id = 0;
    CHECK(!dm_touch_bridge_attach_game(bridge, &bad));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);
    dm_touch_event old_up = event_at(DM_TOUCH_ACTION_UP, wx, wy, 2200000100);
    CHECK(!dm_touch_bridge_event(bridge, &old_up));
    dm_touch_bridge_destroy(bridge, NULL);
}

int main(void) {
    fixture f;
    reset_fixture(&f);
    dm_tgp_provider p = provider(&f);
    dm_touch_bridge *bridge = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(bridge != NULL);
    CHECK(dm_touch_bridge_attach_game(bridge, &p));

    /* Missing/unavailable provider: reject neutral, never a fake scene. */
    f.available = 0;
    float wx, wy;
    world_point(&wx, &wy);
    dm_touch_event down = event_at(DM_TOUCH_ACTION_DOWN, wx, wy, 1000000000);
    CHECK(!dm_touch_bridge_event(bridge, &down));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);

    f.available = 1;
    CHECK(dm_touch_bridge_event(bridge, &down));
    dm_touch_event up = event_at(DM_TOUCH_ACTION_UP, wx, wy, 1000000100);
    CHECK(dm_touch_bridge_event(bridge, &up));
    CHECK(f.acked == 1);
    CHECK(f.last_action == DM_TGP_ACTION_WORLD_TAP);

    /* Wrong module generation is rejected on refresh. */
    f.generation = 11;
    down.time_ns = 1100000000; up.time_ns = 1100000100;
    f.now = 1100000000;
    CHECK(!dm_touch_bridge_event(bridge, &down));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);

    /* Unknown/mismatched module identity in a snapshot is rejected. */
    f.generation = 10;
    f.module_id = 7;
    p = provider(&f);
    p.module_id = 8; /* provider says 8; snapshot still reports 7 */
    dm_touch_bridge *wrong = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(wrong != NULL);
    CHECK(dm_touch_bridge_attach_game(wrong, &p));
    down.time_ns = 1200000000;
    f.now = 1200000000;
    CHECK(!dm_touch_bridge_event(wrong, &down));
    dm_touch_bridge_destroy(wrong, NULL);

    /* Unknown snapshot ABI is rejected before its scene is adopted. */
    f.module_id = 7;
    f.snapshot_abi = DM_TGP_ABI_VERSION + 1;
    p = provider(&f);
    dm_touch_bridge *abi = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(abi != NULL);
    CHECK(dm_touch_bridge_attach_game(abi, &p));
    down.time_ns = 1250000000;
    f.now = 1250000000;
    CHECK(!dm_touch_bridge_event(abi, &down));
    dm_touch_bridge_destroy(abi, NULL);
    f.snapshot_abi = DM_TGP_ABI_VERSION;

    /* Unmapped object range is rejected, not silently treated as a game hit. */
    f.module_id = 7;
    f.hit_kind = DM_TGP_HIT_ITEM;
    f.hit_offset = 0x1000;
    f.hit_size = 1;
    p = provider(&f);
    dm_touch_bridge *range = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(range != NULL);
    CHECK(dm_touch_bridge_attach_game(range, &p));
    down.time_ns = 1300000000;
    f.now = 1300000000;
    CHECK(!dm_touch_bridge_event(range, &down));
    dm_touch_bridge_destroy(range, NULL);

    /* Retired object generation is rejected even when the module matches. */
    f.hit_offset = 0x20;
    f.hit_size = 0x20;
    f.hit_kind = DM_TGP_HIT_ITEM;
    f.hit_object_generation = f.generation + 1;
    p = provider(&f);
    dm_touch_bridge *stale = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(stale != NULL);
    CHECK(dm_touch_bridge_attach_game(stale, &p));
    down.time_ns = 1350000000;
    f.now = 1350000000;
    CHECK(!dm_touch_bridge_event(stale, &down));
    dm_touch_bridge_destroy(stale, NULL);
    f.hit_object_generation = f.generation;

    /* Blocked consumer: core generated the action, but no phantom ack. */
    f.hit_offset = 0x20;
    f.hit_size = 0x20;
    f.hit_kind = DM_TGP_HIT_NONE;
    f.action_result = 0;
    f.acked = f.rejected = 0;
    p = provider(&f);
    dm_touch_bridge *blocked = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(blocked != NULL);
    CHECK(dm_touch_bridge_attach_game(blocked, &p));
    down.time_ns = 1400000000;
    f.now = 1400000000;
    down.points[0].x_px = wx; down.points[0].y_px = wy;
    up.time_ns = 1400000100;
    up.points[0].x_px = wx; up.points[0].y_px = wy;
    CHECK(dm_touch_bridge_event(blocked, &down));
    f.now = 1400000100;
    CHECK(dm_touch_bridge_event(blocked, &up));
    CHECK(f.acked == 0);
    CHECK(f.rejected == 1);
    dm_touch_bridge_destroy(blocked, NULL);

    /* Object-specific actions are explicitly rejected in this bounded slice. */
    f.scene = DM_TGP_SCENE_MENU;
    f.scene_epoch = 5;
    f.world_epoch = 1;
    f.hit_kind = DM_TGP_HIT_ITEM;
    f.hit_offset = 0x20;
    f.hit_size = 0x20;
    f.object_generation = f.generation;
    f.action_result = 1;
    f.acked = f.rejected = 0;
    p = provider(&f);
    dm_touch_bridge *object = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(object != NULL);
    CHECK(dm_touch_bridge_attach_game(object, &p));
    for (int tap = 0; tap < 2; tap++) {
        down.time_ns = 1700000000 + tap * 100000000;
        f.now = (uint64_t)down.time_ns;
        CHECK(dm_touch_bridge_event(object, &down));
        up.time_ns = down.time_ns + 1000000;
        f.now = (uint64_t)up.time_ns;
        CHECK(dm_touch_bridge_event(object, &up));
    }
    CHECK(f.acked == 0);
    CHECK(f.rejected == 0); /* object action was filtered before provider action */
    dm_touch_bridge_destroy(object, NULL);

    /* Scene switch changes navigation context and clears the old gesture. */
    f.scene_epoch = 2;
    f.scene = DM_TGP_SCENE_PANEL;
    f.hit_kind = DM_TGP_HIT_ITEM;
    f.hit_offset = 0x20;
    f.hit_size = 0x20;
    f.action_result = 1;
    p = provider(&f);
    dm_touch_bridge *scene = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(scene != NULL);
    CHECK(dm_touch_bridge_attach_game(scene, &p));
    down.time_ns = 1500000000;
    f.now = 1500000000;
    CHECK(dm_touch_bridge_event(scene, &down));
    f.scene_epoch = 3;
    f.scene = DM_TGP_SCENE_MENU;
    f.hit_kind = DM_TGP_HIT_NONE;
    up.time_ns = 1500000100;
    f.now = 1500000100;
    CHECK(!dm_touch_bridge_event(scene, &up));
    CHECK(dm_touch_bridge_snapshot(scene).buttons == 0);
    dm_touch_bridge_destroy(scene, NULL);

    /* World identity switch also cancels the old gesture. */
    f.scene_epoch = 4;
    f.world_epoch = 2;
    f.scene = DM_TGP_SCENE_GAME;
    f.hit_kind = DM_TGP_HIT_NONE;
    p = provider(&f);
    dm_touch_bridge *world = dm_touch_bridge_create(667, 375, 1.0f);
    CHECK(world != NULL);
    CHECK(dm_touch_bridge_attach_game(world, &p));
    down.time_ns = 1600000000;
    f.now = 1600000000;
    CHECK(dm_touch_bridge_event(world, &down));
    f.world_epoch = 3;
    up.time_ns = 1600000100;
    f.now = 1600000100;
    CHECK(!dm_touch_bridge_event(world, &up));
    CHECK(dm_touch_bridge_snapshot(world).buttons == 0);
    dm_touch_bridge_destroy(world, NULL);

    provider_loss_case();
    transition_case(0, 7);
    transition_case(1, 7);
    transition_case(2, 7);
    transition_case(3, 99);
    transition_case(4, 0);
    transition_case(5, 16);
    replace_failure_case();

    dm_touch_bridge_destroy(bridge, NULL);
    printf("PASS touch game bridge: %d checks\n", checks);
    return 0;
}
