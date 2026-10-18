// #42：在注入的 loader 边界上完整走通「层存在 + debug utils 可用」的路径，不需要 GPU 或真实验证层：
// 层/扩展枚举、实例创建、messenger 创建/销毁、回调计数与并发读取、required/optional 的缺失与 messenger 失败清理。
// 这不是真实验证证据；真实层不可用时的结论仍由 vulkan_validation.c 的 SKIP 表达。
// #42 drives the "layer present + debug utils available" path entirely on an injected loader boundary, with no GPU and
// no real layer: enumeration, instance creation, messenger create/destroy, callback counters and concurrent readers,
// required/optional absence, and messenger-failure cleanup. This is NOT real validation evidence; a real-layer absence
// is still reported as SKIP by vulkan_validation.c.
#include "../vulkan/vk_host.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define EXPECT(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define HANDLE(t, v) ((t)(uintptr_t)(v))
#define FAKE_INSTANCE HANDLE(VkInstance, 0x1001)
#define FAKE_PHYSICAL HANDLE(VkPhysicalDevice, 0x2001)
#define FAKE_DEVICE HANDLE(VkDevice, 0x3001)
#define FAKE_QUEUE HANDLE(VkQueue, 0x4001)
#define FAKE_POOL HANDLE(VkCommandPool, 0x5001)
#define FAKE_MESSENGER HANDLE(VkDebugUtilsMessengerEXT, 0x6001)
#define FAKE_DRIVER_VERSION 0x00ABCDEFu

static int g_layer, g_debug_ext, g_messenger_fails, g_create_fails;
static int g_instance_destroyed, g_messenger_destroyed;
static PFN_vkDebugUtilsMessengerCallbackEXT g_cb;
static void *g_cb_user;
static const char *const kMsg = "VUID-test: boom";

// ---- 注入的 loader 边界 | injected loader boundary ------------------------------------------------
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkEnumerateInstanceLayerProperties(uint32_t *count, VkLayerProperties *p) {
    if (!p) { *count = g_layer ? 1 : 0; return VK_SUCCESS; }
    if (!g_layer) { *count = 0; return VK_SUCCESS; }
    memset(p, 0, sizeof *p);
    snprintf(p->layerName, sizeof p->layerName, "VK_LAYER_KHRONOS_validation");
    p->specVersion = VK_MAKE_VERSION(1, 3, 275);
    p->implementationVersion = 7;
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count, VkExtensionProperties *p) {
    (void)layer;
    static const char *names[2] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    uint32_t n = 1 + (g_debug_ext ? 1 : 0);
    if (!p) { *count = n; return VK_SUCCESS; }
    for (uint32_t i = 0; i < n; i++) { memset(&p[i], 0, sizeof p[i]); snprintf(p[i].extensionName, sizeof p[i].extensionName, "%s", names[i]); }
    *count = n;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkCreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *a, VkInstance *out) {
    (void)info; (void)a;
    if (g_create_fails) return VK_ERROR_INITIALIZATION_FAILED;
    *out = FAKE_INSTANCE;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL dm_test_vkDestroyInstance(VkInstance i, const VkAllocationCallbacks *a) { (void)i; (void)a; g_instance_destroyed++; }
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkEnumeratePhysicalDevices(VkInstance i, uint32_t *count, VkPhysicalDevice *devices) {
    (void)i;
    if (!devices) { *count = 1; return VK_SUCCESS; }
    devices[0] = FAKE_PHYSICAL; *count = 1; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL dm_test_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p, uint32_t *count, VkQueueFamilyProperties *f) {
    (void)p;
    if (!f) { *count = 1; return; }
    memset(f, 0, sizeof *f); f[0].queueCount = 1; f[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; *count = 1;
}
VKAPI_ATTR void VKAPI_CALL dm_test_vkGetPhysicalDeviceProperties(VkPhysicalDevice p, VkPhysicalDeviceProperties *props) {
    (void)p;
    memset(props, 0, sizeof *props);
    snprintf(props->deviceName, sizeof props->deviceName, "FakeGPU");
    props->apiVersion = VK_MAKE_VERSION(1, 1, 0);
    props->driverVersion = FAKE_DRIVER_VERSION;
}
VKAPI_ATTR void VKAPI_CALL dm_test_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *m) {
    (void)p; memset(m, 0, sizeof *m);
}
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice p, const char *layer, uint32_t *count, VkExtensionProperties *e) {
    (void)p; (void)layer; (void)e; *count = 0; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkCreateDevice(VkPhysicalDevice p, const VkDeviceCreateInfo *info, const VkAllocationCallbacks *a, VkDevice *out) {
    (void)p; (void)info; (void)a; *out = FAKE_DEVICE; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL dm_test_vkDestroyDevice(VkDevice d, const VkAllocationCallbacks *a) { (void)d; (void)a; }
VKAPI_ATTR void VKAPI_CALL dm_test_vkGetDeviceQueue(VkDevice d, uint32_t family, uint32_t index, VkQueue *q) { (void)d; (void)family; (void)index; *q = FAKE_QUEUE; }
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkCreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo *info, const VkAllocationCallbacks *a, VkCommandPool *out) {
    (void)d; (void)info; (void)a; *out = FAKE_POOL; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL dm_test_vkDestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; }
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkDeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }

