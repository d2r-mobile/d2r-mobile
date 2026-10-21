// Android 设备能力探针，依据 NDK 系统属性、POSIX 内存及 Vulkan 1.1 接口。
// Android device capability probe, based on NDK system properties, POSIX memory and Vulkan 1.1 APIs.
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

static void property(const char *key) {
    char value[PROP_VALUE_MAX] = {0};
    __system_property_get(key, value);
    printf("%s=%s\n", key, value[0] ? value : "unknown");
}

static int memory_probe(void) {
    long page = sysconf(_SC_PAGESIZE);
    printf("page_size=%ld\n", page);
    if (page <= 0) return 1;
    void *p = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap RW"); return 1; }
    // 只测试权限切换，不执行客体代码；adb shell 域不代表 App 域。
    // Only test permission changes, never execute guest code; the adb shell domain does not represent the app domain.
    const uint32_t ret = 0xd65f03c0;
    memcpy(p, &ret, sizeof ret);
    __builtin___clear_cache((char *)p, (char *)p + sizeof ret);
    int rc = mprotect(p, (size_t)page, PROT_READ | PROT_EXEC);
    int error = errno;
    printf("anonymous_rw_to_rx=%s\n", rc == 0 ? "yes" : strerror(error));
    if (munmap(p, (size_t)page)) { perror("munmap"); return 1; }
    return rc != 0;
}

static int gpu_probe(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "D2R Android preflight", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app};
    VkInstance instance;
    VkResult result = vkCreateInstance(&info, NULL, &instance);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance=%d\n", result); return 1; }
    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(instance, &count, NULL);
    if (result != VK_SUCCESS || count == 0) {
        fprintf(stderr, "vkEnumeratePhysicalDevices=%d count=%u\n", result, count);
        vkDestroyInstance(instance, NULL); return 1;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof *devices);
    if (!devices) { vkDestroyInstance(instance, NULL); return 1; }
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices=%d\n", result);
        free(devices); vkDestroyInstance(instance, NULL); return 1;
    }
    int usable = 0;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        vkGetPhysicalDeviceFeatures(devices[i], &features);
        printf("gpu[%u]=%s vendor=0x%x device=0x%x vulkan=%u.%u.%u driver=0x%x\n", i,
            props.deviceName, props.vendorID, props.deviceID,
            VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
            VK_VERSION_PATCH(props.apiVersion), props.driverVersion);
        printf("gpu[%u].bc=%u astc_ldr=%u shader_int64=%u max_texture_2d=%u\n", i,
            features.textureCompressionBC, features.textureCompressionASTC_LDR,
            features.shaderInt64, props.limits.maxImageDimension2D);
        uint32_t queues = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queues, NULL);
        VkQueueFamilyProperties *families = calloc(queues, sizeof *families);
        if (!families) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queues, families);
        for (uint32_t q = 0; q < queues; q++) {
            if (!families[q].queueCount || !(families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            float priority = 1.0f;
            VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = q, .queueCount = 1, .pQueuePriorities = &priority};
            VkDeviceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
            VkDevice device;
            result = vkCreateDevice(devices[i], &create, NULL, &device);
            printf("gpu[%u].graphics_device=%d\n", i, result);
            if (result == VK_SUCCESS) { usable = 1; vkDestroyDevice(device, NULL); }
            break;
        }
        free(families);
    }
    free(devices);
    vkDestroyInstance(instance, NULL);
    return !usable;
}

int main(void) {
    puts("D2R Android preflight; target: Xiaomi 15 Ultra / Snapdragon 8 Elite");
    puts("Diagnostic only; game integration and the NVN renderer are not implemented.");
    property("ro.product.model"); property("ro.soc.model");
    property("ro.build.version.release"); property("ro.build.version.sdk");
    property("ro.product.cpu.abi");
    int memory = memory_probe();
    uint64_t cache_type;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(cache_type));
    printf("ctr_el0=0x%llx icache_min_line=%u dcache_min_line=%u\n",(unsigned long long)cache_type,
        4u<<(cache_type&15u),4u<<((cache_type>>16)&15u));
    int gpu = gpu_probe();
    printf("preflight=%s (does not certify game compatibility)\n", memory || gpu ? "FAIL" : "PASS");
    return memory || gpu;
}
