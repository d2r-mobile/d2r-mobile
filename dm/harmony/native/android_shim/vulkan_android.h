// vulkan.h 在定义 VK_USE_PLATFORM_ANDROID_KHR 时会包含本文件：改接鸿蒙 VK_OHOS_surface（替身，见 README.md）。
// vulkan.h includes this file when VK_USE_PLATFORM_ANDROID_KHR is defined: route it to HarmonyOS VK_OHOS_surface (stand-in, see README.md).
#pragma once
#include <vulkan/vulkan_ohos.h>
#define VkAndroidSurfaceCreateInfoKHR VkSurfaceCreateInfoOHOS
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS
#define vkCreateAndroidSurfaceKHR vkCreateSurfaceOHOS
#define VK_KHR_ANDROID_SURFACE_EXTENSION_NAME VK_OHOS_SURFACE_EXTENSION_NAME
