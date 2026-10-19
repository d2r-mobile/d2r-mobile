#include "../../host/ui_trace.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    dm_ui_trace_state s = {0};
    if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 1; // opt-out silent
    dm_ui_trace_reset(&s, 1);
    for (int i = 0; i < 32; i++) if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 2;
    if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 3; // stage cap
    if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_PUMP)) return 4; // independent stage
    if (dm_ui_trace_arm(&s, 1) != 1) return 13;
    unsigned rearm_generation = 0;
    if (!dm_ui_trace_rearm_generation(&rearm_generation, 1)) return 24;
    if (dm_ui_trace_rearm_generation(&rearm_generation, 1)) return 25; // frontend sync unchanged
    if (!dm_ui_trace_rearm_generation(&rearm_generation, 2)) return 26; // frontend sync new
    for (int i = 0; i < 32; i++) if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 14;
    if (dm_ui_trace_arm(&s, 1) != 0) return 15; // unchanged generation never rearms
    if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 16;
    if (dm_ui_trace_arm(&s, 2) != 1) return 17; // new generation resets
    if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 18;
    if (dm_ui_trace_arm(&s, 2) != 0) return 19; // repeated new generation is not per-event
    dm_ui_trace_reset(&s, 1); // phase-3 cancel/reset starts a fresh bounded window
    if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_CANCEL)) return 5;
    dm_ui_trace_reset(&s, 0);
    if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_CANCEL)) return 6;
    dm_ui_trace_reset(&s, 1);
    int cancel_logs = 0;
    for (int i = 0; i < 100; i++) if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_CANCEL)) cancel_logs++;
    if (cancel_logs != 32) return 7; // real cancellation gate stays bounded
    for (int i = 0; i < 100; i++) if (dm_ui_trace_touch_take(&s, 1)) return 8;
    if (!dm_ui_trace_touch_take(&s, 2) || !dm_ui_trace_touch_take(&s, 3)) return 9;
    _Atomic int last_phase = -1;
    if (!dm_ui_trace_pump_take(&s, &last_phase, 2)) return 10;
    for (int i = 0; i < 100; i++) if (dm_ui_trace_pump_take(&s, &last_phase, 2)) return 11;
    if (!dm_ui_trace_pump_take(&s, &last_phase, 3)) return 12;
    const char *old_name = "PreviousButton";
    if (strcmp(dm_ui_trace_snap_name(0, old_name), "unknown")) return 20;
    if (strcmp(dm_ui_trace_snap_name(6, old_name), "unknown")) return 21;
    if (strcmp(dm_ui_trace_snap_name(1, old_name), old_name)) return 22;
    if (strcmp(dm_ui_trace_snap_name(2, old_name), old_name)) return 23;
    puts("PASS: ui trace opt-out, per-stage cap, long-hold terminal receipts, 100-call cancel bound, stale snap name unknown");
    return 0;
}
