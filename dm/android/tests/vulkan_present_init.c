// #139 present-aware init seam: real production dm_vk_create_present path, injected Vulkan loader boundary.
// Two devices prove non-blind selection: first has graphics/compute but no present; second has swapchain/present.
// Covers missing extensions, surface/device/pool failure, no present device, cleanup order and offscreen non-use.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <string.h>
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define H(t, v) ((t)(uintptr_t)(v))

static int g_surface_ext = 1, g_surface_fail, g_surface_null, g_device_fail, g_pool_fail;
static int g_present_supported = 1, g_swapchain = 1, g_subset, g_instance_created, g_instance_destroyed;
static int g_surface_maint, g_surface_caps2, g_maint_feature, g_maint_ext;
static char g_inst_names[8][64]; static uint32_t g_inst_n;
static char g_dev_names[8][64]; static uint32_t g_dev_n;
static int g_feature_pnext;
static int g_surface_destroyed, g_device_created, g_device_destroyed, g_pool_created, g_pool_destroyed;
static uint32_t g_loader_api = VK_API_VERSION_1_1, g_app_api;
static char g_order[16]; static unsigned g_order_n;
static VkInstance g_instance;
static VkDevice g_device;
static void *g_window = (void *)(uintptr_t)0x1234;
static int has_name(char names[][64], uint32_t n, const char *want) {
    for (uint32_t i = 0; i < n; i++) if (!strcmp(names[i], want)) return 1;
    return 0;
}

