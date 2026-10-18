// #121 CPU window seam: actual bridge MotionEvents -> read-only observation ->
// production HUD conversion -> platform-neutral bounded CPU overlay. No Vulkan,
// Android, game or device. Includes stride/padding, alpha, density, cancel,
// resize, scheduler coalescing/retry and a finite 1e30 clipping regression.
#include "touch_bridge.h"
#include "touch_hud.h"
#include "touch_hud_overlay.h"
#include "dm_touch_controls.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } checks++; } while (0)

static int event(dm_touch_bridge *b, dm_touch_action action, int32_t id,
                 float x, float y, int64_t t) {
    dm_touch_event e;
    memset(&e, 0, sizeof e);
    e.action = action; e.pointer_count = 1;
    e.points[0] = (dm_touch_point){id, x, y}; e.time_ns = t;
    return dm_touch_bridge_event(b, &e);
}

static uint8_t *buffer_new(uint32_t width, uint32_t height, uint32_t stride) {
    (void)width;
    size_t cap = (size_t)stride * height * 4u;
    uint8_t *p = malloc(cap);
    if (p) memset(p, 0x40, cap);
    return p;
}

static uint32_t pixel(const uint8_t *p, uint32_t stride, uint32_t x, uint32_t y) {
    const uint8_t *q = p + ((size_t)y * stride + x) * 4u;
    return (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
}

static int padding_untouched(const uint8_t *p, uint32_t width, uint32_t height, uint32_t stride) {
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = p + (size_t)y * stride * 4u;
        for (uint32_t x = width; x < stride; ++x)
            for (int c = 0; c < 4; ++c) if (row[x * 4u + c] != 0x40) return 0;
    }
    return 1;
}

static uint32_t expected_blend(uint8_t bg, float r, float g, float b, float a) {
    uint8_t rr = (uint8_t)(r * a * 255.0f + (float)bg * (1.0f - a) + 0.5f);
    uint8_t gg = (uint8_t)(g * a * 255.0f + (float)bg * (1.0f - a) + 0.5f);
    uint8_t bb = (uint8_t)(b * a * 255.0f + (float)bg * (1.0f - a) + 0.5f);
    return (uint32_t)rr | ((uint32_t)gg << 8) | ((uint32_t)bb << 16) | 0xff000000u;
}

static int observe_snapshot(dm_touch_bridge *b, float density, uint32_t w, uint32_t h,
                            dm_touch_hud_snapshot *s) {
    dm_android_touch_observation o;
    if (!dm_touch_bridge_observe(b, density, &o)) return 0;
    return dm_touch_hud_snapshot_from_observation(&o, w, h, s);
}

