// #13 采样器创建失败注入：loader 边界返回失败，out 必须保持零值，后续 destroy 安全。
// #13 sampler-creation failure injection: the loader boundary fails, out must stay zeroed and destroy stays safe.
#include "../vulkan/vk_sampler.h"
#include <stdio.h>
#include <string.h>

static int checks, failures, g_calls;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

VkResult dm_test_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *info,
                                 const VkAllocationCallbacks *allocator, VkSampler *sampler) {
    (void)device; (void)info; (void)allocator;
    g_calls++;
    *sampler = (VkSampler)(uintptr_t)0xdead;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

int main(void) {
    dm_vk vk;
    VkResult r = dm_vk_create(&vk);
    if (r != VK_SUCCESS) { fprintf(stderr, "no Vulkan device\n"); return 1; }
    dm_vk_sampler_desc d;
    memset(&d, 0, sizeof d);
    d.min_filter = DM_VK_FILTER_NEAREST; d.mag_filter = DM_VK_FILTER_NEAREST;
    d.mipmap = DM_VK_MIP_NEAREST; d.max_aniso = 1.0f;
    d.address_u = d.address_v = d.address_w = DM_VK_ADDRESS_CLAMP_TO_EDGE;
    dm_vk_sampler out;
    memset(&out, 0, sizeof out);
    dm_vk_sampler sentinel;
    memset(&sentinel, 0x5a, sizeof sentinel);
    unsigned char before[sizeof sentinel];
    memcpy(before, &sentinel, sizeof before);

    // 未知/负值/超大公开枚举：必须在调用 vkCreateSampler 之前失败，且 out 不变。
    // Unknown/negative/large public enums must fail before vkCreateSampler is called, with out unchanged.
    dm_vk_sampler_desc bad = d;
    bad.address_u = (dm_vk_address)99;
    CHECK(dm_vk_sampler_create(&vk, &bad, &sentinel) != VK_SUCCESS);
    bad = d; bad.address_v = (dm_vk_address)-1;
    CHECK(dm_vk_sampler_create(&vk, &bad, &sentinel) != VK_SUCCESS);
    bad = d; bad.address_w = (dm_vk_address)(1 << 20);
    CHECK(dm_vk_sampler_create(&vk, &bad, &sentinel) != VK_SUCCESS);
    bad = d; bad.min_filter = (dm_vk_filter)9;
    CHECK(dm_vk_sampler_create(&vk, &bad, &sentinel) != VK_SUCCESS);
    bad = d; bad.mipmap = (dm_vk_mip)7;
    CHECK(dm_vk_sampler_create(&vk, &bad, &sentinel) != VK_SUCCESS);
    CHECK(g_calls == 0);                       // 未触碰 loader | loader untouched
    CHECK(!memcmp(before, &sentinel, sizeof before));

    CHECK(dm_vk_sampler_create(&vk, &d, &out) == VK_ERROR_OUT_OF_HOST_MEMORY);
    CHECK(out.handle == VK_NULL_HANDLE);
    CHECK(g_calls == 1);                       // 合法描述恰好调用一次 | exactly one loader call for a valid descriptor
    dm_vk_sampler_destroy(&vk, &out);          // 失败后销毁安全 | destroy after failure is safe
    dm_vk_destroy(&vk);
    printf("vulkan_sampler_createfail: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
