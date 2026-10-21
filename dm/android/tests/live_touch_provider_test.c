// #33 owned fixture for the observation-only provider seam. The host callbacks
// are injected so the test can prove context identity transitions without a
// game. The provider is the production implementation; there is no fake
// module-backed generic provider in this slice.
#include "live_touch_provider.h"
#include "live_touch.h"
#include "../ios/Sources/dm_touch_controls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Controlled host stubs for the production-default entry path. The injected
// host below is the only source of the other cases.
static int prod_ingame, prod_panel_open, prod_panel_bits, prod_hud_valid, prod_dead, prod_area;
static int prod_panel_stack;
static uint32_t prod_ui_gen = 1;
static uint64_t prod_now = 1;
int dm_uiscreen_ingame(void) { return prod_ingame; }
int dm_uiscreen_panel(void) { return prod_panel_open; }
int dm_ui_panel_stack_count(void) { return prod_panel_stack; }
int dm_ui_panel_state(void) { return prod_panel_bits; }
uint32_t dm_ui_gen(void) { return prod_ui_gen; }
void dm_hud_get(dm_hud_info *out) {
    memset(out, 0, sizeof(*out));
    out->valid = prod_hud_valid;
    out->dead = prod_dead;
    out->area = prod_area;
}
uint64_t now_ns(void) { return prod_now; }

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct {
    int ingame, panel_open, panel_bits, hud_valid, dead, area;
    uint32_t ui_gen;
    uint64_t now;
    uint64_t player_identity, game_identity;
    int x,y;
} fixture;

static int f_ingame(void *ctx) { return ((fixture *)ctx)->ingame; }
static int f_panel_open(void *ctx) { return ((fixture *)ctx)->panel_open; }
static int f_panel_bits(void *ctx) { return ((fixture *)ctx)->panel_bits; }
static uint32_t f_ui_gen(void *ctx) { return ((fixture *)ctx)->ui_gen; }
static void f_hud(void *ctx, dm_hud_info *out) {
    fixture *f = ctx;
    memset(out, 0, sizeof(*out));
    out->valid = f->hud_valid;
    out->dead = f->dead;
    out->area = f->area;
    out->player_identity=f->player_identity;out->game_identity=f->game_identity;
    out->x=f->x;out->y=f->y;
}
static uint64_t f_now(void *ctx) { return ((fixture *)ctx)->now; }

static int ui_cancel_count;
static void ui_cancel(void) { ui_cancel_count++; }

