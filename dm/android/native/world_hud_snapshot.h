#pragma once
#include "../../host/dm_rift.h"
#include "../../host/dm_world.h"
#include "../../host/dm_cursor.h"
#include "../../host/dm_uiscreen.h"
#include "../../host/dm_i18n.h"
#include <string.h>

enum { DM_WORLD_HUD_VALUES = 80 };
int dm_custom_get(const char *key);

static inline void dm_world_hud_capture(float out[DM_WORLD_HUD_VALUES], int visible) {
    memset(out, 0, DM_WORLD_HUD_VALUES * sizeof *out);
    if (!visible) return;
    dm_rift_hud_t r = {0};
    if (dm_rift_hud(&r)) {
        const float values[] = {1, r.tier, r.progress, r.secs_left, r.done, r.guardian,
            r.inc, r.next, r.overtime, r.goal, r.gdead, r.gtotal, r.wave, r.nwave};
        memcpy(out, values, sizeof values);
    }
    out[30] = dm_lang_group();
    out[31] = dm_lang_index() == 8 && dm_custom_get("item_notes") != 0;
    dm_map_goal_view m = {0};
    int n = dm_map_goals(&m), w = 0, h = 0;
    float cell = 0, mini[4] = {0};
    int mode = dm_ui_map_geom(&cell), have_mini = dm_ui_minimap_rect(mini);
    if (n <= 0 || n > 12 || !dm_screen_real(&w, &h) || w <= 0 || h <= 0 ||
        !(cell > 0) || (mode != 2 && (mode != 1 || !have_mini))) return;
    out[16] = mode; out[17] = cell; out[18] = w; out[19] = h; out[20] = have_mini;
    memcpy(out + 21, mini, sizeof mini);
    out[25] = n; out[26] = m.px; out[27] = m.py; out[28] = m.sx; out[29] = m.sy;
    for (int i = 0; i < n; i++) {
        out[32 + i * 3] = m.x[i]; out[33 + i * 3] = m.y[i]; out[34 + i * 3] = m.art[i];
    }
}