int main(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2);
    CHECK(b);
    dm_tc_layout layout; dm_tc_layout_make(&layout, 667, 375);
    uint32_t width = 1334, height = 750, stride = 1340;
    uint8_t *bits = buffer_new(width, height, stride);
    CHECK(bits);
    dm_touch_hud_snapshot s;
    CHECK(observe_snapshot(b, 2, width, height, &s));
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    CHECK(padding_untouched(bits, width, height, stride));

    float main_x = layout.target[DM_TC_MAIN].x * 2.0f;
    float main_y = layout.target[DM_TC_MAIN].y * 2.0f;
    uint32_t unavailable = pixel(bits, stride, (uint32_t)main_x, (uint32_t)main_y);
    CHECK((unavailable >> 24) == 255u);   // visible alpha was written
    CHECK(unavailable != 0x40404040u);    // real overlay pixels, not untouched sentinel

    /* Actual DOWN pressed feedback and cancel clearing (negative stale control). */
    CHECK(event(b, DM_TOUCH_ACTION_DOWN, 7, main_x, main_y, 21000000000));
    CHECK(observe_snapshot(b, 2, width, height, &s));
    CHECK(s.pressed_mask == (1u << DM_TC_MAIN));
    memset(bits, 0x20, (size_t)stride * height * 4u);
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    uint32_t pressed = pixel(bits, stride, (uint32_t)main_x, (uint32_t)main_y);
    CHECK(pressed != unavailable);        // pressed diagnostic differs from unavailable
    CHECK(pressed == expected_blend(0x20, 0.95f, 0.35f, 0.15f, 0.60f));

    /* Production frame orchestration: every unchanged external redraw must
       compose HUD even when dirty is clear; fresh baseline is independent. */
    dm_touch_hud_frame frame; dm_touch_hud_frame_init(&frame);
    dm_touch_hud_frame_dirty(&frame);
    CHECK(dm_touch_hud_frame_begin(&frame, 0, 1, 1, 1));
    dm_touch_hud_frame_composed(&frame, 1);
    CHECK(dm_touch_hud_frame_complete(&frame, 1));
    CHECK(!frame.schedule.dirty);
    for (int redraw = 0; redraw < 2; ++redraw) {
        memset(bits, 0x40, (size_t)stride * height * 4u);
        CHECK(dm_touch_hud_frame_begin(&frame, 1, 0, 1, 1)); // dirty is clear
        CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                        (size_t)stride * height * 4u) == 1);
        dm_touch_hud_frame_composed(&frame, 1);
        CHECK(dm_touch_hud_frame_complete(&frame, 1));
        CHECK(pixel(bits, stride, (uint32_t)main_x, (uint32_t)main_y) ==
              expected_blend(0x40, 0.95f, 0.35f, 0.15f, 0.60f));
        CHECK(padding_untouched(bits, width, height, stride));
    }

    /* External redraw can fail before begin (GPU preparation/window lock).
       NativeActivity marks the request dirty before those fallible operations. */
    dm_touch_hud_frame early; dm_touch_hud_frame_init(&early);
    dm_touch_hud_frame_dirty(&early);
    CHECK(!early.open && dm_touch_hud_frame_timer_ready(&early, 1));
    CHECK(!dm_touch_hud_frame_complete(&early, 0));
    CHECK(dm_touch_hud_frame_timer_ready(&early, 1));
    CHECK(dm_touch_hud_frame_begin(&early, 0, 1, 1, 1));
    dm_touch_hud_frame_composed(&early, 1);
    CHECK(dm_touch_hud_frame_complete(&early, 1));
    CHECK(!dm_touch_hud_frame_timer_ready(&early, 1));

    /* Post failure must retain retry; a later successful post clears dirty once. */
    dm_touch_hud_frame_dirty(&frame);
    CHECK(dm_touch_hud_frame_begin(&frame, 0, 1, 1, 1));
    memset(bits, 0x40, (size_t)stride * height * 4u);
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    dm_touch_hud_frame_composed(&frame, 1);
    CHECK(!dm_touch_hud_frame_complete(&frame, 0));
    CHECK(frame.schedule.dirty && frame.schedule.retries == 1);
    CHECK(dm_touch_hud_frame_begin(&frame, 0, 1, 1, 1));
    memset(bits, 0x40, (size_t)stride * height * 4u);
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    dm_touch_hud_frame_composed(&frame, 1);
    CHECK(dm_touch_hud_frame_complete(&frame, 1));
    CHECK(!frame.schedule.dirty && frame.schedule.draws == 2);

    /* Readiness gates actual dispatch; dirty survives a not-ready callback. */
    dm_touch_hud_frame_dirty(&frame);
    CHECK(!dm_touch_hud_frame_timer_ready(&frame, 0));
    CHECK(!dm_touch_hud_frame_begin(&frame, 1, 0, 0, 1));
    CHECK(!dm_touch_hud_frame_begin(&frame, 1, 0, 1, 0));
    CHECK(frame.schedule.dirty);
    dm_touch_hud_frame_suppress(&frame);
    CHECK(!dm_touch_hud_frame_timer_ready(&frame, 1));

    CHECK(dm_touch_bridge_cancel_all(b, 21001000000));
    CHECK(observe_snapshot(b, 2, width, height, &s));
    CHECK(s.pressed_mask == 0 && !s.joystick_active);
    memset(bits, 0x40, (size_t)stride * height * 4u);
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    uint32_t normal = pixel(bits, stride, (uint32_t)main_x, (uint32_t)main_y);
    CHECK(normal != pressed);             // a no-op/stale overlay would leave pressed here
    CHECK(normal == expected_blend(0x40, 0.45f, 0.45f, 0.45f, 0.16f));
    CHECK(padding_untouched(bits, width, height, stride));

    /* Actual joystick observation drives origin/current, no duplicated hit rule. */
    CHECK(event(b, DM_TOUCH_ACTION_DOWN, 8, 180, 550, 21002000000));
    CHECK(observe_snapshot(b, 2, width, height, &s));
    CHECK(s.joystick_active && s.joy_origin_x_pt == 90 && s.joy_current_x_pt == 90);
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    uint32_t rest = pixel(bits, stride, 180, 550);
    CHECK(event(b, DM_TOUCH_ACTION_MOVE, 8, 300, 550, 21003000000));
    CHECK(observe_snapshot(b, 2, width, height, &s));
    CHECK(s.joy_current_x_pt == 150);
    CHECK(dm_touch_hud_overlay_draw(&s, bits, width, height, stride,
                                    (size_t)stride * height * 4u) == 1);
    CHECK(pixel(bits, stride, 300, 550) != rest); // live thumb moved
    CHECK(dm_touch_bridge_cancel_all(b, 21004000000));

    /* Density 3 conversion and overlay use the same frozen centers x density. */
    dm_touch_bridge *b3 = dm_touch_bridge_create(667, 375, 3);
    CHECK(b3);
    uint32_t w3 = 2001, h3 = 1125, st3 = 2008;
    uint8_t *bits3 = buffer_new(w3, h3, st3);
    CHECK(bits3);
    CHECK(observe_snapshot(b3, 3, w3, h3, &s));
    CHECK(s.density == 3);
    CHECK(dm_touch_hud_overlay_draw(&s, bits3, w3, h3, st3,
                                    (size_t)st3 * h3 * 4u) == 1);
    CHECK(pixel(bits3, st3, (uint32_t)(layout.target[DM_TC_MAIN].x * 3.0f),
                (uint32_t)(layout.target[DM_TC_MAIN].y * 3.0f)) != 0x40404040u);
    CHECK(padding_untouched(bits3, w3, h3, st3));

    /* Actual resize re-observes the new layout extent; no stale pressed state. */
    CHECK(dm_touch_bridge_resize(b, 812, 375, 21005000000));
    CHECK(observe_snapshot(b, 2, 1624, 750, &s));
    CHECK(s.width_pt == 812 && s.pressed_mask == 0);
    uint32_t wr = 1624, hr = 750, sr = 1632;
    uint8_t *bitsr = buffer_new(wr, hr, sr);
    CHECK(bitsr);
    CHECK(dm_touch_hud_overlay_draw(&s, bitsr, wr, hr, sr,
                                    (size_t)sr * hr * 4u) == 1);
    dm_tc_layout wide; dm_tc_layout_make(&wide, 812, 375);
    CHECK(wide.target[DM_TC_MAIN].x != layout.target[DM_TC_MAIN].x);
    CHECK(pixel(bitsr, sr, (uint32_t)(wide.target[DM_TC_MAIN].x * 2.0f),
                (uint32_t)(wide.target[DM_TC_MAIN].y * 2.0f)) != 0x40404040u);
    CHECK(padding_untouched(bitsr, wr, hr, sr));

    /* Finite 1e30 joystick coordinates must clip in double, never cast unbounded.
       Run this target under UBSAN/float-cast-overflow as an explicit regression. */
    CHECK(observe_snapshot(b, 2, wr, hr, &s));
    s.joystick_active = 1;
    s.joy_origin_x_pt = 1e30f; s.joy_origin_y_pt = 1e30f;
    s.joy_current_x_pt = 1e30f; s.joy_current_y_pt = 1e30f;
    CHECK(dm_touch_hud_validate(&s));
    uint8_t *huge = buffer_new(wr, hr, sr);
    CHECK(huge);
    CHECK(dm_touch_hud_overlay_draw(&s, huge, wr, hr, sr,
                                    (size_t)sr * hr * 4u) == 1);
    CHECK(padding_untouched(huge, wr, hr, sr));

    /* Invalid dimensions/stride/capacity/NULL return 0 with output untouched. */
    uint8_t sentinel[64]; memset(sentinel, 0x5a, sizeof sentinel);
    uint8_t before[64]; memcpy(before, sentinel, sizeof sentinel);
    CHECK(!dm_touch_hud_overlay_draw(&s, NULL, 4, 4, 4, 64));
    CHECK(!dm_touch_hud_overlay_draw(&s, sentinel, 0, 4, 4, 64));
    CHECK(!dm_touch_hud_overlay_draw(&s, sentinel, 4, 4, 3, 64));
    CHECK(!dm_touch_hud_overlay_draw(&s, sentinel, 4, 4, 4, 63));
    CHECK(memcmp(sentinel, before, sizeof sentinel) == 0);

    /* Production scheduler: coalesce dirty, retry after failure, stop when not ready. */
    dm_touch_hud_schedule sc; dm_touch_hud_schedule_init(&sc);
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 1));
    dm_touch_hud_schedule_dirty(&sc); dm_touch_hud_schedule_dirty(&sc);
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 0));   // pause/focus/windowloss/late callback
    CHECK(dm_touch_hud_schedule_should_draw(&sc, 1));
    dm_touch_hud_schedule_draw_done(&sc, 0);   // failed draw keeps needed retry
    CHECK(sc.dirty && sc.retries == 1 && dm_touch_hud_schedule_should_draw(&sc, 1));
    dm_touch_hud_schedule_draw_done(&sc, 1);
    CHECK(!sc.dirty && sc.draws == 1);         // one coalesced draw for the tick
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 1));
    /* Not-ready callbacks retain pending work for a later ready tick. */
    dm_touch_hud_schedule_dirty(&sc);
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 0));
    CHECK(sc.dirty);
    dm_touch_hud_schedule_draw_done(&sc, 1);
    /* Permanent format/extent suppression drops work instead of looping. */
    dm_touch_hud_schedule_dirty(&sc);
    dm_touch_hud_schedule_suppress(&sc);
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 1));
    /* Stop/destroy late callback cannot draw. */
    dm_touch_hud_schedule_dirty(&sc);
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 0));
    dm_touch_hud_schedule_suppress(&sc);
    CHECK(!dm_touch_hud_schedule_should_draw(&sc, 1));

    free(bits); free(bits3); free(bitsr); free(huge);
    dm_touch_bridge_destroy(b, NULL); dm_touch_bridge_destroy(b3, NULL);
    printf("touch HUD CPU seam: %d checks\n", checks);
    return 0;
}
