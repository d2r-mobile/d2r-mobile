#include "view_policy.h"

#include <math.h>

void dm_view_policy_init(dm_view_policy *policy) {
    if (!policy) return;
    policy->view_width_px = 667;
    policy->view_height_px = 375;
    policy->buffer_width_px = 0;
    policy->buffer_height_px = 0;
    policy->buffer_generation = 0;
    policy->density = 1.0f;
    policy->view_width_pt = 667.0f;
    policy->view_height_pt = 375.0f;
}

int dm_view_policy_set_view(dm_view_policy *policy, uint32_t width_px,
                            uint32_t height_px, float density) {
    if (!policy || width_px == 0 || height_px == 0 || !isfinite(density) ||
        density <= 0.0f) return 0;
    policy->view_width_px = width_px;
    policy->view_height_px = height_px;
    policy->density = density;
    policy->view_width_pt = (float)width_px / density;
    policy->view_height_pt = (float)height_px / density;
    return 1;
}

int dm_view_policy_set_buffer(dm_view_policy *policy, uint32_t width_px,
                              uint32_t height_px) {
    if (!policy || width_px == 0 || height_px == 0) return 0;
    policy->buffer_width_px = width_px;
    policy->buffer_height_px = height_px;
    policy->buffer_generation++;
    return 1;
}
