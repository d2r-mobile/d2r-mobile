#include "live_touch_provider.h"
#include "dm.h"
#include "dm_cursor.h"
#include "dm_uiscreen.h"

#include <string.h>

static int host_ingame(void *context) { (void)context; return dm_uiscreen_ingame(); }
static int host_panel_open(void *context) { (void)context; return dm_ui_panel_stack_count() > 0; }
static int host_panel_bits(void *context) { (void)context; return dm_ui_panel_state(); }
static uint32_t host_ui_gen(void *context) { (void)context; return dm_ui_gen(); }
static void host_hud(void *context, dm_hud_info *out) { (void)context; dm_hud_get(out); }
static uint64_t host_now(void *context) { (void)context; return now_ns(); }
static int provider_snapshot(void *context, dm_tgp_snapshot *out);

static uint32_t module_name_id(const char *name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        hash = (hash ^ *p) * 16777619u;
    return hash ? hash : 1u;
}

static int provider_hit(void *context, float x, float y, dm_tgp_hit *out) {
    (void)context; (void)x; (void)y;
    if (out) memset(out, 0, sizeof(*out));
    return 0;
}
static int provider_action(void *context, const dm_tgp_action *action, uint64_t now_ns) {
    (void)context; (void)action; (void)now_ns;
    return 0;
}

static int valid_identity(const dm_module_identity *identity) {
    return identity && identity->name[0] && identity->generation != 0 &&
           identity->size != 0 && identity->size <= DM_TGP_MAX_MODULE_BYTES &&
           identity->base != 0 && identity->base <= UINT64_MAX - identity->size;
}

static void apply_identity(dm_live_touch_provider *provider,
                           const dm_module_identity *identity) {
    provider->iface.abi_version = DM_TGP_ABI_VERSION;
    provider->iface.module_id = module_name_id(identity->name);
    provider->iface.generation = identity->generation;
    provider->iface.module_base = identity->base;
    provider->iface.module_size = identity->size;
    // The loader does not provide an object range. Keep it unknown; hit/action
    // stay unavailable and the existing bridge range validation is untouched.
    provider->iface.object_range_base = 0;
    provider->iface.object_range_size = 0;
    provider->iface.context = provider;
    provider->iface.snapshot = provider_snapshot;
    provider->iface.hit = provider_hit;
    provider->iface.action = provider_action;
}

static int sync_generic_identity(dm_live_touch_provider *provider) {
    dm_module_identity identity;
    if (!dm_module_identity_get("main", &identity) || !valid_identity(&identity))
        return 0;
    apply_identity(provider, &identity);
    return 1;
}

static int provider_snapshot(void *context, dm_tgp_snapshot *out) {
    dm_live_touch_provider *provider = context;
    if (!provider || !out) return 0;
    memset(out, 0, sizeof(*out));

    dm_module_identity before, after;
    if (!dm_module_identity_get("main", &before) || !valid_identity(&before))
        return 0;

    // Scene host callbacks may invalidate or replace the loader identity. Do
    // not hold the identity mutex across them; revalidate the exact same
    // generation/base/size/name after observation and fail closed on any
    // change. An old scene is never relabeled as a new module generation.
    dm_live_touch_scene_observation observation;
    if (!dm_live_touch_provider_observe(provider, &observation)) return 0;
    if (!dm_module_identity_get("main", &after) || !valid_identity(&after)) return 0;
    if (before.generation != after.generation || before.base != after.base ||
        before.size != after.size || strcmp(before.name, after.name) != 0)
        return 0;

    apply_identity(provider, &after);
    out->abi_version = DM_TGP_ABI_VERSION;
    out->module_id = provider->iface.module_id;
    out->generation = provider->iface.generation;
    out->scene_epoch = observation.scene_epoch;
    out->world_epoch = observation.world_epoch;
    out->object_generation = 0;
    out->captured_ns = observation.captured_ns;
    out->ui_identity = observation.ui_identity;
    out->panel_bits = observation.panel_bits;
    out->available = 1;
    out->scene = observation.scene == DM_LIVE_TOUCH_SCENE_GAME ? DM_TGP_SCENE_GAME :
                 observation.scene == DM_LIVE_TOUCH_SCENE_PANEL ? DM_TGP_SCENE_PANEL :
                 observation.scene == DM_LIVE_TOUCH_SCENE_DEAD ? DM_TGP_SCENE_DEAD :
                 DM_TGP_SCENE_MENU;
    out->dialogue_only = 0;
    out->dead = observation.dead;
    return 1;
}

