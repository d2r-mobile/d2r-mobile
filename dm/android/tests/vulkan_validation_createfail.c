// #42：实例创建失败的清理契约。注入 loader 边界（vkCreateInstance 返回失败），确认 dm_vk_create_ex
// 不留下部分状态：out 全零、无 diag、destroy 可安全重复调用。不需要 GPU。
// #42 cleanup contract for instance-creation failure. The loader boundary is injected (vkCreateInstance fails); the test
// checks that dm_vk_create_ex leaves no partial state: out stays zeroed, no diag, destroy is safe to call again. No GPU.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define EXPECT(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

// 由 CMake 以 -DvkCreateInstance=dm_test_vkCreateInstance 注入 | injected by CMake via -DvkCreateInstance=...
VKAPI_ATTR VkResult VKAPI_CALL dm_test_vkCreateInstance(const VkInstanceCreateInfo *info,
        const VkAllocationCallbacks *alloc, VkInstance *instance) {
    (void)info; (void)alloc; (void)instance;
    return VK_ERROR_INITIALIZATION_FAILED;
}

int main(void) {
    dm_vk_options options = DM_VK_OPTIONS_INIT;
    options.validation = DM_VK_VALIDATION_OPTIONAL;   // 先走到实例创建，再由注入的 loader 失败 | reach the injected loader failure
    dm_vk vk = {0};
    VkResult r = dm_vk_create_ex(&options, &vk);
    if (r != VK_ERROR_INITIALIZATION_FAILED) fprintf(stderr, "create_ex returned %d\n", (int)r);
    EXPECT(r == VK_ERROR_INITIALIZATION_FAILED);
    EXPECT(!vk.instance && !vk.device && !vk.queue && !vk.pool);
    EXPECT(!vk.diag);
    EXPECT(!dm_vk_validation_enabled(&vk));
    EXPECT(!strcmp(dm_vk_validation_status(&vk), "disabled"));
    dm_vk_destroy(&vk);   // 幂等 | idempotent
    dm_vk_destroy(&vk);
    printf("vulkan_validation_createfail: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
