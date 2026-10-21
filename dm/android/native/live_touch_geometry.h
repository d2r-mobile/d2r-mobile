// Actual rendered-picture geometry for Android touch forwarding.
// All pages fit the actual game output aspect (explicit 16:9, phone aspect, capped
// ultrawide), never blindly stretched to the surface. Invalid surfaces or
// output sizes must not produce a fake picture.
#pragma once
#include <stdint.h>

typedef struct {
    int valid;
    float left, top, width, height;
    float logical_width, logical_height;
} dm_live_touch_geometry;

static inline dm_live_touch_geometry dm_live_touch_geometry_for(int surface_w, int surface_h,
                                                                 int output_w, int output_h,
                                                                 int ingame) {
    dm_live_touch_geometry g = {0};
    (void)ingame; // Scene changes do not change the rendered output rectangle.
    if (surface_w < 320 || surface_h < 240 || surface_w <= 0 || surface_h <= 0 ||
        surface_w > 16384 || surface_h > 16384) return g;
    float sw = (float)surface_w, sh = (float)surface_h;
    if (output_w > 0 && output_h > 0) {
        // Fit the actual output aspect into the visible surface.
        float ow = (float)output_w, oh = (float)output_h;
        float sx = sw / ow, sy = sh / oh;
        float scale = sx < sy ? sx : sy;
        g.width = ow * scale;
        g.height = oh * scale;
    } else {
        // Compatibility fallback when output dimensions are unavailable.
        g.width = sw;
        g.height = sw * 9.0f / 16.0f;
        if (g.height > sh) {
            g.height = sh;
            g.width = sh * 16.0f / 9.0f;
        }
    }
    g.left = (sw - g.width) * 0.5f;
    g.top = (sh - g.height) * 0.5f;
    g.logical_width = 667.0f;
    g.logical_height = g.logical_width * g.height / g.width;
    g.valid = g.width > 0 && g.height > 0 && g.logical_height > 0;
    return g;
}
