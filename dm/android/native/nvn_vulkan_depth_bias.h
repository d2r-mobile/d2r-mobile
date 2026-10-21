#pragma once

// NVN 多边形偏移按图元类型分别启用（POINT=1、LINE=2、FILL=4）；本后端只画填充三角形，
// 与原 Metal（nvn_mtl.m：offset_en & 4 才设 depthBias/slope/clamp）一致，只认 FILL 位，
// 并在设备支持 depthBiasClamp 时传递 NVN 的 clamp，而不是恒为 0。
// NVN enables polygon offset per primitive kind (POINT=1, LINE=2, FILL=4); this backend draws only filled
// triangles, so like the original Metal (nvn_mtl.m sets depthBias/slope/clamp only when offset_en & 4) only
// the FILL bit counts, and NVN's clamp is forwarded when the device supports depthBiasClamp instead of always 0.
#define NVN_VK_POLYGON_OFFSET_FILL 4u

typedef struct { int enable; float constant, clamp, slope; } nvn_vk_depth_bias;

static inline int nvn_vk_depth_bias_enabled(unsigned offset_enables) {
    return (offset_enables & NVN_VK_POLYGON_OFFSET_FILL) != 0;
}

static inline nvn_vk_depth_bias nvn_vk_depth_bias_values(unsigned offset_enables, float units, float factor,
                                                         float clamp, int clamp_supported) {
    nvn_vk_depth_bias bias = {0, 0.0f, 0.0f, 0.0f};
    if (!nvn_vk_depth_bias_enabled(offset_enables)) return bias;
    bias.enable = 1;
    bias.constant = units;
    bias.slope = factor;
    bias.clamp = clamp_supported ? clamp : 0.0f;
    return bias;
}