static VkResult VKAPI_CALL test_surface_factory(VkInstance instance, void *window, VkSurfaceKHR *surface) {
    CHECK(instance == g_instance && window == g_window);
    if (g_surface_fail) return VK_ERROR_INITIALIZATION_FAILED;
    if (g_surface_null) { *surface = VK_NULL_HANDLE; return VK_SUCCESS; }
    *surface = H(VkSurfaceKHR, 0x5001);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateInstanceVersion(uint32_t *v) { *v = g_loader_api; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateInstanceLayerProperties(uint32_t *count, VkLayerProperties *p) {
    (void)p; *count = 0; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count, VkExtensionProperties *p) {
    (void)layer;
    const char *names[4] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME};
    uint32_t n = 0;
    if (g_surface_ext) { names[n++] = VK_KHR_SURFACE_EXTENSION_NAME; names[n++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME; }
    if (g_surface_maint) names[n++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
    if (g_surface_caps2) names[n++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
    if (!p) { *count = n; return VK_SUCCESS; }
    for (uint32_t i = 0; i < n; i++) { memset(&p[i], 0, sizeof p[i]); snprintf(p[i].extensionName, sizeof p[i].extensionName, "%s", names[i]); }
    *count = n; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateInstance(const VkInstanceCreateInfo *i, const VkAllocationCallbacks *a, VkInstance *out) {
    (void)a; CHECK(i && i->enabledExtensionCount >= 1);
    g_app_api = i->pApplicationInfo ? i->pApplicationInfo->apiVersion : 0;
    g_inst_n = i->enabledExtensionCount;
    for (uint32_t k = 0; k < i->enabledExtensionCount && k < 8; k++) snprintf(g_inst_names[k], sizeof g_inst_names[k], "%s", i->ppEnabledExtensionNames[k]);
    int surface = 0;
    for (uint32_t k = 0; k < i->enabledExtensionCount; k++) if (!strcmp(i->ppEnabledExtensionNames[k], VK_KHR_SURFACE_EXTENSION_NAME)) surface = 1;
    CHECK(surface);
    g_instance_created++; g_instance = H(VkInstance, 0x1001); *out = g_instance; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyInstance(VkInstance i, const VkAllocationCallbacks *a) { (void)i; (void)a; g_order[g_order_n++] = 'I'; g_instance_destroyed++; }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL test_vkGetInstanceProcAddr(VkInstance i, const char *name) { (void)i; (void)name; return NULL; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumeratePhysicalDevices(VkInstance i, uint32_t *count, VkPhysicalDevice *d) {
    (void)i;
    if (!d) { *count = 2; return VK_SUCCESS; }
    d[0] = H(VkPhysicalDevice, 0x2001); d[1] = H(VkPhysicalDevice, 0x2002); *count = 2; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p, uint32_t *count, VkQueueFamilyProperties *f) {
    (void)p;
    if (!f) { *count = 1; return; }
    memset(f, 0, sizeof *f); f[0].queueCount = 1; f[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; *count = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice p, const char *layer, uint32_t *count, VkExtensionProperties *e) {
    (void)p; (void)layer;
    if (!g_swapchain || p == H(VkPhysicalDevice, 0x2001)) { *count = 0; return VK_SUCCESS; }
    uint32_t n = 1u + (g_subset ? 1u : 0u) + (g_maint_ext ? 1u : 0u);
    if (!e) { *count = n; return VK_SUCCESS; }
    memset(e, 0, n * sizeof *e);
    uint32_t k = 0;
    snprintf(e[k++].extensionName, sizeof e[0].extensionName, "%s", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (g_subset) snprintf(e[k++].extensionName, sizeof e[0].extensionName, "%s", VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    if (g_maint_ext) snprintf(e[k++].extensionName, sizeof e[0].extensionName, "%s", VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
    *count = n; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice p, uint32_t family, VkSurfaceKHR s, VkBool32 *supported) {
    (void)family; CHECK(s == H(VkSurfaceKHR, 0x5001)); *supported = (p == H(VkPhysicalDevice, 0x2002) && g_present_supported) ? VK_TRUE : VK_FALSE; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceProperties(VkPhysicalDevice p, VkPhysicalDeviceProperties *o) { (void)p; memset(o, 0, sizeof *o); o->apiVersion = VK_API_VERSION_1_1; }
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice p, VkPhysicalDeviceFeatures2 *o) {
    (void)p; g_feature_pnext = o && o->pNext != NULL;
    if (o && o->pNext) ((VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *)o->pNext)->swapchainMaintenance1 = g_maint_feature ? VK_TRUE : VK_FALSE;
}
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *o) { (void)p; memset(o, 0, sizeof *o); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateDevice(VkPhysicalDevice p, const VkDeviceCreateInfo *i, const VkAllocationCallbacks *a, VkDevice *d) {
    (void)a; int maint_enabled = g_maint_ext && g_maint_feature && g_surface_maint && g_surface_caps2 && g_loader_api >= VK_API_VERSION_1_1;
    uint32_t expected = 1u + (g_subset ? 1u : 0u) + (maint_enabled ? 1u : 0u);
    CHECK(p == H(VkPhysicalDevice, 0x2002) && i->enabledExtensionCount == expected);
    g_dev_n = i->enabledExtensionCount;
    for (uint32_t k = 0; k < i->enabledExtensionCount && k < 8; k++) snprintf(g_dev_names[k], sizeof g_dev_names[k], "%s", i->ppEnabledExtensionNames[k]);
    int swap = 0, subset = 0, maintenance = 0;
    for (uint32_t k = 0; k < i->enabledExtensionCount; k++) {
        if (!strcmp(i->ppEnabledExtensionNames[k], VK_KHR_SWAPCHAIN_EXTENSION_NAME)) swap = 1;
        if (!strcmp(i->ppEnabledExtensionNames[k], VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) subset = 1;
        if (!strcmp(i->ppEnabledExtensionNames[k], VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) maintenance = 1;
    }
    CHECK(swap && subset == g_subset && maintenance == maint_enabled);
    if (maint_enabled) CHECK(i->pNext && ((const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *)i->pNext)->swapchainMaintenance1);
    else CHECK(!i->pNext);
    if (g_device_fail) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_device_created++; g_device = H(VkDevice, 0x3001); *d = g_device; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyDevice(VkDevice d, const VkAllocationCallbacks *a) { (void)d; (void)a; g_order[g_order_n++] = 'D'; g_device_destroyed++; }
VKAPI_ATTR void VKAPI_CALL test_vkGetDeviceQueue(VkDevice d, uint32_t family, uint32_t index, VkQueue *q) { (void)d; (void)family; (void)index; *q = H(VkQueue, 0x4001); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo *i, const VkAllocationCallbacks *a, VkCommandPool *p) {
    (void)d; (void)i; (void)a; if (g_pool_fail) return VK_ERROR_OUT_OF_HOST_MEMORY; g_pool_created++; *p = H(VkCommandPool, 0x6001); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; g_order[g_order_n++] = 'P'; g_pool_destroyed++; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkDeviceWaitIdle(VkDevice d) { (void)d; g_order[g_order_n++] = 'W'; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL test_vkDestroySurfaceKHR(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks *a) { (void)i; (void)s; (void)a; g_order[g_order_n++] = 'S'; g_surface_destroyed++; }

static void reset(void) {
    g_surface_ext = g_present_supported = g_swapchain = 1;
    g_surface_maint = g_surface_caps2 = g_maint_feature = g_maint_ext = 0;
    g_inst_n = g_dev_n = 0; g_feature_pnext = 0;
    memset(g_inst_names, 0, sizeof g_inst_names); memset(g_dev_names, 0, sizeof g_dev_names);
    g_surface_fail = g_surface_null = g_device_fail = g_pool_fail = 0;
    g_subset = 0; g_loader_api = VK_API_VERSION_1_1; g_app_api = 0;
    g_order_n = 0; memset(g_order, 0, sizeof g_order);
    g_instance_created = g_instance_destroyed = g_surface_destroyed = 0;
    g_device_created = g_device_destroyed = g_pool_created = g_pool_destroyed = 0;
    g_instance = VK_NULL_HANDLE; g_device = VK_NULL_HANDLE;
}
static dm_vk_present_options options(void) {
    dm_vk_present_options o = DM_VK_PRESENT_OPTIONS_INIT;
    o.window = g_window; o.create_surface = test_surface_factory; return o;
}
static void expect_clean(void) {
    CHECK(g_instance_created == g_instance_destroyed);
    CHECK(g_device_created == g_device_destroyed);
    CHECK(g_pool_created == g_pool_destroyed);
    CHECK(g_surface_destroyed == 1);
}

int main(void) {
    dm_vk_present_options o = options();
    reset(); dm_vk vk = {0}; CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(vk.device == H(VkDevice, 0x3001) && vk.surface == H(VkSurfaceKHR, 0x5001) && vk.surface_owned);
    CHECK(vk.family == 0);
    CHECK(!vk.swapchain_maintenance1);
    CHECK(vk.api_version == VK_API_VERSION_1_1 && g_app_api == VK_API_VERSION_1_1);
    dm_vk_destroy(&vk); expect_clean();
    CHECK(!strcmp(g_order, "WPDSI"));

    // Positive capability chain: instance surface-maintenance deps + device extension + feature.
    reset(); g_surface_maint = g_surface_caps2 = g_maint_ext = g_maint_feature = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(vk.surface_maintenance1 && vk.swapchain_maintenance1);
    CHECK(has_name(g_inst_names, g_inst_n, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME));
    CHECK(has_name(g_inst_names, g_inst_n, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME));
    CHECK(has_name(g_dev_names, g_dev_n, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME));
    CHECK(g_feature_pnext);
    dm_vk_destroy(&vk); expect_clean();

    // Missing instance dependency: maintenance must stay unsupported, never forge the flag.
    reset(); g_surface_maint = g_surface_caps2 = g_maint_ext = g_maint_feature = 1; g_surface_caps2 = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(!vk.surface_maintenance1 && !vk.swapchain_maintenance1);
    CHECK(!has_name(g_dev_names, g_dev_n, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME));
    dm_vk_destroy(&vk); expect_clean();

    // Advertised extension but feature false: no device extension/feature enablement.
    reset(); g_surface_maint = g_surface_caps2 = g_maint_ext = 1; g_maint_feature = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(!vk.swapchain_maintenance1);
    CHECK(!has_name(g_dev_names, g_dev_n, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME));
    dm_vk_destroy(&vk); expect_clean();

    // Loader 1.0: maintenance is unsupported (no properties2 query path), not silently claimed.
    reset(); g_surface_maint = g_surface_caps2 = g_maint_ext = g_maint_feature = 1; g_loader_api = VK_API_VERSION_1_0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(!vk.swapchain_maintenance1);
    dm_vk_destroy(&vk); expect_clean();
    reset(); g_subset = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(g_device_created == 1); dm_vk_destroy(&vk); expect_clean();
    CHECK(!strcmp(g_order, "WPDSI"));

    // Loader only reports 1.0: request must be negotiated down, not hardcoded to 1.1.
    reset(); g_loader_api = VK_API_VERSION_1_0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(vk.api_version == VK_API_VERSION_1_0 && g_app_api == VK_API_VERSION_1_0);
    dm_vk_destroy(&vk); expect_clean();

    reset(); g_surface_ext = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_EXTENSION_NOT_PRESENT);
    CHECK(!vk.instance && !vk.device && !vk.surface);
    CHECK(g_instance_created == 0);

    reset(); g_swapchain = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_FEATURE_NOT_PRESENT);
    CHECK(!vk.device && g_instance_created == g_instance_destroyed && g_surface_destroyed == 1);

    reset(); g_present_supported = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_FEATURE_NOT_PRESENT);
    CHECK(!vk.device && g_instance_destroyed == 1 && g_surface_destroyed == 1);
    CHECK(!strcmp(g_order, "SI"));

    reset(); g_surface_fail = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) != VK_SUCCESS);
    CHECK(g_instance_destroyed == 1 && g_surface_destroyed == 0 && !vk.surface);
    CHECK(!strcmp(g_order, "I"));

    reset(); g_surface_null = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(g_instance_destroyed == 1 && !vk.surface);

    reset(); g_device_fail = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) != VK_SUCCESS);
    CHECK(g_instance_destroyed == 1 && g_surface_destroyed == 1 && !vk.device);
    CHECK(!strcmp(g_order, "SI"));

    reset(); g_pool_fail = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) != VK_SUCCESS);
    CHECK(g_instance_destroyed == 1 && g_surface_destroyed == 1 && g_device_destroyed == 1 && !vk.pool);
    CHECK(!strcmp(g_order, "WDSI"));

    // Surface factory sees the same instance and the borrowed window; device selection does not pick device 0.
    reset(); dm_vk_present_options bad = options(); bad.struct_size = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&bad, &vk) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(g_instance_created == 0);

    printf("vulkan_present_init: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
