#pragma once

#include <stdint.h>

// 深度比较采样（反射类型带 1024 位）落在颜色格式上时，Maxwell 按红通道比较；Vulkan 只对
// 带 SAMPLED_IMAGE_DEPTH_COMPARISON 的深度格式定义 Dref 结果，颜色格式结果未定义。
// 同位宽、同字节排布的单通道格式改用对应深度格式的别名图像采样；其余保持原格式。
// When a depth-compare sample (reflected type bit 1024) lands on a color format, Maxwell compares the red
// channel; Vulkan defines Dref results only for depth formats with SAMPLED_IMAGE_DEPTH_COMPARISON, and color
// formats are undefined. Single-channel formats with the same width and byte layout sample through an alias
// image in the matching depth format; everything else keeps its format.
#define TEXTURE_SHADOW_ALIAS_SERIAL (1ull << 62)

static inline int texture_shadow_alias_format(uint32_t type, int storage, int format) {
    if (storage || !(type & 1024u)) return 0;
    switch (format) {
        case 0x06: return 0x32;   // R16_UNORM → DEPTH16 | R16_UNORM → DEPTH16
        case 0x0a: return 0x34;   // R32_FLOAT → DEPTH32F | R32_FLOAT → DEPTH32F
        default: return 0;
    }
}

static inline uint64_t texture_shadow_alias_serial(uint64_t serial) {
    return serial | TEXTURE_SHADOW_ALIAS_SERIAL;
}
