// #33 module-code identity fixture: loader publication is the only source for
// the generic provider fields. No game files or guest memory are used.
#include "live_touch_provider.h"
#include "live_touch.h"
#include "dm.h"
#include "../ios/Sources/dm_touch_controls.h"

#include <stdio.h>
#include <string.h>

int dm_uiscreen_ingame(void) { return 0; }
int dm_uiscreen_panel(void) { return 0; }
int dm_ui_panel_state(void) { return 0; }
uint32_t dm_ui_gen(void) { return 1; }
void dm_hud_get(dm_hud_info *out) { memset(out, 0, sizeof(*out)); }
uint64_t now_ns(void) { return 1; }

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

static int f_ingame(void *ctx) { (void)ctx; return 1; }
static int f_panel_open(void *ctx) { (void)ctx; return 0; }
static int f_panel_bits(void *ctx) { (void)ctx; return 0; }
static uint32_t f_ui_gen(void *ctx) { (void)ctx; return 3; }
static int hud_invalidate, hud_republish;
static module_t hud_modules[2];
static void f_hud(void *ctx, dm_hud_info *out) {
    (void)ctx;
    memset(out, 0, sizeof(*out)); out->valid = 1; out->area = 1;
    if (hud_invalidate) {
        dm_module_identity_invalidate();
        if (hud_republish) (void)dm_module_identity_publish(hud_modules, 2);
        hud_invalidate = hud_republish = 0;
    }
}
static uint64_t f_now(void *ctx) { (void)ctx; return 1000000000; }

static module_t module_at(const char *name, uintptr_t base, size_t size) {
    module_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.name, name, sizeof(m.name) - 1);
    m.base = (uint8_t *)base;
    m.size = size;
    return m;
}

int main(void) {
    dm_module_identity identity;
    CHECK(!dm_module_identity_get("main", &identity));

    module_t modules[2];
    modules[0] = module_at("main", 0x100000, 0x2000);
    modules[1] = module_at("sdk", 0x200000, 0x1000);
    CHECK(!dm_module_identity_publish(NULL, 0));
    CHECK(!dm_module_identity_get("main", &identity));
    CHECK(dm_module_identity_publish(modules, 2));
    CHECK(dm_module_identity_get("main", &identity));
    CHECK(identity.base == 0x100000 && identity.size == 0x2000 && identity.generation != 0);
    CHECK(dm_module_identity_get("sdk", &identity));
    CHECK(identity.base == 0x200000 && identity.size == 0x1000);
    CHECK(!dm_module_identity_get("missing", &identity));

    dm_live_touch_provider_host host = {
        .context = NULL, .ingame = f_ingame, .panel_open = f_panel_open,
        .panel_bits = f_panel_bits, .ui_gen = f_ui_gen, .hud = f_hud, .now_ns = f_now
    };
    dm_live_touch_provider provider;
    dm_live_touch_provider_init(&provider, &host);
    dm_live_touch_scene_observation observation;
    CHECK(dm_live_touch_provider_observe(&provider, &observation));
    CHECK(observation.available && observation.scene == DM_LIVE_TOUCH_SCENE_GAME);

    const dm_tgp_provider *iface = dm_live_touch_provider_generic_interface(&provider);
    CHECK(iface != NULL);
    CHECK(iface->module_base == 0x100000 && iface->module_size == 0x2000);
    CHECK(dm_module_identity_get("main", &identity));
    CHECK(iface->generation == identity.generation && iface->module_id != 0);
    CHECK(iface->object_range_size == 0);
    dm_tgp_hit hit;
    CHECK(!iface->hit(provider.iface.context, 0, 0, &hit));
    dm_tgp_action action;
    memset(&action, 0, sizeof(action));
    CHECK(!iface->action(provider.iface.context, &action, 1000000000));

    dm_tgp_snapshot snapshot;
    CHECK(iface->snapshot(iface->context, &snapshot));
    CHECK(snapshot.available && snapshot.scene == DM_TGP_SCENE_GAME);
    CHECK(snapshot.module_id == iface->module_id && snapshot.generation == iface->generation);
    CHECK(snapshot.object_generation == 0);

    // Invalidation during scene/HUD observation must fail closed and zero the
    // snapshot, even though the old provider interface pointer remains valid.
    hud_modules[0] = modules[0];
    hud_modules[1] = modules[1];
    hud_invalidate = 1;
    CHECK(!iface->snapshot(iface->context, &snapshot));
    CHECK(!snapshot.available && snapshot.generation == 0 && snapshot.module_id == 0);
    CHECK(dm_live_touch_provider_generic_interface(&provider) == NULL);
    CHECK(dm_module_identity_publish(modules, 2));
    CHECK(dm_live_touch_provider_generic_interface(&provider) != NULL);

    // Invalidate plus a new publication during observation is a new generation;
    // the old scene must not be relabeled with it.
    hud_modules[0] = module_at("main", 0x300000, 0x4000);
    hud_invalidate = 1; hud_republish = 1;
    CHECK(!iface->snapshot(iface->context, &snapshot));
    CHECK(!snapshot.available && snapshot.generation == 0);
    CHECK(dm_live_touch_provider_generic_interface(&provider) != NULL);
    CHECK(dm_module_identity_publish(modules, 2));

    // Unchanged identity remains usable after the interleaving cases.
    CHECK(iface->snapshot(iface->context, &snapshot));
    CHECK(snapshot.available && snapshot.generation != 0);

    // A next load invalidates the old snapshot. The old provider copy keeps the
    // old generation; a new publication must not make that identity current.
    uint64_t old_generation = iface->generation;
    dm_tgp_provider old = *iface;
    dm_module_identity_invalidate();
    CHECK(!dm_module_identity_get("main", &identity));
    CHECK(dm_live_touch_provider_generic_interface(&provider) == NULL);
    modules[0] = module_at("main", 0x300000, 0x4000);
    CHECK(dm_module_identity_publish(modules, 2));
    const dm_tgp_provider *new_iface = dm_live_touch_provider_generic_interface(&provider);
    CHECK(new_iface != NULL && new_iface->generation != old_generation);
    CHECK(old.snapshot(old.context, &snapshot));
    CHECK(snapshot.generation != old.generation);   // old bridge identity would reject this

    // Unknown / overflowing metadata must not publish a snapshot.
    dm_module_identity_invalidate();
    modules[0] = module_at("main", UINTPTR_MAX - 8, 16);
    CHECK(!dm_module_identity_publish(modules, 2));
    CHECK(!dm_module_identity_get("main", &identity));
    modules[0] = module_at("main", 0x400000, 0x1000);
    CHECK(dm_module_identity_publish(modules, 2));
    CHECK(dm_module_identity_get("main", &identity));

    puts("PASS: loader-published module identity is exact, generation-bound, fail-closed and generic hit/action remain unavailable");
    return 0;
}
