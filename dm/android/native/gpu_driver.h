// 每个Vulkan上下文持有自己的loader与完整分发表，驱动只能在初始化时选择。
// Each Vulkan context owns its loader and complete dispatch; select a driver only at initialization.
#pragma once
#ifdef __ANDROID__
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#endif
#include <vulkan/vulkan.h>
#include "vulkan_dispatch.h"

typedef struct dm_vk_driver {
    void *library, *adrenotools;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define DM_VK_FIELD(name) PFN_##name name;
    DM_VK_GLOBAL_FUNCTIONS(DM_VK_FIELD)
    DM_VK_INSTANCE_FUNCTIONS(DM_VK_FIELD)
    DM_VK_ANDROID_INSTANCE_FUNCTIONS(DM_VK_FIELD)
    DM_VK_DEVICE_FUNCTIONS(DM_VK_FIELD)
#undef DM_VK_FIELD
    char hook_directory[1024], driver_directory[1024], driver_name[128], error[512];
    int requested_custom, using_custom, require_custom, needs_surface;
    uint32_t actual_driver_id, subgroup_size;
    // 保留真实物理缺省属性；shader明确选择的大小单独记录，政策只在初始化确定。
    // Preserve actual physical defaults; record explicitly selected shader size separately and choose only at initialization.
    uint32_t requested_subgroup_size, effective_shader_subgroup_size;
    VkShaderStageFlags required_subgroup_stages;
    int subgroup_policy_required, subgroup_policy_supported, subgroup_policy_prepared, subgroup_policy_enabled;
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroup_features;
    const char **policy_device_extensions;
} dm_vk_driver;

int dm_vk_driver_open(dm_vk_driver *driver);
int dm_vk_driver_load_instance(dm_vk_driver *driver, VkInstance instance, int needs_surface);
int dm_vk_driver_load_device(dm_vk_driver *driver, VkDevice device);
// shader目录为NULL时用于无离线Maxwell shader缓存的独立诊断/GLSL测试。
// A NULL shader directory is for independent diagnostics/GLSL tests without an offline Maxwell cache.
int dm_vk_driver_validate(dm_vk_driver *driver, VkPhysicalDevice physical, const char *shader_directory);
// 保留调用者device扩展/feature链；明确政策无法满足时失败，默认路径不修改。
// Preserve caller device extensions/feature chains; fail on unsupported explicit policy and preserve defaults.
int dm_vk_driver_prepare_device(dm_vk_driver *driver, VkDeviceCreateInfo *create);
// required由调用者持有至pipeline创建完成；不启FULL，允许32线程客体组。
// Keep caller-owned required storage alive until pipeline creation; never enable FULL, allowing guest groups of32.
int dm_vk_driver_prepare_shader_stage(dm_vk_driver *driver, VkPipelineShaderStageCreateInfo *stage, VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *required);
// 调用者必须先用旧分发表销毁其surface/instance/device；不支持活对象切换。
// Destroy old surface/instance/device with their old dispatch before calling; live-object switching is unsupported.
int dm_vk_driver_fallback(dm_vk_driver *driver, const char *reason);
void dm_vk_driver_close(dm_vk_driver *driver);
const char *dm_vk_driver_error(const dm_vk_driver *driver);
