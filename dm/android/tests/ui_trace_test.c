#include "../../host/ui_trace.h"
#include <stdio.h>

int main(void) {
    dm_ui_trace_state s = {0};
    if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 1; // opt-out silent
    dm_ui_trace_reset(&s, 1);
    for (int i = 0; i < 32; i++) if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 2;
    if (dm_ui_trace_take_stage(&s, DM_UI_TRACE_TOUCH)) return 3; // stage cap
    if (!dm_ui_trace_take_stage(&s, DM_UI_TRACE_PUMP)) return 4; // independent stage
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
    int last_phase = -1;
    if (!dm_ui_trace_pump_take(&s, &last_phase, 2)) return 10;
    for (int i = 0; i < 100; i++) if (dm_ui_trace_pump_take(&s, &last_phase, 2)) return 11;
    if (!dm_ui_trace_pump_take(&s, &last_phase, 3)) return 12;
    puts("PASS: ui trace opt-out, per-stage cap, long-hold terminal receipts, 100-call cancel bound");
    return 0;
}