static VkResult VKAPI_CALL fake_create_messenger(VkInstance i, const VkDebugUtilsMessengerCreateInfoEXT *info,
        const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *out) {
    (void)i; (void)a;
    if (g_messenger_fails) return VK_ERROR_INITIALIZATION_FAILED;
    g_cb = info->pfnUserCallback; g_cb_user = info->pUserData;
    *out = FAKE_MESSENGER;
    return VK_SUCCESS;
}
static void VKAPI_CALL fake_destroy_messenger(VkInstance i, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) {
    (void)i; (void)m; (void)a; g_messenger_destroyed++;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL dm_test_vkGetInstanceProcAddr(VkInstance i, const char *name) {
    (void)i;
    if (!strcmp(name, "vkCreateDebugUtilsMessengerEXT")) return (PFN_vkVoidFunction)fake_create_messenger;
    if (!strcmp(name, "vkDestroyDebugUtilsMessengerEXT")) return (PFN_vkVoidFunction)fake_destroy_messenger;
    return NULL;
}

// ---- 并发读回调文本 | concurrent readers of the callback text -------------------------------------
static _Atomic int g_stop;
static _Atomic int g_torn;
static void *writer(void *arg) {
    (void)arg;
    VkDebugUtilsMessengerCallbackDataEXT data = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
        .pMessageIdName = "VUID-test", .pMessage = "boom"};
    for (int i = 0; i < 50; i++) g_cb(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data, g_cb_user);
    return NULL;
}
static void *reader(void *arg) {
    dm_vk *vk = arg;
    char buf[256];
    while (!atomic_load(&g_stop)) {
        size_t n = dm_vk_validation_first_error(vk, buf, sizeof buf);
        if (n && strcmp(buf, kMsg)) g_torn = 1;
    }
    return NULL;
}

static void reset_fakes(void) {
    g_layer = 1; g_debug_ext = 1; g_messenger_fails = 0; g_create_fails = 0;
    g_instance_destroyed = g_messenger_destroyed = 0;
    g_cb = NULL; g_cb_user = NULL;
}

