// #298 present filter policy/caller fixtures: actual production selector used
// by the final present blit. Covers absent extension, unsupported source
// format, supported cubic and the bounded linear opt-out. No GPU is opened.
// #298 上屏滤波策略/调用方夹具：使用最终 present blit 的真实生产选择器，
// 覆盖扩展缺失、源格式不支持、支持 cubic 与有界 linear 开关。不打开 GPU。
#include "nvn_present_filter.h"

#include <stdio.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    const VkFormatFeatureFlags cubic = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT;
    const VkFormatFeatureFlags linear = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    // Absent extension: never enable it; LINEAR even if the format claims cubic.
    CHECK(!dm_nvn_present_filter_extension_enabled(0, 0));
    CHECK(dm_nvn_present_filter_select(0, 1, cubic, 0) == DM_NVN_PRESENT_FILTER_LINEAR);

    // Advertised extension but the actual source format lacks cubic support.
    CHECK(dm_nvn_present_filter_extension_enabled(1, 0));
    CHECK(dm_nvn_present_filter_select(1, 1, linear, 0) == DM_NVN_PRESENT_FILTER_LINEAR);
    CHECK(dm_nvn_present_filter_select(1, 1, 0, 0) == DM_NVN_PRESENT_FILTER_LINEAR);

    // Supported extension and 2D source format: the caller selects cubic.
    CHECK(dm_nvn_present_filter_select(1, 1, cubic | linear, 0) == DM_NVN_PRESENT_FILTER_CUBIC);

    // Production allocator also supports 1D/3D; cubic blit is invalid there.
    CHECK(dm_nvn_present_filter_select(1, 0, cubic | linear, 0) == DM_NVN_PRESENT_FILTER_LINEAR);

    // Bounded root A/B: linear opt-out disables the extension and forces LINEAR.
    CHECK(!dm_nvn_present_filter_extension_enabled(1, 1));
    CHECK(dm_nvn_present_filter_select(1, 1, cubic, 1) == DM_NVN_PRESENT_FILTER_LINEAR);

    // A stale/unknown caller flag never upgrades a missing capability.
    CHECK(dm_nvn_present_filter_select(-1, 1, cubic, 0) == DM_NVN_PRESENT_FILTER_LINEAR);
    CHECK(dm_nvn_present_filter_select(1, -1, cubic, 0) == DM_NVN_PRESENT_FILTER_LINEAR);
    CHECK(dm_nvn_present_filter_extension_enabled(-1, 0) == 0);

    printf("nvn_present_filter: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
