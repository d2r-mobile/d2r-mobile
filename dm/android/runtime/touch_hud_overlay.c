#include "touch_hud_overlay.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const float COLOR_NORMAL[4] = {0.15f, 0.65f, 0.95f, 0.30f};
static const float COLOR_PRESSED[4] = {0.95f, 0.35f, 0.15f, 0.60f};
static const float COLOR_UNAVAILABLE[4] = {0.45f, 0.45f, 0.45f, 0.16f};
static const float COLOR_JOY[4] = {0.15f, 0.85f, 0.55f, 0.22f};

static void blend_pixel(uint8_t *bits, uint32_t stride, uint32_t width, uint32_t height,
                        int32_t x, int32_t y, const float c[4]) {
    if (x < 0 || y < 0 || (uint32_t)x >= width || (uint32_t)y >= height) return;
    uint8_t *p = bits + ((size_t)y * stride + (uint32_t)x) * 4u;
    double a = c[3];
    p[0] = (uint8_t)(c[0] * a * 255.0 + p[0] * (1.0 - a) + 0.5);
    p[1] = (uint8_t)(c[1] * a * 255.0 + p[1] * (1.0 - a) + 0.5);
    p[2] = (uint8_t)(c[2] * a * 255.0 + p[2] * (1.0 - a) + 0.5);
    p[3] = 255;
}

// Clip in double space before any integer conversion. cx/cy may be legitimate
// finite values far outside the view; they must not become huge int casts.
static void draw_disk(uint8_t *bits, uint32_t stride, uint32_t width, uint32_t height,
                      double cx, double cy, double r, const float c[4]) {
    if (!(r > 0.0) || !isfinite(r) || !isfinite(cx) || !isfinite(cy)) return;
    double x0d = floor(cx - r), x1d = ceil(cx + r);
    double y0d = floor(cy - r), y1d = ceil(cy + r);
    if (!isfinite(x0d) || !isfinite(x1d) || !isfinite(y0d) || !isfinite(y1d)) return;
    if (x1d < 0.0 || y1d < 0.0 || x0d >= (double)width || y0d >= (double)height) return;
    if (x0d < 0.0) x0d = 0.0;
    if (y0d < 0.0) y0d = 0.0;
    if (x1d > (double)(width - 1u)) x1d = (double)(width - 1u);
    if (y1d > (double)(height - 1u)) y1d = (double)(height - 1u);
    int32_t x0 = (int32_t)x0d, x1 = (int32_t)x1d;
    int32_t y0 = (int32_t)y0d, y1 = (int32_t)y1d;
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            double dx = (double)x + 0.5 - cx, dy = (double)y + 0.5 - cy;
            if (dx * dx + dy * dy <= r * r) blend_pixel(bits, stride, width, height, x, y, c);
        }
    }
}

static int overlay_valid(uint8_t *bits, uint32_t width, uint32_t height,
                         uint32_t stride, size_t capacity) {
    if (!bits || !width || !height || stride < width) return 0;
    if ((uint64_t)stride * (uint64_t)height > (uint64_t)SIZE_MAX / 4u) return 0;
    return capacity >= (size_t)stride * (size_t)height * 4u;
}

int dm_touch_hud_overlay_draw(const dm_touch_hud_snapshot *s,
                              uint8_t *bits, uint32_t width, uint32_t height,
                              uint32_t stride, size_t capacity) {
    if (!s || !overlay_valid(bits, width, height, stride, capacity)) return 0;
    dm_touch_hud_geometry g[DM_TC_COUNT];
    dm_touch_hud_joy_geometry joy;
    if (!dm_touch_hud_build_geometry(s, g, &joy)) return 0;
    double density = s->density;
    for (int i = 0; i < DM_TC_COUNT; ++i) {
        if (!(s->enabled_mask & (1u << i))) continue;
        int pressed = (s->pressed_mask & (1u << i)) != 0;
        int available = (s->available_mask & (1u << i)) != 0;
        draw_disk(bits, stride, width, height, g[i].center_x_px, g[i].center_y_px, g[i].radius_px,
                  pressed ? COLOR_PRESSED : available ? COLOR_NORMAL : COLOR_UNAVAILABLE);
    }
    if (s->joystick_active) {
        draw_disk(bits, stride, width, height, s->joy_origin_x_pt * density,
                  s->joy_origin_y_pt * density, joy.radius_px, COLOR_JOY);
        draw_disk(bits, stride, width, height, s->joy_current_x_pt * density,
                  s->joy_current_y_pt * density, joy.radius_px * 0.42, COLOR_PRESSED);
    } else {
        draw_disk(bits, stride, width, height, joy.center_x_px, joy.center_y_px, joy.radius_px, COLOR_JOY);
    }
    return 1;
}

void dm_touch_hud_schedule_init(dm_touch_hud_schedule *schedule) {
    if (!schedule) return;
    schedule->dirty = 0;
    schedule->draws = 0;
    schedule->retries = 0;
}

void dm_touch_hud_schedule_dirty(dm_touch_hud_schedule *schedule) {
    if (schedule) schedule->dirty = 1;
}

int dm_touch_hud_schedule_should_draw(const dm_touch_hud_schedule *schedule, int ready) {
    return schedule && schedule->dirty && ready;
}

void dm_touch_hud_schedule_draw_done(dm_touch_hud_schedule *schedule, int success) {
    if (!schedule) return;
    if (success) {
        schedule->dirty = 0;
        schedule->draws++;
    } else {
        schedule->retries++;
    }
}

void dm_touch_hud_schedule_suppress(dm_touch_hud_schedule *schedule) {
    if (schedule) schedule->dirty = 0;
}

void dm_touch_hud_frame_init(dm_touch_hud_frame *frame) {
    if (!frame) return;
    memset(frame, 0, sizeof *frame);
    dm_touch_hud_schedule_init(&frame->schedule);
}

void dm_touch_hud_frame_dirty(dm_touch_hud_frame *frame) {
    if (frame) dm_touch_hud_schedule_dirty(&frame->schedule);
}

void dm_touch_hud_frame_suppress(dm_touch_hud_frame *frame) {
    if (!frame) return;
    dm_touch_hud_schedule_suppress(&frame->schedule);
    frame->open = 0;
    frame->composed_ok = 0;
}

int dm_touch_hud_frame_timer_ready(const dm_touch_hud_frame *frame, int ready) {
    return frame && ready && frame->schedule.dirty;
}

int dm_touch_hud_frame_begin(dm_touch_hud_frame *frame, int external_redraw,
                             int timer_tick, int ready, int hud_valid) {
    if (!frame || !ready || !hud_valid) return 0;
    if (!external_redraw && !(timer_tick && frame->schedule.dirty)) return 0;
    frame->open = 1;
    frame->composed_ok = 0;
    frame->external = external_redraw != 0;
    frame->frames++;
    return 1;
}

void dm_touch_hud_frame_composed(dm_touch_hud_frame *frame, int success) {
    if (!frame || !frame->open) return;
    frame->composed_ok = success != 0;
}

int dm_touch_hud_frame_complete(dm_touch_hud_frame *frame, int post_success) {
    if (!frame || !frame->open) return 0;
    int ack = frame->composed_ok && post_success;
    if (ack) {
        if (frame->schedule.dirty) frame->schedule.draws++;
        frame->schedule.dirty = 0;
    } else {
        frame->schedule.dirty = 1;
        frame->schedule.retries++;
    }
    frame->open = 0;
    frame->composed_ok = 0;
    return ack;
}