int main(void) {
    // 1. 层 + debug utils 可用：required 成功，记录层/驱动版本，回调和并发读取正确。
    //    Layer + debug utils present: required succeeds, versions recorded, callback and concurrent reads are correct.
    reset_fakes();
    dm_vk vk = {0};
    dm_vk_options options = DM_VK_OPTIONS_INIT;
    options.validation = DM_VK_VALIDATION_REQUIRED;
    VkResult r = dm_vk_create_ex(&options, &vk);
    EXPECT(r == VK_SUCCESS);
    if (r != VK_SUCCESS) { printf("fakeloader: setup failed %d\n", (int)r); return 1; }
    EXPECT(dm_vk_validation_enabled(&vk));
    EXPECT(dm_vk_validation_capture(&vk) == 1);
    EXPECT(strstr(dm_vk_validation_status(&vk), "enabled:VK_LAYER_KHRONOS_validation") != NULL);
    EXPECT(strstr(dm_vk_validation_layer_version(&vk), "spec 1.3.275") != NULL);
    EXPECT(strstr(dm_vk_validation_layer_version(&vk), "impl 7") != NULL);
    EXPECT(vk.driver_version == FAKE_DRIVER_VERSION);
    EXPECT(VK_VERSION_MAJOR(vk.driver_api_version) == 1 && VK_VERSION_MINOR(vk.driver_api_version) == 1);

    VkDebugUtilsMessengerCallbackDataEXT data = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
        .pMessageIdName = "VUID-test", .pMessage = "boom"};
    g_cb(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data, g_cb_user);
    g_cb(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data, g_cb_user);
    char first[256] = {0};
    size_t n = dm_vk_validation_first_error(&vk, first, sizeof first);
    EXPECT(n == strlen(kMsg) && !strcmp(first, kMsg));
    EXPECT(dm_vk_validation_errors(&vk) == 1);
    EXPECT(dm_vk_validation_warnings(&vk) == 1);

    pthread_t writers[4], readers[2];
    g_stop = 0; g_torn = 0;
    for (int i = 0; i < 2; i++) pthread_create(&readers[i], NULL, reader, &vk);
    for (int i = 0; i < 4; i++) pthread_create(&writers[i], NULL, writer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(writers[i], NULL);
    g_stop = 1;
    for (int i = 0; i < 2; i++) pthread_join(readers[i], NULL);
    EXPECT(!g_torn);
    EXPECT(dm_vk_validation_errors(&vk) == 201);
    dm_vk_destroy(&vk);
    EXPECT(g_messenger_destroyed == 1 && g_instance_destroyed == 1);

    // 2. messenger 创建失败：optional 必须写 "capture unavailable" 且层仍算启用（不能谎称没开）。
    //    Messenger creation fails: optional must say "capture unavailable" while the layer stays enabled.
    reset_fakes(); g_messenger_fails = 1;
    dm_vk vk2 = {0};
    options.validation = DM_VK_VALIDATION_OPTIONAL;
    r = dm_vk_create_ex(&options, &vk2);
    EXPECT(r == VK_SUCCESS);
    if (r == VK_SUCCESS) {
        EXPECT(dm_vk_validation_enabled(&vk2) == 1);
        EXPECT(dm_vk_validation_capture(&vk2) == 0);
        EXPECT(strstr(dm_vk_validation_status(&vk2), "capture unavailable") != NULL);
        EXPECT(dm_vk_validation_errors(&vk2) == 0);
        dm_vk_destroy(&vk2);
    }

    // 3. messenger 创建失败 + required：明确失败并清理实例。
    //    Messenger failure + required: explicit failure and the instance is destroyed.
    reset_fakes(); g_messenger_fails = 1;
    dm_vk vk3 = {0};
    options.validation = DM_VK_VALIDATION_REQUIRED;
    g_instance_destroyed = 0;
    r = dm_vk_create_ex(&options, &vk3);
    EXPECT(r == VK_ERROR_EXTENSION_NOT_PRESENT);
    EXPECT(!vk3.instance && !vk3.diag);
    EXPECT(g_instance_destroyed == 1);
    dm_vk_destroy(&vk3);

    // 4. 层在但 debug utils 缺失：required 明确失败；optional 成功但状态写 unavailable。
    //    Layer present, debug utils missing: required fails; optional succeeds with an "unavailable" status.
    reset_fakes(); g_debug_ext = 0;
    dm_vk vk4 = {0};
    options.validation = DM_VK_VALIDATION_REQUIRED;
    EXPECT(dm_vk_create_ex(&options, &vk4) == VK_ERROR_EXTENSION_NOT_PRESENT);
    EXPECT(!vk4.instance && !vk4.diag);
    dm_vk_destroy(&vk4);
    options.validation = DM_VK_VALIDATION_OPTIONAL;
    r = dm_vk_create_ex(&options, &vk4);
    EXPECT(r == VK_SUCCESS);
    if (r == VK_SUCCESS) {
        EXPECT(dm_vk_validation_enabled(&vk4) == 0);
        EXPECT(strstr(dm_vk_validation_status(&vk4), "unavailable") != NULL);
        dm_vk_destroy(&vk4);
    }

    // 5. 层缺失：required 失败，optional 成功但未验证。
    //    Layer missing: required fails; optional succeeds unvalidated.
    reset_fakes(); g_layer = 0;
    dm_vk vk5 = {0};
    options.validation = DM_VK_VALIDATION_REQUIRED;
    EXPECT(dm_vk_create_ex(&options, &vk5) == VK_ERROR_LAYER_NOT_PRESENT);
    options.validation = DM_VK_VALIDATION_OPTIONAL;
    r = dm_vk_create_ex(&options, &vk5);
    EXPECT(r == VK_SUCCESS);
    if (r == VK_SUCCESS) { EXPECT(dm_vk_validation_enabled(&vk5) == 0); dm_vk_destroy(&vk5); }

    printf("vulkan_validation_fakeloader: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
