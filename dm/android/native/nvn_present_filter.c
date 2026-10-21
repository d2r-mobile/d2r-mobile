#include "nvn_present_filter.h"

int dm_nvn_present_filter_extension_enabled(int advertised, int linear_override) {
    return advertised == 1 && linear_override == 0;
}

dm_nvn_present_filter dm_nvn_present_filter_select(
    int extension_enabled, int source_is_2d, VkFormatFeatureFlags source_features,
    int linear_override) {
    if (extension_enabled != 1 || source_is_2d != 1 || linear_override)
        return DM_NVN_PRESENT_FILTER_LINEAR;
    if (!(source_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT))
        return DM_NVN_PRESENT_FILTER_LINEAR;
    return DM_NVN_PRESENT_FILTER_CUBIC;
}
