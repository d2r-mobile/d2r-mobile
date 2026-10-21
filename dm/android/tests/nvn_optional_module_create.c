// #246 owned boundary regression: inject a controllable VkCreateShaderModule
// result into the same helper used by shader_load_by_key and verify optional
// cleanup/lock release, required fatal behavior, and successful continuation.
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

// Minimal Vulkan ABI for this host boundary test. The production helper is
// compiled against the real Vulkan headers; this test only needs opaque
// pointer types and the result constants used by the injected API.
typedef int VkResult;
typedef void *VkDevice;
typedef void *VkShaderModule;
typedef struct VkShaderModuleCreateInfo VkShaderModuleCreateInfo;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
#define VK_NULL_HANDLE NULL

#define LOG(...) ((void)0)
#define die(...) do { _exit(77); } while (0)
#include "nvn_vulkan_shader_module.inc"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static VkResult fake_fail(VkDevice device, const VkShaderModuleCreateInfo *info,
                          const VkAllocationCallbacks *alloc, VkShaderModule *module) {
    (void)device; (void)info; (void)alloc;
    *module = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

static VkResult fake_success(VkDevice device, const VkShaderModuleCreateInfo *info,
                             const VkAllocationCallbacks *alloc, VkShaderModule *module) {
    (void)device; (void)info; (void)alloc;
    *module = (VkShaderModule)(uintptr_t)0x1;
    return VK_SUCCESS;
}

static void *owned_block(void) { return malloc(64); }

int main(void) {
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    VkShaderModule module = VK_NULL_HANDLE;
    void *code, *meta, *entry;

    // Optional failure: exact VkResult, cleanup, unlock, no module retained.
    pthread_mutex_lock(&lock);
    code = owned_block(); meta = owned_block(); entry = owned_block();
    VkResult result = shader_module_create_or_skip(
        fake_fail, VK_NULL_HANDLE, NULL, &module, 0, "optional-fail",
        code, meta, entry, &lock);
    CHECK(result == VK_ERROR_OUT_OF_DEVICE_MEMORY);
    CHECK(module == VK_NULL_HANDLE);
    CHECK(pthread_mutex_trylock(&lock) == 0);
    pthread_mutex_unlock(&lock);

    // Success: helper must not cleanup or unlock; caller continues and owns
    // code/meta/entry (module creation itself succeeds).
    pthread_mutex_lock(&lock);
    code = owned_block(); meta = owned_block(); entry = owned_block();
    module = VK_NULL_HANDLE;
    result = shader_module_create_or_skip(
        fake_success, VK_NULL_HANDLE, NULL, &module, 0, "success",
        code, meta, entry, &lock);
    CHECK(result == VK_SUCCESS);
    CHECK(module == (VkShaderModule)(uintptr_t)0x1);
    CHECK(pthread_mutex_trylock(&lock) == EBUSY);
    free(code); free(meta); free(entry);
    pthread_mutex_unlock(&lock);

    // Required failure: still fatal, in a child so the test process survives.
    pid_t child = fork();
    if (child == 0) {
        pthread_mutex_lock(&lock);
        code = owned_block(); meta = owned_block(); entry = owned_block();
        (void)shader_module_create_or_skip(
            fake_fail, VK_NULL_HANDLE, NULL, &module, 1, "required-fail",
            code, meta, entry, &lock);
        _exit(0);
    }
    int status = 0;
    CHECK(child > 0);
    if (child > 0) {
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
    }

    if (failures) return 1;
    puts("PASS nvn optional module create: cleanup/unlock/no-publish/required/success");
    return 0;
}
