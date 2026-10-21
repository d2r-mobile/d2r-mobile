// Vulkan 设备与单队列初始化；Apple 验证时显式启用 portability，Android 直接用原生 Vulkan loader。
// Vulkan device / single-queue initialization; explicitly enable portability for Apple verification, use native Vulkan loader on Android.
#include "vk_host.h"
#include <stdlib.h>
#include <string.h>

VkResult dm_vk_create(dm_vk *out) {
    dm_vk vk = {0};
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (result != VK_SUCCESS) return result;
    VkExtensionProperties *extensions = calloc(count ? count : 1, sizeof *extensions);
    if (!extensions) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result = vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);
    int portability = 0;
    for (uint32_t i = 0; result == VK_SUCCESS && i < count; i++)
        if (!strcmp(extensions[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) portability = 1;
    free(extensions);
    if (result != VK_SUCCESS) return result;
    const char *instance_extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "D2R native Vulkan foundation", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
        .enabledExtensionCount = portability ? 1 : 0, .ppEnabledExtensionNames = &instance_extension};
    result = vkCreateInstance(&create, NULL, &vk.instance);
    if (result != VK_SUCCESS) return result;
    count = 0; result = vkEnumeratePhysicalDevices(vk.instance, &count, NULL);
    if (result != VK_SUCCESS || !count) { if (result == VK_SUCCESS) result = VK_ERROR_FEATURE_NOT_PRESENT; goto failed; }
    VkPhysicalDevice *devices = calloc(count, sizeof *devices);
    if (!devices) { result = VK_ERROR_OUT_OF_HOST_MEMORY; goto failed; }
    result = vkEnumeratePhysicalDevices(vk.instance, &count, devices);
    for (uint32_t i = 0; result == VK_SUCCESS && i < count && !vk.physical; i++) {
        uint32_t queues = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queues, NULL);
        VkQueueFamilyProperties *families = calloc(queues ? queues : 1, sizeof *families);
        if (!families) { result = VK_ERROR_OUT_OF_HOST_MEMORY; break; }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queues, families);
        for (uint32_t q = 0; q < queues; q++) {
            VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if (families[q].queueCount && (families[q].queueFlags & need) == need) {
                vk.physical = devices[i]; vk.family = q; break;
            }
        }
        free(families);
    }
    free(devices);
    if (result != VK_SUCCESS) goto failed;
    if (!vk.physical) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto failed; }
    vkGetPhysicalDeviceProperties(vk.physical, &vk.properties);
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);
    count = 0; result = vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &count, NULL);
    if (result != VK_SUCCESS) goto failed;
    extensions = calloc(count ? count : 1, sizeof *extensions);
    if (!extensions) { result = VK_ERROR_OUT_OF_HOST_MEMORY; goto failed; }
    result = vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &count, extensions);
    portability = 0;
    for (uint32_t i = 0; result == VK_SUCCESS && i < count; i++)
        if (!strcmp(extensions[i].extensionName, "VK_KHR_portability_subset")) portability = 1;
    free(extensions);
    if (result != VK_SUCCESS) goto failed;
    const char *device_extension = "VK_KHR_portability_subset";
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue,
        .enabledExtensionCount = portability ? 1 : 0, .ppEnabledExtensionNames = &device_extension};
    result = vkCreateDevice(vk.physical, &device, NULL, &vk.device);
    if (result != VK_SUCCESS) goto failed;
    vkGetDeviceQueue(vk.device, vk.family, 0, &vk.queue);
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk.family, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
    result = vkCreateCommandPool(vk.device, &pool, NULL, &vk.pool);
    if (result != VK_SUCCESS) goto failed;
    *out = vk; return VK_SUCCESS;
failed:
    dm_vk_destroy(&vk); return result;
}

void dm_vk_destroy(dm_vk *vk) {
    if (vk->device) {
        vkDeviceWaitIdle(vk->device);
        if (vk->pool) vkDestroyCommandPool(vk->device, vk->pool, NULL);
        vkDestroyDevice(vk->device, NULL);
    }
    if (vk->instance) vkDestroyInstance(vk->instance, NULL);
    memset(vk, 0, sizeof *vk);
}

VkResult dm_vk_begin(dm_vk *vk, VkCommandBuffer *out) {
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    VkResult result = vkAllocateCommandBuffers(vk->device, &alloc, &command);
    if (result != VK_SUCCESS) return result;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    result = vkBeginCommandBuffer(command, &begin);
    if (result != VK_SUCCESS) { vkFreeCommandBuffers(vk->device, vk->pool, 1, &command); return result; }
    *out = command; return VK_SUCCESS;
}

VkResult dm_vk_submit(dm_vk *vk, VkCommandBuffer command) {
    VkResult result = vkEndCommandBuffer(command);
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (result == VK_SUCCESS) result = vkCreateFence(vk->device, &info, NULL, &fence);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    if (result == VK_SUCCESS) {
        result = vkQueueSubmit(vk->queue, 1, &submit, fence);
        int uncertain = result != VK_SUCCESS && result != VK_ERROR_OUT_OF_HOST_MEMORY &&
            result != VK_ERROR_OUT_OF_DEVICE_MEMORY;
        if (result == VK_SUCCESS) {
            result = vkWaitForFences(vk->device, 1, &fence, VK_TRUE, UINT64_MAX);
            uncertain = result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST;
        }
        // 提交时设备丢失不代表完成；等待时设备丢失允许清理，其他错误必须再确认空闲。
        // Device loss during submit does not mean completion; loss during a wait permits cleanup, other errors require idle confirmation.
        if (uncertain) {
            VkResult idle = vkDeviceWaitIdle(vk->device);
            if (idle != VK_SUCCESS && idle != VK_ERROR_DEVICE_LOST) abort();
        }
    }
    if (fence) vkDestroyFence(vk->device, fence, NULL);
    vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    return result;
}
