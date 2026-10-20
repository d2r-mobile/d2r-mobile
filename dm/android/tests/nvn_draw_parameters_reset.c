// #230 fallback/reset regression using the actual gpu_driver reset boundary.
// A failed custom device must not carry draw-feature support/prepared state
// into the retry on a different physical device.
#include "../native/gpu_driver.c"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static uint32_t fixture_driver_id;
static void VKAPI_PTR fixture_properties(VkPhysicalDevice p, VkPhysicalDeviceProperties *out) {
    (void)p; memset(out,0,sizeof *out); out->apiVersion=VK_API_VERSION_1_2;
}
static void VKAPI_PTR fixture_features(VkPhysicalDevice p, VkPhysicalDeviceFeatures2 *out) {
    (void)p; ((VkPhysicalDeviceShaderDrawParametersFeatures *)out->pNext)->shaderDrawParameters=VK_FALSE;
}
static void VKAPI_PTR fixture_properties2(VkPhysicalDevice p, VkPhysicalDeviceProperties2 *out) {
    (void)p; VkPhysicalDeviceSubgroupProperties *sub=out->pNext;
    sub->subgroupSize=64;
    VkPhysicalDeviceDriverProperties *id=sub->pNext; id->driverID=fixture_driver_id;
}

int main(void) {
    dm_vk_driver first = {0};
    VkDeviceCreateInfo first_create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    first.shader_draw_parameters_supported = 1;
    CHECK(dm_vk_driver_prepare_device(&first, &first_create));
    CHECK(first_create.pNext != NULL);
    CHECK(first.shader_draw_parameters_prepared);

    // This is the existing reset called by open_system before driver_retry.
    reset_subgroup_policy(&first);
    CHECK(!first.shader_draw_parameters_supported);
    CHECK(!first.shader_draw_parameters_prepared);

    // The retry sees a new physical without the feature; it must not inherit support.
    first.shader_draw_parameters_supported = 0;
    VkDeviceCreateInfo retry = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    CHECK(dm_vk_driver_prepare_device(&first, &retry));
    CHECK(retry.pNext == NULL);
    CHECK(dm_vk_driver_require_shader_draw_parameters(&first, 0));
    CHECK(!dm_vk_driver_require_shader_draw_parameters(&first, 1));

    // Same selected device must still reject a duplicate application.
    dm_vk_driver duplicate = {0};
    VkDeviceCreateInfo duplicate_create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    duplicate.shader_draw_parameters_supported = 1;
    CHECK(dm_vk_driver_prepare_device(&duplicate, &duplicate_create));
    CHECK(!dm_vk_driver_prepare_device(&duplicate, &duplicate_create));

    // Custom Qualcomm is an explicit identity choice, never accepted as Turnip.
    dm_vk_driver custom={0}; custom.using_custom=1;
    custom.vkGetPhysicalDeviceProperties=fixture_properties;
    custom.vkGetPhysicalDeviceFeatures2=fixture_features;
    custom.vkGetPhysicalDeviceProperties2=fixture_properties2;
    fixture_driver_id=VK_DRIVER_ID_QUALCOMM_PROPRIETARY;
    CHECK(!dm_vk_driver_validate(&custom,VK_NULL_HANDLE,NULL));
    custom.expected_driver_id=VK_DRIVER_ID_QUALCOMM_PROPRIETARY;
    CHECK(dm_vk_driver_validate(&custom,VK_NULL_HANDLE,NULL));
    fixture_driver_id=VK_DRIVER_ID_MESA_TURNIP;
    CHECK(!dm_vk_driver_validate(&custom,VK_NULL_HANDLE,NULL));
    custom.expected_driver_id=VK_DRIVER_ID_MESA_TURNIP;
    CHECK(dm_vk_driver_validate(&custom,VK_NULL_HANDLE,NULL));
    if (failures) return 1;
    puts("PASS nvn draw parameters reset/retry: no inherited support");
    return 0;
}