void dm_live_touch_provider_init(dm_live_touch_provider *provider,
                                 const dm_live_touch_provider_host *host) {
    if (!provider) return;
    memset(provider, 0, sizeof(*provider));
    if (host) {
        provider->host = *host;
    } else {
        provider->host.context = NULL;
        provider->host.ingame = host_ingame;
        provider->host.panel_open = host_panel_open;
        provider->host.panel_bits = host_panel_bits;
        provider->host.ui_gen = host_ui_gen;
        provider->host.hud = host_hud;
        provider->host.now_ns = host_now;
    }
    provider->scene_epoch = 1;
    provider->world_epoch = 1;
}

int dm_live_touch_provider_observe(dm_live_touch_provider *provider,
                                   dm_live_touch_scene_observation *out) {
    if (!provider || !out) return 0;
    memset(out, 0, sizeof(*out));
    if (!provider->host.ingame || !provider->host.panel_open ||
        !provider->host.panel_bits || !provider->host.ui_gen ||
        !provider->host.hud || !provider->host.now_ns) return 0;

    int ingame = provider->host.ingame(provider->host.context) ? 1 : 0;
    dm_hud_info hud;
    memset(&hud, 0, sizeof(hud));
    provider->host.hud(provider->host.context, &hud);
    // An in-run observation without the real player publication is unknown.
    if (ingame && !hud.valid) return 0;

    int panel_bits = provider->host.panel_bits(provider->host.context);
    if (panel_bits < 0 || (panel_bits & ~0x7f)) return 0;
    int panel_open = provider->host.panel_open(provider->host.context) ? 1 : 0;
    uint32_t ui_gen = provider->host.ui_gen(provider->host.context);
    // dm_ui_gen is zero before the host UI generation exists. Zero is unknown:
    // never synthesize identity 1 and never let an old context survive it.
    if (ui_gen == 0) return 0;
    uint64_t now = provider->host.now_ns(provider->host.context);
    if (now == 0) return 0;

    int scene;
    if (!ingame) scene = DM_LIVE_TOUCH_SCENE_MENU;
    else if (hud.dead || (panel_bits & DM_PS_DEAD)) scene = DM_LIVE_TOUCH_SCENE_DEAD;
    else if (panel_open || panel_bits) scene = DM_LIVE_TOUCH_SCENE_PANEL;
    else scene = DM_LIVE_TOUCH_SCENE_GAME;

    int area = hud.valid ? hud.area : 0;
    int changed = !provider->have_context || provider->last_scene != scene ||
                  provider->last_ingame != ingame || provider->last_panel != panel_bits ||
                  provider->last_ui_identity != ui_gen || provider->last_area != area;
    if (changed) {
        if (provider->have_context) {
            provider->scene_epoch++;
            if (provider->last_ingame != ingame || provider->last_area != area)
                provider->world_epoch++;
        }
        provider->have_context = 1;
        provider->last_scene = scene;
        provider->last_ingame = ingame;
        provider->last_panel = panel_bits;
        provider->last_ui_identity = ui_gen;
        provider->last_area = area;
    }

    out->available = 1;
    out->scene = scene;
    out->dead = scene == DM_LIVE_TOUCH_SCENE_DEAD;
    out->panel_bits = (uint32_t)panel_bits;
    out->scene_epoch = provider->scene_epoch;
    out->world_epoch = provider->world_epoch;
    out->ui_identity = ui_gen;
    out->captured_ns = now;
    return 1;
}

const dm_tgp_provider *dm_live_touch_provider_generic_interface(
    dm_live_touch_provider *provider) {
    if (!provider || !sync_generic_identity(provider)) return NULL;
    return &provider->iface;
}
