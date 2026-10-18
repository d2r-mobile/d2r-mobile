// #42：验证层诊断的契约测试。显式 opt-in、required 缺失必须失败、optional 缺失必须写明未验证、
// 参数结构校验、层可用时真的捕获到一条已知非法用法。层不可用时返回 77（CTest SKIP），绝不当作通过。
// #42 contract test for validation diagnostics: explicit opt-in, required-missing must fail, optional-missing must report
// "unvalidated", option struct validation, and (when the layer exists) a deliberately invalid usage must be captured.
// When the layer is unavailable the binary returns 77 (CTest SKIP), never a pass.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define EXPECT(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static int layer_present(void) {
    uint32_t n = 0;
    if (vkEnumerateInstanceLayerProperties(&n, NULL) != VK_SUCCESS || !n) return 0;
    VkLayerProperties *p = calloc(n, sizeof *p);
    if (!p) return 0;
    int found = 0;
    if (vkEnumerateInstanceLayerProperties(&n, p) == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            if (!strcmp(p[i].layerName, "VK_LAYER_KHRONOS_validation")) { found = 1; break; }
    free(p);
    return found;
}

int main(void) {
    uint32_t loader = VK_API_VERSION_1_0;
    VkResult vr = vkEnumerateInstanceVersion(&loader);
    if (vr != VK_SUCCESS) loader = VK_API_VERSION_1_0;
    int present = layer_present();
    printf("validation test: loader %u.%u, VK_LAYER_KHRONOS_validation %s\n",
           VK_VERSION_MAJOR(loader), VK_VERSION_MINOR(loader), present ? "present" : "absent");

    // 1. 参数结构必须精确匹配；错的 struct_size 不得被当成"没请求"。 | wrong struct_size is rejected, not treated as "off".
    dm_vk bad = {0};
    dm_vk_options broken = {0, DM_VK_VALIDATION_REQUIRED};
    EXPECT(dm_vk_create_ex(&broken, &bad) == VK_ERROR_INITIALIZATION_FAILED);
    dm_vk_destroy(&bad);

    // 2. required 且层缺失 → VK_ERROR_LAYER_NOT_PRESENT，且 out 保持清零。 | required + missing -> error, out stays zeroed.
    if (!present) {
        dm_vk vk = {0};
        dm_vk_options options = DM_VK_OPTIONS_INIT;
        options.validation = DM_VK_VALIDATION_REQUIRED;
        VkResult r = dm_vk_create_ex(&options, &vk);
        EXPECT(r == VK_ERROR_LAYER_NOT_PRESENT);
        EXPECT(!vk.instance && !vk.device);
        EXPECT(!dm_vk_validation_enabled(&vk));
        dm_vk_destroy(&vk);
    }

    // 3. optional：有则启用；缺失则成功但状态必须写明 unavailable（不谎称已验证）。 | optional: enabled if present, otherwise
    //    the status must say unavailable - never pretend it was validated.
    {
        dm_vk vk = {0};
        dm_vk_options options = DM_VK_OPTIONS_INIT;
        options.validation = DM_VK_VALIDATION_OPTIONAL;
        VkResult r = dm_vk_create_ex(&options, &vk);
        EXPECT(r == VK_SUCCESS);
        if (r == VK_SUCCESS) {
            printf("optional mode: %s (enabled=%d)\n", dm_vk_validation_status(&vk), dm_vk_validation_enabled(&vk));
            EXPECT(dm_vk_validation_enabled(&vk) == (present ? 1 : 0));
            if (!present) EXPECT(strstr(dm_vk_validation_status(&vk), "unavailable") != NULL);
            else EXPECT(dm_vk_validation_errors(&vk) == 0);   // 干净创建不应报错 | a clean creation must have zero errors
            dm_vk_destroy(&vk);
        }
    }

    if (!present) {
        printf("validation test: layer unavailable, actual validation checks SKIPPED (not a pass)\n");
        return failures ? 1 : 77;
    }

    // 4. layer 可用：required 成功，并用一条已知非法用法证明诊断真的接通（未捕获到就是失败）。
    //    Layer present: required succeeds and a known-invalid usage must be captured; not captured is a failure.
    dm_vk vk = {0};
    dm_vk_options options = DM_VK_OPTIONS_INIT;
    options.validation = DM_VK_VALIDATION_REQUIRED;
    VkResult r = dm_vk_create_ex(&options, &vk);
    EXPECT(r == VK_SUCCESS);
    if (r != VK_SUCCESS) { printf("validation test: %u checks, %u failures\n", checks, failures); return 1; }
    EXPECT(dm_vk_validation_enabled(&vk));
    printf("required mode: %s (instance %u.%u, driver %u.%u)\n", dm_vk_validation_status(&vk),
           VK_VERSION_MAJOR(vk.api_version), VK_VERSION_MINOR(vk.api_version),
           VK_VERSION_MAJOR(vk.driver_api_version), VK_VERSION_MINOR(vk.driver_api_version));

    dm_vk_buffer_info info = {.size = 16, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    dm_vk_buffer buffer = {0};
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkResult made = dm_vk_buffer_create(&vk, &info, &buffer);
    if (made == VK_SUCCESS) made = dm_vk_begin(&vk, &command);
    if (made == VK_SUCCESS) {
        // 已知非法：索引缓冲没有 INDEX_BUFFER usage（VUID-vkCmdBindIndexBuffer-buffer-08784）。
        // Known invalid: binding a buffer without INDEX_BUFFER usage (VUID-vkCmdBindIndexBuffer-buffer-08784).
        vkCmdBindIndexBuffer(command, buffer.handle, 0, VK_INDEX_TYPE_UINT16);
        vkEndCommandBuffer(command);
        vkFreeCommandBuffers(vk.device, vk.pool, 1, &command);
        vkDeviceWaitIdle(vk.device);
    }
    EXPECT(made == VK_SUCCESS);
    char first[256] = {0};
    dm_vk_validation_first_error(&vk, first, sizeof first);
    printf("captured validation errors: %u (first: %.120s)\n", dm_vk_validation_errors(&vk), first);
    EXPECT(dm_vk_validation_errors(&vk) > 0);
    EXPECT(dm_vk_validation_capture(&vk) == 1);
    EXPECT(dm_vk_validation_layer_version(&vk)[0]);
    dm_vk_buffer_destroy(&vk, &buffer);
    dm_vk_destroy(&vk);
    printf("validation test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
