#include "live_touch_provider.h"
#include "dm.h"
#include "dm_cursor.h"
#include "dm_uiscreen.h"

#include <string.h>

static int host_ingame(void *context) { (void)context; return dm_uiscreen_ingame(); }
static int host_panel_open(void *context) { (void)context; return dm_uiscreen_panel(); }
static int host_panel_bits(void *context) { (void)context; return dm_ui_panel_state(); }
static uint32_t host_ui_gen(void *context) { (void)context; return dm_ui_gen(); }
static void host_hud(void *context, dm_hud_info *out) { (void)context; dm_hud_get(out); }
static uint64_t host_now(void *context) { (void)context; return now_ns(); }

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
    const dm_live_touch_provider *provider) {
    (void)provider;
    // This slice has no pinned guest module, hit model or action consumer.
    // There is deliberately no generic provider object to hand to a bridge.
    return NULL;
}
