// 多边形偏移与原 Metal 一致：只有 FILL 位启用三角形深度偏移，clamp 在设备支持时传递。
// Polygon offset matches the original Metal: only the FILL bit enables triangle depth bias; clamp is forwarded when supported.
#include "nvn_vulkan_depth_bias.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#ifdef NVN_DEPTH_BIAS_BASE_BEHAVIOR
// 修正前生产行为：任一启用位都打开偏移，clamp 恒为 0。 | Pre-fix production behavior: any enable bit turns bias on, clamp always 0.
#define nvn_vk_depth_bias_enabled(e) ((e) != 0)
static nvn_vk_depth_bias base_values(unsigned e, float u, float f, float c, int s) { (void)c; (void)s; nvn_vk_depth_bias b = {e != 0, u, 0.0f, f}; return b; }
#define nvn_vk_depth_bias_values base_values
#endif

int main(void) {
    // 原 Metal：offset_en & 4 才偏移。 | Original Metal: bias only when offset_en & 4.
    for (unsigned e = 0; e < 8; e++) CHECK(nvn_vk_depth_bias_enabled(e) == ((e & 4u) != 0));
    nvn_vk_depth_bias b = nvn_vk_depth_bias_values(1u | 2u, 4.0f, 2.0f, 0.01f, 1);
    CHECK(!b.enable && b.constant == 0.0f && b.slope == 0.0f && b.clamp == 0.0f);
    b = nvn_vk_depth_bias_values(4u, 4.0f, 2.0f, 0.01f, 1);
    CHECK(b.enable && b.constant == 4.0f && b.slope == 2.0f && b.clamp == 0.01f);
    // 未启用 depthBiasClamp 特性时 clamp 必须为 0（Vulkan 有效性）。 | Without depthBiasClamp the clamp must be 0 (Vulkan validity).
    b = nvn_vk_depth_bias_values(7u, 4.0f, 2.0f, 0.01f, 0);
    CHECK(b.enable && b.clamp == 0.0f);
    puts("nvn_depth_bias_test OK");
    return 0;
}
