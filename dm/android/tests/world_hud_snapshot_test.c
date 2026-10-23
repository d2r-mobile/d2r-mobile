#include "world_hud_snapshot.h"
#include <assert.h>
#include <stdio.h>

static dm_rift_hud_t rift;
static dm_map_goal_view goals;
static int active = 1, mode = 2, have_mini = 1, calls;
static int hidden;
int dm_ui_map_is_hidden(void) { return hidden; }
unsigned dm_ui_map_title_revision(void) { return 7; }
int dm_rift_hud(dm_rift_hud_t *out) { calls++; *out = rift; return active; }
int dm_map_goals(dm_map_goal_view *out) { *out = goals; return goals.n; }
int dm_ui_map_geom(float *cell) { *cell = 2; return mode; }
int dm_ui_minimap_rect(float out[4]) {
    const float rect[] = {.8f, .1f, .98f, .3f}; memcpy(out, rect, sizeof rect); return have_mini;
}
int dm_screen_real(int *w, int *h) { *w = 1280; *h = 720; return 1; }
int dm_lang_group(void) { return 1; }
int dm_lang_index(void) { return 8; }
int dm_custom_get(const char *key) { return !strcmp(key, "item_notes"); }

int main(void) {
    float out[DM_WORLD_HUD_VALUES];
    hidden=1;dm_world_hud_capture(out,1);assert(out[70]==1&&out[71]==7&&out[72]==.8f);hidden=0;calls=0;
    rift = (dm_rift_hud_t){.tier=99,.progress=.75f,.secs_left=42,.guardian=1,.next=100,.goal=2,.gdead=1,.gtotal=2};
    goals = (dm_map_goal_view){.n=2,.px=100,.py=200,.sx=.5f,.sy=.5f,.x={102,104},.y={203,205},.art={3,66}};
    dm_world_hud_capture(out, 1);
    assert(out[0] == 1 && out[1] == 99 && out[2] == .75f && out[3] == 42 && out[7] == 100);
    assert(out[9] == 2 && out[10] == 1 && out[11] == 2);
    assert(out[16] == 2 && out[18] == 1280 && out[25] == 2 && out[31] == 1);
    assert(out[32] == 102 && out[33] == 203 && out[34] == 3 && out[37] == 66);
    dm_world_hud_capture(out, 0);
    for (unsigned i = 0; i < DM_WORLD_HUD_VALUES; i++) assert(out[i] == 0);
    assert(calls == 1);
    mode = 1; have_mini = 0; dm_world_hud_capture(out, 1);
    assert(out[0] == 1 && out[16] == 0 && out[25] == 0);
    have_mini = 1; dm_world_hud_capture(out, 1);
    assert(out[16] == 1 && out[20] == 1 && out[21] == .8f && out[25] == 2);
    active = 0; goals.n = 0; dm_world_hud_capture(out, 1);
    assert(out[0] == 0 && out[16] == 0);
    goals.n = 13; dm_world_hud_capture(out, 1); assert(out[25] == 0);
    goals.n = 12; dm_world_hud_capture(out, 1); assert(out[25] == 12);
    puts("PASS world HUD: real getter values, bounded goals, navigation clearing and minimap readiness");
}
