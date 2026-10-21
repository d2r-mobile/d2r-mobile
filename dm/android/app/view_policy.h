// NativeActivity view-bound policy: input/render geometry is the real view,
// never the diagnostic Vulkan readback buffer. Host-testable and bounded.
#pragma once

#include <stdint.h>

typedef struct dm_view_policy {
    uint32_t view_width_px;
    uint32_t view_height_px;
    uint32_t buffer_width_px;
    uint32_t buffer_height_px;
    uint32_t buffer_generation;
    float density;
    float view_width_pt;
    float view_height_pt;
} dm_view_policy;

void dm_view_policy_init(dm_view_policy *policy);
// Sets the physical view bounds and density. Returns 0 for zero/non-finite
// values; on success recomputes logical point bounds.
int dm_view_policy_set_view(dm_view_policy *policy, uint32_t width_px,
                            uint32_t height_px, float density);
// Records the actual render buffer for diagnostics. It never changes the view
// bounds or logical input domain.
int dm_view_policy_set_buffer(dm_view_policy *policy, uint32_t width_px,
                              uint32_t height_px);
