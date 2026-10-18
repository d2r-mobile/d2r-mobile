// Vulkan 设备与单队列初始化；Apple 验证时显式启用 portability，Android 直接用原生 Vulkan loader。
// Vulkan device / single-queue initialization; explicitly enable portability for Apple verification, use native Vulkan loader on Android.
#include "vk_host.h"
#if defined(__ANDROID__)
#include <android/native_window.h>
#include <vulkan/vulkan_android.h>
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#define DM_VK_VALIDATION_LAYER "VK_LAYER_KHRONOS_validation"

// 验证诊断的堆上状态：回调 userdata 指向它，实例存活期间地址不变（dm_vk 会被按值复制）。
// Heap state for validation diagnostics: callback userdata points here, stable for the instance lifetime (dm_vk is copied by value).
typedef struct dm_vk_diag {
    _Atomic uint32_t errors, warnings;
    atomic_flag lock;                // 保护 first_error 的写入与拷贝 | guards writes and copies of first_error
    int capture;                     // 1 = debug messenger 已装好，计数才有意义 | messenger installed, counters meaningful
    char status[224];
    char layer_version[96];
    char first_error[256];
} dm_vk_diag;

static VKAPI_ATTR VkBool32 VKAPI_CALL dm_validation_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    (void)types;
    dm_vk_diag *d = user;
    if (!d) return VK_FALSE;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        // 先在锁内发布完整字符串，再增加计数：看到 errors>0 的读者一定能拿到完整文本。
        // Publish the complete text under the lock before bumping the counter: a reader that sees errors>0 can copy a complete string.
        while (atomic_flag_test_and_set(&d->lock)) { }
        if (!d->first_error[0]) {
            snprintf(d->first_error, sizeof d->first_error, "%s: %.200s",
                     data && data->pMessageIdName ? data->pMessageIdName : "?",
                     data && data->pMessage ? data->pMessage : "");
        }
        atomic_flag_clear(&d->lock);
        atomic_fetch_add(&d->errors, 1);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        atomic_fetch_add(&d->warnings, 1);
    }
    return VK_FALSE;   // 只报告，不中止 | report only, never abort
}

static int dm_instance_layer_present(const char *name, VkLayerProperties *props) {
    uint32_t n = 0;
    if (vkEnumerateInstanceLayerProperties(&n, NULL) != VK_SUCCESS || !n) return 0;
    VkLayerProperties *p = calloc(n, sizeof *p);
    if (!p) return 0;
    int found = 0;
    if (vkEnumerateInstanceLayerProperties(&n, p) == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            if (!strcmp(p[i].layerName, name)) { found = 1; if (props) *props = p[i]; break; }
    free(p);
    return found;
}

static int dm_instance_extension_present(const char *name) {
    uint32_t n = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &n, NULL) != VK_SUCCESS || !n) return 0;
    VkExtensionProperties *p = calloc(n, sizeof *p);
    if (!p) return 0;
    int found = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &n, p) == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            if (!strcmp(p[i].extensionName, name)) { found = 1; break; }
    free(p);
    return found;
}

static int dm_device_extension_present(VkPhysicalDevice physical, const char *name) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL) != VK_SUCCESS || !count) return 0;
    VkExtensionProperties *props = calloc(count, sizeof *props);
    if (!props) return 0;
    int found = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, props) == VK_SUCCESS)
        for (uint32_t i = 0; i < count; i++)
            if (!strcmp(props[i].extensionName, name)) { found = 1; break; }
    free(props);
    return found;
}

