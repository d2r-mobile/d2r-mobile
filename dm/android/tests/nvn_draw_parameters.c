// #230 owned feature/chain counterexample:
// - SPIR-V capability 4427 is detected from a small input
// - supported physical feature is appended to the real VkDeviceCreateInfo chain
// - unsupported feature is not fabricated and only required shaders fail
// - an existing caller chain and the subgroup chain are preserved
#include "gpu_driver.h"
#include "nvn_vulkan_caps.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static int is_draw(const VkBaseInStructure *node) {
    return node && node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
}
static int is_subgroup(const VkBaseInStructure *node) {
    return node && node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
}

int main(void) {
    uint32_t required[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 4427u};
    uint32_t unrelated[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 1u};
    uint32_t tess[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 3u};
    uint32_t storage_write[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 56u};
    uint32_t multiview[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 57u};
    CHECK(dm_nvn_spirv_uses_shader_draw_parameters(required, sizeof required));
    CHECK(!dm_nvn_spirv_uses_shader_draw_parameters(unrelated, sizeof unrelated));
    CHECK(!dm_nvn_spirv_uses_shader_draw_parameters(required, 16));
    CHECK(!dm_nvn_spirv_uses_shader_draw_parameters(NULL, 0));
    CHECK(dm_nvn_spirv_has_capability(tess, sizeof tess, 3u));
    CHECK(dm_nvn_spirv_has_capability(storage_write, sizeof storage_write, 56u));
    CHECK(dm_nvn_spirv_has_capability(multiview, sizeof multiview, 57u));
    CHECK(!dm_nvn_spirv_has_capability(tess, sizeof tess, 57u));

    // Supported: the queried feature must be an actual node in the device chain.
    dm_vk_driver supported = {0};
    VkDeviceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    supported.shader_draw_parameters_supported = 1;
    CHECK(dm_vk_driver_prepare_device(&supported, &create));
    const VkPhysicalDeviceShaderDrawParametersFeatures *draw =
        (const VkPhysicalDeviceShaderDrawParametersFeatures *)create.pNext;
    CHECK(is_draw((const VkBaseInStructure *)draw));
    CHECK(draw && draw->shaderDrawParameters == VK_TRUE);
    CHECK(draw && draw->pNext == NULL);
    CHECK(dm_vk_driver_require_shader_draw_parameters(&supported, 1));

    // Unsupported: no node is fabricated, but an unrelated shader path still works.
    dm_vk_driver unsupported = {0};
    VkDeviceCreateInfo unsupported_create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    CHECK(dm_vk_driver_prepare_device(&unsupported, &unsupported_create));
    CHECK(unsupported_create.pNext == NULL);
    CHECK(dm_vk_driver_require_shader_draw_parameters(&unsupported, 0));
    CHECK(!dm_vk_driver_require_shader_draw_parameters(&unsupported, 1));

    // Existing caller chain remains reachable after the appended feature.
    VkPhysicalDeviceFeatures2 existing = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    dm_vk_driver chained = {0};
    VkDeviceCreateInfo chained_create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = (void *)&existing};
    chained.shader_draw_parameters_supported = 1;
    CHECK(dm_vk_driver_prepare_device(&chained, &chained_create));
    draw = (const VkPhysicalDeviceShaderDrawParametersFeatures *)chained_create.pNext;
    CHECK(is_draw((const VkBaseInStructure *)draw));
    CHECK(draw && draw->pNext == (void *)&existing);

    // Subgroup policy is still prepended and its old chain includes the draw node.
    dm_vk_driver both = {0};
    VkDeviceCreateInfo both_create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = (void *)&existing};
    both.shader_draw_parameters_supported = 1;
    both.requested_subgroup_size = 64;
    both.subgroup_policy_supported = 1;
    CHECK(dm_vk_driver_prepare_device(&both, &both_create));
    const VkPhysicalDeviceSubgroupSizeControlFeatures *sub =
        (const VkPhysicalDeviceSubgroupSizeControlFeatures *)both_create.pNext;
    CHECK(is_subgroup((const VkBaseInStructure *)sub));
    draw = (const VkPhysicalDeviceShaderDrawParametersFeatures *)sub->pNext;
    CHECK(is_draw((const VkBaseInStructure *)draw));
    CHECK(draw && draw->pNext == (void *)&existing);
    CHECK(both_create.enabledExtensionCount == 1);
    CHECK(both_create.ppEnabledExtensionNames &&
          !strcmp(both_create.ppEnabledExtensionNames[0], VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME));

    if (failures) return 1;
    puts("PASS nvn draw parameters: capability/feature/chain/no-need");
    return 0;
}
