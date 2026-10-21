// #13 NVN minf 映射的 loader 捕获回归：拦截 vkCreateSampler，逐字段核对全部 6 个 minf 与 malformed LOD 的 no-call 行为。
// #13 loader-captured regression for the NVN minf mapping: intercept vkCreateSampler and check every field for all six
// minf values plus the no-call behaviour for malformed original LOD input.
#include "../vulkan/vk_sampler.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures, g_calls;
static VkSamplerCreateInfo g_ci;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

VkResult dm_test_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *info,
                                 const VkAllocationCallbacks *allocator, VkSampler *sampler) {
    (void)device; (void)allocator;
    g_calls++;
    if (info) g_ci = *info;
    *sampler = (VkSampler)(uintptr_t)0x5151;
    return VK_SUCCESS;
}
void dm_test_vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *allocator) {
    (void)device; (void)sampler; (void)allocator;
}

static void expect_capture(dm_vk *vk, int minf, VkFilter min_filter, VkSamplerMipmapMode mipmap,
                           float lod_min, float lod_max) {
    dm_vk_nvn_sampler nvn;
    memset(&nvn, 0, sizeof nvn);
    nvn.minf = minf; nvn.magf = 1;
    nvn.ws = 1; nvn.wt = 0; nvn.wr = 6;
    nvn.lod_min = 1.5f; nvn.lod_max = 3.5f;
    nvn.lod_bias = 0.5f; nvn.aniso = 1.0f;
    int before = g_calls;
    dm_vk_sampler s;
    memset(&s, 0, sizeof s);
    CHECK(dm_vk_sampler_from_nvn(vk, &nvn, &s) == VK_SUCCESS);
    CHECK(g_calls == before + 1);
    CHECK(s.handle != VK_NULL_HANDLE);
    CHECK(g_ci.magFilter == VK_FILTER_LINEAR);
    CHECK(g_ci.minFilter == min_filter);
    CHECK(g_ci.mipmapMode == mipmap);
    CHECK(g_ci.addressModeU == VK_SAMPLER_ADDRESS_MODE_REPEAT);
    CHECK(g_ci.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CHECK(g_ci.addressModeW == VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
    CHECK(g_ci.mipLodBias == 0.5f);
    CHECK(g_ci.minLod == lod_min);
    CHECK(g_ci.maxLod == lod_max);
    CHECK(g_ci.anisotropyEnable == VK_FALSE);
    CHECK(g_ci.compareEnable == VK_FALSE);
    dm_vk_sampler_destroy(vk, &s);
}

int main(void) {
    dm_vk vk;
    if (dm_vk_create(&vk) != VK_SUCCESS) { fprintf(stderr, "no Vulkan device\n"); return 1; }
    const VkFilter min_f[6] = {VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_FILTER_NEAREST,
                               VK_FILTER_LINEAR, VK_FILTER_NEAREST, VK_FILTER_LINEAR};
    const VkSamplerMipmapMode mip[6] = {VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                        VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                        VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR};
    for (int minf = 0; minf <= 5; minf++) {
        // 0/1 非 mipmap：有效 LOD 为 0；2..5 保留原始 LOD。 | 0/1 non-mipmapped: LOD 0; 2..5 keep the original LOD.
        float lo = minf <= 1 ? 0.0f : 1.5f, hi = minf <= 1 ? 0.0f : 3.5f;
        expect_capture(&vk, minf, min_f[minf], mip[minf], lo, hi);
    }

    // 原始 NVN LOD 非法：NaN/Inf/min>max 必须在调用 loader 之前拒绝（非 mipmap 归一化不能绕过）。
    // Malformed original NVN LOD: NaN/Inf/min>max must be rejected before the loader (non-mip normalization must not bypass).
    dm_vk_nvn_sampler bad;
    memset(&bad, 0, sizeof bad);
    bad.minf = 0; bad.magf = 1; bad.ws = bad.wt = bad.wr = 1; bad.aniso = 1.0f;
    bad.lod_min = NAN; bad.lod_max = 0.0f;
    dm_vk_sampler s;
    memset(&s, 0, sizeof s);
    int before = g_calls;
    CHECK(dm_vk_sampler_from_nvn(&vk, &bad, &s) != VK_SUCCESS);
    bad.lod_min = 0.0f; bad.lod_max = INFINITY;
    CHECK(dm_vk_sampler_from_nvn(&vk, &bad, &s) != VK_SUCCESS);
    bad.lod_min = 2.0f; bad.lod_max = 1.0f;
    CHECK(dm_vk_sampler_from_nvn(&vk, &bad, &s) != VK_SUCCESS);
    bad.lod_min = 0.0f; bad.lod_max = 1.0f; bad.minf = 9;
    CHECK(dm_vk_sampler_from_nvn(&vk, &bad, &s) != VK_SUCCESS);
    CHECK(g_calls == before);   // 全部在 loader 之前拒绝 | all rejected before the loader
    CHECK(s.handle == VK_NULL_HANDLE);
    dm_vk_destroy(&vk);
    printf("vulkan_sampler_nvnmap: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