VkResult dm_vk_create_present(const dm_vk_present_options *options, dm_vk *out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    memset(out, 0, sizeof *out);
    if (!options || options->struct_size != sizeof *options || !options->window)
        return VK_ERROR_INITIALIZATION_FAILED;
#ifndef __ANDROID__
    if (!options->create_surface) return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
    if (!dm_instance_extension_present(VK_KHR_SURFACE_EXTENSION_NAME))
        return VK_ERROR_EXTENSION_NOT_PRESENT;
#ifdef __ANDROID__
    if (!options->create_surface && !dm_instance_extension_present(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME))
        return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
    dm_vk vk = {0};
    const char *instance_extensions[4];
    uint32_t instance_extension_count = 0;
    instance_extensions[instance_extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef __ANDROID__
    if (!options->create_surface) instance_extensions[instance_extension_count++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#endif
    int portability = dm_instance_extension_present(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (portability) instance_extensions[instance_extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    VkResult result = vkEnumerateInstanceVersion(&vk.api_version);
    if (result != VK_SUCCESS) vk.api_version = VK_API_VERSION_1_0;
    uint32_t requested_api = VK_API_VERSION_1_1;
    if (vk.api_version < requested_api) requested_api = vk.api_version;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "D2R present Vulkan foundation", .apiVersion = requested_api};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
        .enabledExtensionCount = instance_extension_count, .ppEnabledExtensionNames = instance_extensions};
    result = vkCreateInstance(&create, NULL, &vk.instance);
    if (result != VK_SUCCESS) return result;

    if (options->create_surface) {
        result = options->create_surface(vk.instance, options->window, &vk.surface);
    } else {
#ifdef __ANDROID__
        PFN_vkCreateAndroidSurfaceKHR make = (PFN_vkCreateAndroidSurfaceKHR)
            vkGetInstanceProcAddr(vk.instance, "vkCreateAndroidSurfaceKHR");
        if (!make) { result = VK_ERROR_EXTENSION_NOT_PRESENT; }
        else {
            VkAndroidSurfaceCreateInfoKHR surface_info = {.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                .window = (ANativeWindow *)options->window};
            result = make(vk.instance, &surface_info, NULL, &vk.surface);
        }
#else
        result = VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
    }
    if (result != VK_SUCCESS || !vk.surface) {
        if (result == VK_SUCCESS) result = VK_ERROR_INITIALIZATION_FAILED;
        dm_vk_destroy(&vk);
        return result;
    }
    vk.surface_owned = 1;

    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(vk.instance, &count, NULL);
    if (result != VK_SUCCESS || !count) {
        if (result == VK_SUCCESS) result = VK_ERROR_FEATURE_NOT_PRESENT;
        dm_vk_destroy(&vk);
        return result;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof *devices);
    if (!devices) { dm_vk_destroy(&vk); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    result = vkEnumeratePhysicalDevices(vk.instance, &count, devices);
    for (uint32_t i = 0; result == VK_SUCCESS && i < count && !vk.physical; i++) {
        if (!dm_device_extension_present(devices[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        uint32_t families_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families_count, NULL);
        VkQueueFamilyProperties *families = calloc(families_count ? families_count : 1, sizeof *families);
        if (!families) { result = VK_ERROR_OUT_OF_HOST_MEMORY; break; }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families_count, families);
        for (uint32_t q = 0; q < families_count; q++) {
            VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if (!families[q].queueCount || (families[q].queueFlags & need) != need) continue;
            VkBool32 supported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, vk.surface, &supported) == VK_SUCCESS && supported) {
                vk.physical = devices[i]; vk.family = q; break;
            }
        }
        free(families);
    }
    free(devices);
    if (result != VK_SUCCESS) { dm_vk_destroy(&vk); return result; }
    if (!vk.physical) { dm_vk_destroy(&vk); return VK_ERROR_FEATURE_NOT_PRESENT; }

    vkGetPhysicalDeviceProperties(vk.physical, &vk.properties);
    vk.driver_api_version = vk.properties.apiVersion;
    vk.driver_version = vk.properties.driverVersion;
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);
    const char *device_extensions[2];
    uint32_t device_extension_count = 0;
    device_extensions[device_extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    int portability_subset = dm_device_extension_present(vk.physical, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    if (portability_subset) device_extensions[device_extension_count++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue,
        .enabledExtensionCount = device_extension_count, .ppEnabledExtensionNames = device_extensions};
    result = vkCreateDevice(vk.physical, &device, NULL, &vk.device);
    if (result != VK_SUCCESS) { dm_vk_destroy(&vk); return result; }
    vkGetDeviceQueue(vk.device, vk.family, 0, &vk.queue);
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk.family, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
    result = vkCreateCommandPool(vk.device, &pool, NULL, &vk.pool);
    if (result != VK_SUCCESS) { dm_vk_destroy(&vk); return result; }
    *out = vk;
    return VK_SUCCESS;
}

VkResult dm_vk_create(dm_vk *out) { return dm_vk_create_ex(NULL, out); }

VkResult dm_vk_create_ex(const dm_vk_options *options, dm_vk *out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    memset(out, 0, sizeof *out);
    uint32_t mode = DM_VK_VALIDATION_OFF;
    if (options) {
        if (options->struct_size != sizeof *options || options->validation > DM_VK_VALIDATION_REQUIRED)
            return VK_ERROR_INITIALIZATION_FAILED;
        mode = options->validation;
    }
    dm_vk vk = {0};
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceVersion(&vk.api_version);
    if (result != VK_SUCCESS) vk.api_version = VK_API_VERSION_1_0;
    result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (result != VK_SUCCESS) return result;
    VkExtensionProperties *extensions = calloc(count ? count : 1, sizeof *extensions);
    if (!extensions) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result = vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);
    int portability = 0;
    for (uint32_t i = 0; result == VK_SUCCESS && i < count; i++)
        if (!strcmp(extensions[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) portability = 1;
    free(extensions);
    if (result != VK_SUCCESS) return result;
    // 显式 opt-in 的验证层：required 缺失即失败；optional 缺失继续但状态写明未验证。
    // Explicit opt-in layer: required fails when missing; optional continues with an explicit "unvalidated" status.
    VkLayerProperties layer_props;
    memset(&layer_props, 0, sizeof layer_props);
    int layer_present = dm_instance_layer_present(DM_VK_VALIDATION_LAYER, &layer_props);
    int debug_ext = dm_instance_extension_present(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    int use_layer = 0;
    if (mode == DM_VK_VALIDATION_REQUIRED && !layer_present) return VK_ERROR_LAYER_NOT_PRESENT;
    if (mode != DM_VK_VALIDATION_OFF && layer_present && !debug_ext) {
        if (mode == DM_VK_VALIDATION_REQUIRED) return VK_ERROR_EXTENSION_NOT_PRESENT;
        layer_present = 0;   // 没有 debug utils 就没有诊断，不谎称已启用 | no debug utils means no diagnostics; do not claim the layer
    }
    if (mode != DM_VK_VALIDATION_OFF && layer_present) use_layer = 1;
    if (mode != DM_VK_VALIDATION_OFF) {
        vk.diag = calloc(1, sizeof *vk.diag);
        if (!vk.diag) return VK_ERROR_OUT_OF_HOST_MEMORY;
        atomic_flag_clear(&vk.diag->lock);
        if (use_layer) {
            snprintf(vk.diag->status, sizeof vk.diag->status, "enabled:%s (capture pending)", DM_VK_VALIDATION_LAYER);
            snprintf(vk.diag->layer_version, sizeof vk.diag->layer_version, "spec %u.%u.%u, impl %u",
                     VK_VERSION_MAJOR(layer_props.specVersion), VK_VERSION_MINOR(layer_props.specVersion),
                     VK_VERSION_PATCH(layer_props.specVersion), layer_props.implementationVersion);
        }
        else if (!layer_present) snprintf(vk.diag->status, sizeof vk.diag->status,
            "unavailable:%s not in loader; running WITHOUT validation", DM_VK_VALIDATION_LAYER);
    }
    vk.validation = (uint32_t)use_layer;
    const char *instance_extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    const char *enabled_extensions[2];
    uint32_t enabled_extension_count = 0;
    if (portability) enabled_extensions[enabled_extension_count++] = instance_extension;
    if (use_layer) enabled_extensions[enabled_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    const char *enabled_layers[1] = {DM_VK_VALIDATION_LAYER};
    VkDebugUtilsMessengerCreateInfoEXT debug = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = dm_validation_cb, .pUserData = vk.diag};
    // 应用请求 1.1，且不超过 loader 报告的实例版本 | request 1.1, never above the loader's instance version
    uint32_t api = VK_API_VERSION_1_1;
    if (vk.api_version < api) api = vk.api_version;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "D2R native Vulkan foundation", .apiVersion = api};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
        .enabledLayerCount = use_layer ? 1 : 0, .ppEnabledLayerNames = enabled_layers,
        .enabledExtensionCount = enabled_extension_count, .ppEnabledExtensionNames = enabled_extensions,
        .pNext = use_layer ? &debug : NULL};
    result = vkCreateInstance(&create, NULL, &vk.instance);
    if (result != VK_SUCCESS) { free(vk.diag); vk.diag = NULL; return result; }
    if (use_layer) {
        PFN_vkCreateDebugUtilsMessengerEXT make = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(vk.instance, "vkCreateDebugUtilsMessengerEXT");
        if (!make || make(vk.instance, &debug, NULL, &vk.messenger) != VK_SUCCESS) {
            if (mode == DM_VK_VALIDATION_REQUIRED) {
                vkDestroyInstance(vk.instance, NULL); free(vk.diag); vk.diag = NULL;
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
            // 层仍启用，但没有 messenger 就没有任何诊断：状态必须写清 "capture unavailable"，不能说成没开验证。
            // The layer stays enabled but without a messenger there is no diagnostics: the status must say "capture unavailable".
            vk.diag->capture = 0;
            snprintf(vk.diag->status, sizeof vk.diag->status,
                "enabled:%s (capture unavailable: debug messenger creation failed)", DM_VK_VALIDATION_LAYER);
        } else {
            vk.diag->capture = 1;
            snprintf(vk.diag->status, sizeof vk.diag->status, "enabled:%s", DM_VK_VALIDATION_LAYER);
        }
    }
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
    vk.driver_api_version = vk.properties.apiVersion;
    vk.driver_version = vk.properties.driverVersion;   // 驱动构建号，独立于 API 版本 | driver build, distinct from the API version
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
    if (vk->instance && vk->surface && vk->surface_owned) {
        vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);
    }
    if (vk->instance) {
        if (vk->messenger) {   // messenger 必须先于实例销毁 | the messenger must go before the instance
            PFN_vkDestroyDebugUtilsMessengerEXT bye = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(vk->instance, "vkDestroyDebugUtilsMessengerEXT");
            if (bye) bye(vk->instance, vk->messenger, NULL);
            vk->messenger = VK_NULL_HANDLE;
        }
        vkDestroyInstance(vk->instance, NULL);
    }
    free(vk->diag); vk->diag = NULL;
    memset(vk, 0, sizeof *vk);
}

int dm_vk_validation_enabled(const dm_vk *vk) { return vk && vk->validation ? 1 : 0; }
int dm_vk_validation_capture(const dm_vk *vk) { return vk && vk->diag ? vk->diag->capture : 0; }
uint32_t dm_vk_validation_errors(const dm_vk *vk) { return vk && vk->diag ? atomic_load(&vk->diag->errors) : 0; }
uint32_t dm_vk_validation_warnings(const dm_vk *vk) { return vk && vk->diag ? atomic_load(&vk->diag->warnings) : 0; }
const char *dm_vk_validation_status(const dm_vk *vk) { return vk && vk->diag ? vk->diag->status : "disabled"; }
const char *dm_vk_validation_layer_version(const dm_vk *vk) { return vk && vk->diag ? vk->diag->layer_version : ""; }
size_t dm_vk_validation_first_error(const dm_vk *vk, char *dst, size_t cap) {
    if (!dst || !cap) return 0;
    dst[0] = 0;
    if (!vk || !vk->diag) return 0;
    dm_vk_diag *d = vk->diag;
    while (atomic_flag_test_and_set(&d->lock)) { }   // 与回调互斥，避免半截字符串 | mutual exclusion with the callback, no torn string
    size_t n = strlen(d->first_error);
    if (n >= cap) n = cap - 1;
    memcpy(dst, d->first_error, n);
    dst[n] = 0;
    atomic_flag_clear(&d->lock);
    return n;
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
