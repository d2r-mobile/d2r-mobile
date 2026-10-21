// Final-present upscale filter policy. Pure capability decision, no device
// state: the caller supplies the advertised extension and the real source
// format's optimal-tiling features. Cubic is never claimed when either is
// missing, and LINEAR remains the fallback.
// MIT-licensed Android compatibility implementation.
// (MIT), reduced to this repository's existing device/present boundaries.
// 最终出图放大滤波策略。纯能力判断，不持有设备状态：调用方给出真实扩展与
// 源格式 optimal-tiling 特性；缺任一项都不声称 cubic，LINEAR 仍是回退。
#ifndef DM_ANDROID_NVN_PRESENT_FILTER_H
#define DM_ANDROID_NVN_PRESENT_FILTER_H

#include <vulkan/vulkan.h>

typedef enum {
    DM_NVN_PRESENT_FILTER_LINEAR = 0,
    DM_NVN_PRESENT_FILTER_CUBIC = 1
} dm_nvn_present_filter;

// Enable the device extension only when the driver advertises it and the
// bounded caller switch did not opt out.
int dm_nvn_present_filter_extension_enabled(int advertised, int linear_override);

// Choose CUBIC only for an enabled extension whose actual 2D source format has
// the cubic sampled-filter feature. Vulkan blit filtering rejects 1D/3D
// sources (VUID-VkImageBlit-filter-00237), so anything else stays LINEAR.
dm_nvn_present_filter dm_nvn_present_filter_select(
    int extension_enabled, int source_is_2d, VkFormatFeatureFlags source_features,
    int linear_override);

#endif
