#include "live_touch_glyph.h"
#include "../../host/dm_input.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_glyph_lock = PTHREAD_MUTEX_INITIALIZER;
static dm_live_touch_glyph g_glyph_records[5];
static uint64_t g_glyph_signature;
static int g_glyph_valid;

static dm_ta_glyph shown(const dm_tg_snapshot *s, uint64_t button,
                         uint64_t now, int set) {
    dm_ta_glyph g = dm_ta_current_set(s, button, now, set);
    // Visual-only stale policy from DmTouchOverlay.m: keep drawing the last
    // captured glyph when a live snapshot merely aged out. Scene changes clear
    // captured_ns in the bridge and still produce the unknown placeholder.
    if (g.status == DM_TA_UNKNOWN && s && s->captured_ns && now > s->captured_ns &&
        now - s->captured_ns > DM_TG_MAX_AGE_NS)
        g = dm_ta_current_set(s, button, s->captured_ns, set);
    return g;
}

static void compute_glyphs(dm_live_touch *touch, const dm_tg_snapshot *snapshot,
                           uint64_t host_now_ns, dm_live_touch_glyph out[5]) {
    for (int i = 0; i < 5; ++i)
        out[i] = (dm_live_touch_glyph){DM_LIVE_GLYPH_UNKNOWN, -1, -1, -1, UINT32_MAX};
    if (!touch || !snapshot) return;
    int set = dm_live_touch_skill_set(touch);
    for (int i = 0; i < 5; ++i) {
        uint64_t button = dm_live_touch_target_button(touch, DM_TC_MAIN + i);
        if (!button) continue;
        dm_ta_glyph g = shown(snapshot, button, host_now_ns, set);
        out[i].status = g.status;
        out[i].atlas = g.atlas;
        out[i].normal_frame = g.normal_frame;
        out[i].pressed_frame = g.pressed_frame;
        out[i].raw_skill_id = g.raw_skill_id;
    }
}

static uint64_t glyph_hash(const dm_live_touch_glyph g[5]) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 5; ++i) {
        const uint64_t parts[] = {(uint64_t)(uint32_t)g[i].status,
            (uint64_t)(uint32_t)g[i].atlas, (uint64_t)(uint32_t)g[i].normal_frame,
            (uint64_t)(uint32_t)g[i].pressed_frame, (uint64_t)g[i].raw_skill_id};
        for (unsigned j = 0; j < sizeof parts / sizeof parts[0]; ++j)
            h = (h ^ parts[j]) * 1099511628211ull;
    }
    return h;
}

void dm_live_touch_glyph_publish(dm_live_touch *touch,
                                 const dm_tg_snapshot *snapshot,
                                 uint64_t host_now_ns) {
    dm_live_touch_glyph next[5];
    compute_glyphs(touch, snapshot, host_now_ns, next);
    uint64_t signature = glyph_hash(next);
    pthread_mutex_lock(&g_glyph_lock);
    memcpy(g_glyph_records, next, sizeof g_glyph_records);
    g_glyph_signature = signature;
    g_glyph_valid = 1;
    pthread_mutex_unlock(&g_glyph_lock);
}

int dm_live_touch_skill_glyphs(const dm_live_touch *touch,
                               const dm_tg_snapshot *snapshot,
                               uint64_t host_now_ns,
                               dm_live_touch_glyph out[5]) {
    (void)touch; (void)snapshot; (void)host_now_ns;
    if (!out) return 0;
    pthread_mutex_lock(&g_glyph_lock);
    int valid = g_glyph_valid;
    memcpy(out, g_glyph_records, sizeof g_glyph_records);
    pthread_mutex_unlock(&g_glyph_lock);
    return valid;
}

uint64_t dm_live_touch_glyph_signature(const dm_live_touch *touch,
                                       const dm_tg_snapshot *snapshot,
                                       uint64_t host_now_ns) {
    (void)touch; (void)snapshot; (void)host_now_ns;
    pthread_mutex_lock(&g_glyph_lock);
    uint64_t signature = g_glyph_valid ? g_glyph_signature : 0;
    pthread_mutex_unlock(&g_glyph_lock);
    return signature;
}

void dm_live_touch_glyph_invalidate(void) {
    dm_live_touch_glyph unknown[5];
    for (int i = 0; i < 5; ++i)
        unknown[i] = (dm_live_touch_glyph){DM_LIVE_GLYPH_UNKNOWN, -1, -1, -1, UINT32_MAX};
    pthread_mutex_lock(&g_glyph_lock);
    memcpy(g_glyph_records, unknown, sizeof g_glyph_records);
    g_glyph_valid = 0;
    g_glyph_signature = 0;
    pthread_mutex_unlock(&g_glyph_lock);
}