int main(void) {
    fixture f;
    memset(&f, 0, sizeof(f));
    f.ingame = 0;
    f.ui_gen = 1;
    f.now = 1000000000;

    dm_live_touch_provider_host host = {
        .context = &f,
        .ingame = f_ingame, .panel_open = f_panel_open, .panel_bits = f_panel_bits,
        .ui_gen = f_ui_gen, .hud = f_hud, .now_ns = f_now
    };
    dm_live_touch_provider provider;
    dm_live_touch_provider_init(&provider, &host);
    // This scene observation has no pinned guest module/hit/action contract.
    CHECK(dm_live_touch_provider_generic_interface(&provider) == NULL);

    dm_live_touch *touch = dm_live_touch_create(667, 375);
    CHECK(touch != NULL);
    dm_live_touch_scene_observation obs;

    // Front-end is a real observed menu page even before any player exists.
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    CHECK(obs.available && obs.scene == DM_LIVE_TOUCH_SCENE_MENU);
    dm_live_touch_set_context(touch, 1, dm_tc_nav_mask(1), obs.scene_epoch, obs.world_epoch,
                              obs.ui_identity, 0);

    // In-run requires the real HUD publication; unknown is not promoted to GAME.
    f.ingame = 1; f.hud_valid = 0; f.now = 1100000000;
    CHECK(!dm_live_touch_provider_observe(&provider, &obs));
    CHECK(!obs.available);
    f.hud_valid = 1; f.area = 1; f.ui_gen = 2;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    CHECK(obs.available && obs.scene == DM_LIVE_TOUCH_SCENE_GAME);
    dm_live_touch_set_context(touch, 0, (1u << DM_TC_COUNT) - 1, obs.scene_epoch,
                              obs.world_epoch, obs.ui_identity, 0);

    int32_t id = 42;
    float x = 70, y = 300;
    CHECK(dm_live_touch_event(touch, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y,
                              667, 375, 0, 0, 667, 375, 1200000000));
    CHECK(dm_live_touch_active(touch));

    // Focus/input loss uses the same real cancel boundary: UI phase 3 and core
    // ownership are cancelled together, then a fresh pointer can be admitted.
    dm_live_touch_cancel_sources(touch, 1250000000, ui_cancel);
    CHECK(ui_cancel_count == 1);
    CHECK(!dm_live_touch_active(touch));
    CHECK(dm_live_touch_event(touch, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y,
                              667, 375, 0, 0, 667, 375, 1260000000));
    CHECK(dm_live_touch_active(touch));

    // UI/character identity changes: old pointer is cancelled before the new
    // context is installed, and its late UP cannot reach the new identity.
    f.ui_gen = 3; f.now = 1300000000;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    CHECK(obs.scene_epoch > 1);
    CHECK(obs.ui_identity == 3);
    dm_live_touch_set_context(touch, 0, (1u << DM_TC_COUNT) - 1, obs.scene_epoch,
                              obs.world_epoch, obs.ui_identity, 0);
    CHECK(!dm_live_touch_active(touch));
    CHECK(dm_live_touch_buttons(touch) == 0);
    CHECK(!dm_live_touch_event(touch, DM_LIVE_ACTION_UP, 0, 1, &id, &x, &y,
                               667, 375, 0, 0, 667, 375, 1400000000));

    // Leaving the run is a scene/world change too; no old pointer survives.
    f.ingame = 0; f.now = 1500000000;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    CHECK(obs.scene == DM_LIVE_TOUCH_SCENE_MENU);
    CHECK(obs.world_epoch > 1);
    dm_live_touch_set_context(touch, 1, dm_tc_nav_mask(1), obs.scene_epoch, obs.world_epoch,
                              obs.ui_identity, 0);
    CHECK(!dm_live_touch_active(touch));

    // Panel/dead are observed from the existing host panel/HUD entries.
    f.ingame = 1; f.hud_valid = 1; f.panel_open = 1; f.panel_bits = 1; f.ui_gen = 4; f.now = 1600000000;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    CHECK(obs.scene == DM_LIVE_TOUCH_SCENE_PANEL && obs.panel_bits == 1);
    f.dead = 1; f.ui_gen = 5; f.now = 1700000000;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    CHECK(obs.scene == DM_LIVE_TOUCH_SCENE_DEAD && obs.dead);

    // Unknown UI identity (0) must be unavailable, never coerced to identity 1.
    f.ingame = 1; f.hud_valid = 1; f.dead = 0; f.panel_open = 0; f.panel_bits = 0;
    f.ui_gen = 6; f.now = 1750000000;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    dm_live_touch_set_context(touch, 0, (1u << DM_TC_COUNT) - 1, obs.scene_epoch,
                              obs.world_epoch, obs.ui_identity, 0);
    CHECK(dm_live_touch_event(touch, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y,
                              667, 375, 0, 0, 667, 375, 1760000000));
    CHECK(dm_live_touch_active(touch));
    f.ui_gen = 0; f.now = 1770000000;
    CHECK(!dm_live_touch_provider_observe(&provider, &obs));
    CHECK(!obs.available);
    dm_live_touch_cancel_sources(touch, 1770000000, ui_cancel);
    CHECK(ui_cancel_count == 2);
    CHECK(!dm_live_touch_active(touch));
    // Recovery with a new identity must not revive the old pointer/UP.
    f.ui_gen = 7; f.now = 1780000000;
    CHECK(dm_live_touch_provider_observe(&provider, &obs));
    dm_live_touch_set_context(touch, 0, (1u << DM_TC_COUNT) - 1, obs.scene_epoch,
                              obs.world_epoch, obs.ui_identity, 0);
    CHECK(!dm_live_touch_event(touch, DM_LIVE_ACTION_UP, 0, 1, &id, &x, &y,
                               667, 375, 0, 0, 667, 375, 1790000000));

    // A fresh, nearby observation of the same player/game can rebase ONLY movement.
    f.ingame=1;f.hud_valid=1;f.area=1;f.x=100;f.y=100;
    f.player_identity=0x1000;f.game_identity=0x2000;f.now=2000000000;
    CHECK(dm_live_touch_provider_observe(&provider,&obs));
    dm_live_touch_set_context(touch,0,(1u<<DM_TC_COUNT)-1,obs.scene_epoch,obs.world_epoch,obs.ui_identity,0);
    x=70;y=300;
    CHECK(dm_live_touch_event(touch,DM_LIVE_ACTION_DOWN,0,1,&id,&x,&y,667,375,0,0,667,375,2010000000));
    x=120;y=280;
    CHECK(dm_live_touch_event(touch,DM_LIVE_ACTION_MOVE,0,1,&id,&x,&y,667,375,0,0,667,375,2020000000));
    dm_tc_state *controls=dm_live_touch_controls(touch);
    int32_t before_x=controls->lx,before_y=controls->ly;
    CHECK(before_x||before_y);
    int32_t both_ids[]={id,id+1};
    float both_x[]={x,controls->layout.target[DM_TC_SKILL1].x};
    float both_y[]={y,controls->layout.target[DM_TC_SKILL1].y};
    CHECK(dm_live_touch_event(touch,DM_LIVE_ACTION_POINTER_DOWN,1,2,both_ids,both_x,both_y,
                              667,375,0,0,667,375,2030000000));
    int skill_slot=-1;
    for(int i=0;i<DM_TC_MAX_TOUCH;i++)
        if(controls->finger[i].active&&controls->finger[i].target==DM_TC_SKILL1)skill_slot=i;
    CHECK(skill_slot>=0);
    uint64_t old_world=obs.world_epoch;
    f.area=2;f.ui_gen++;f.x=101;f.now=2050000000;
    CHECK(dm_live_touch_provider_observe(&provider,&obs));
    CHECK(obs.movement_continuity&&obs.world_epoch>old_world);
    dm_live_touch_set_context_moving(touch,0,(1u<<DM_TC_COUNT)-1,obs.scene_epoch,obs.world_epoch,obs.ui_identity,0,obs.movement_continuity);
    CHECK(dm_live_touch_active(touch)&&controls->lx==before_x&&controls->ly==before_y);
    CHECK(!controls->finger[skill_slot].active);
    CHECK(dm_live_touch_buttons(touch)==0);
    CHECK(!dm_live_touch_event(touch,DM_LIVE_ACTION_POINTER_UP,1,2,both_ids,both_x,both_y,
                              667,375,0,0,667,375,2055000000));
    CHECK(dm_live_touch_buttons(touch)==0&&controls->lx==before_x&&controls->ly==before_y);
    CHECK(dm_live_touch_event(touch,DM_LIVE_ACTION_UP,0,1,&id,&x,&y,667,375,0,0,667,375,2060000000));
    CHECK(!controls->lx&&!controls->ly&&!dm_live_touch_active(touch));

    // A teleport, stale observation, new player/game, or panel must never authorize carry.
    f.x+=100;f.now=2100000000;CHECK(dm_live_touch_provider_observe(&provider,&obs));CHECK(!obs.movement_continuity);
    f.now=3100000000;CHECK(dm_live_touch_provider_observe(&provider,&obs));CHECK(!obs.movement_continuity);
    old_world=obs.world_epoch;
    f.player_identity++;f.now+=10000000;CHECK(dm_live_touch_provider_observe(&provider,&obs));
    CHECK(!obs.movement_continuity&&obs.world_epoch>old_world);
    f.game_identity++;f.now+=10000000;CHECK(dm_live_touch_provider_observe(&provider,&obs));CHECK(!obs.movement_continuity);
    f.panel_open=1;f.now+=10000000;CHECK(dm_live_touch_provider_observe(&provider,&obs));CHECK(!obs.movement_continuity);
    f.panel_open=0;f.now+=10000000;CHECK(dm_live_touch_provider_observe(&provider,&obs));CHECK(!obs.movement_continuity);

    // Default callers still cancel on every identity change; continuity must be explicitly proven.
    dm_live_touch_set_context(touch,0,(1u<<DM_TC_COUNT)-1,obs.scene_epoch,obs.world_epoch,obs.ui_identity,0);
    x=70;y=300;CHECK(dm_live_touch_event(touch,DM_LIVE_ACTION_DOWN,0,1,&id,&x,&y,667,375,0,0,667,375,3200000000));
    dm_live_touch_set_context(touch,0,(1u<<DM_TC_COUNT)-1,obs.scene_epoch+1,obs.world_epoch,obs.ui_identity+1,0);
    CHECK(!dm_live_touch_active(touch));

    // The production default host path calls the real host entry points. These
    // stubs only stand in for that boundary in this host build.
    dm_live_touch_provider prod;
    dm_live_touch_provider_init(&prod, NULL);
    prod_ingame = 1; prod_hud_valid = 1; prod_ui_gen = 9; prod_now = 1800000000;
    dm_live_touch_scene_observation pobs;
    CHECK(dm_live_touch_provider_observe(&prod, &pobs));
    CHECK(pobs.available && pobs.scene == DM_LIVE_TOUCH_SCENE_GAME && pobs.ui_identity == 9);
    // Actual original panel stack transition: inventory open/close must move
    // the production default observation into/out of PANEL, not rely on the
    // unset front-end dm_uiscreen_panel flag.
    prod_panel_stack = 1; prod_now = 1810000000;
    CHECK(dm_live_touch_provider_observe(&prod, &pobs));
    CHECK(pobs.available && pobs.scene == DM_LIVE_TOUCH_SCENE_PANEL);
    prod_panel_stack = 0; prod_now = 1820000000;
    CHECK(dm_live_touch_provider_observe(&prod, &pobs));
    CHECK(pobs.available && pobs.scene == DM_LIVE_TOUCH_SCENE_GAME);
    prod_ui_gen = 0;
    CHECK(!dm_live_touch_provider_observe(&prod, &pobs));
    prod_ui_gen = 9; prod_hud_valid = 0;
    CHECK(!dm_live_touch_provider_observe(&prod, &pobs));
    CHECK(dm_live_touch_provider_generic_interface(&prod) == NULL);

    dm_live_touch_destroy(touch);
    puts("PASS: nearby same-player movement survives area changes; skills, unknown identity and new sessions cancel safely");
    return 0;
}
