#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include "legacy_digest.h"

static VkDevice device;
static VkPipelineCache pipeline_cache;
static unsigned presents, gpu_subgroup_size = 64;
static uint8_t driver_blob[64];
static uint8_t disk_expected[64];
static unsigned create_calls, initial_bytes, snapshot_calls;
static int reject_initial, incomplete_snapshot, fail_rename;
static struct {
    unsigned actual_driver_id, using_custom, requested_subgroup_size;
    char driver_name[64], driver_directory[64];
} vk_api = {1, 1, 64, "fixture-turnip", "/fixture/driver"};

static uint64_t dm_android_clock_ns(clockid_t clock) {
    struct timespec time;
    assert(!clock_gettime(clock, &time));
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}
static void test_log(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}
#define LOG(...) test_log(__VA_ARGS__)
#define VK_CHECK(result) assert((result) == VK_SUCCESS)

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice d, const VkPipelineCacheCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkPipelineCache *cache) {
    (void)d; (void)allocator;
    create_calls++;
    if (info->initialDataSize) {
        initial_bytes = (unsigned)info->initialDataSize;
        assert(info->initialDataSize == sizeof driver_blob);
        assert(!memcmp(info->pInitialData, driver_blob, sizeof driver_blob));
        if (reject_initial) return VK_ERROR_INITIALIZATION_FAILED;
    }
    *cache = (VkPipelineCache)(uintptr_t)1;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice d, VkPipelineCache cache,
        size_t *size, void *data) {
    (void)d; (void)cache;
    snapshot_calls++;
    if (!data) { *size = sizeof driver_blob; return VK_SUCCESS; }
    assert(*size >= sizeof driver_blob);
    memcpy(data, driver_blob, sizeof driver_blob);
    *size = sizeof driver_blob;
    return incomplete_snapshot ? VK_INCOMPLETE : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d, VkPipelineCache cache,
        uint32_t count, const VkGraphicsPipelineCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkPipeline *pipeline) {
    (void)d; (void)cache; (void)count; (void)info; (void)allocator; (void)pipeline;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice d, VkPipelineCache cache,
        uint32_t count, const VkComputePipelineCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkPipeline *pipeline) {
    (void)d; (void)cache; (void)count; (void)info; (void)allocator; (void)pipeline;
    return VK_SUCCESS;
}

#include <fcntl.h>
#include <unistd.h>
static int test_renameat(int from, const char *source, int to, const char *destination) {
    if (fail_rename) { errno = EIO; return -1; }
    return renameat(from, source, to, destination);
}
static int test_thread_name(pthread_t thread, const char *name) {
#ifdef __APPLE__
    (void)thread;
    return pthread_setname_np(name);
#else
    return pthread_setname_np(thread, name);
#endif
}

/* Compile the production Android namespace and disk path with a synthetic Vulkan driver. */
#ifndef __ANDROID__
#define __ANDROID__ 1
#endif
#define renameat test_renameat
#define pthread_setname_np test_thread_name
#include "nvn_vulkan_pipeline_cache.inc"
#undef pthread_setname_np
#undef renameat

static void disconnect_disk(void) {
    if (pipeline_disk_lock >= 0) close(pipeline_disk_lock);
    if (pipeline_disk_dir >= 0) close(pipeline_disk_dir);
    pipeline_disk_lock = pipeline_disk_dir = -1;
}
static void expect_blob(int accepted) {
    size_t size = 123;
    void *data = pipeline_disk_load(&size);
    assert((data != NULL) == accepted);
    assert(size == (accepted ? sizeof driver_blob : 0));
    if (data) assert(!memcmp(data, disk_expected, size));
    free(data);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(!setenv("DM_VK_PIPELINE_CACHE_DIR", argv[1], 1));
    unsetenv("DM_VK_SHADERS");
    VkPhysicalDeviceProperties properties = {
        .vendorID = 0x5143, .deviceID = 0x800, .driverVersion = 7, .apiVersion = VK_API_VERSION_1_1,
        .pipelineCacheUUID = {1, 2, 3, 4}
    };
    pipeline_cache_initialize(&properties);
    assert(pipeline_disk_dir >= 0 && create_calls == 1 && initial_bytes == 0);
    pipeline_disk_put32(driver_blob, 32);
    pipeline_disk_put32(driver_blob + 4, VK_PIPELINE_CACHE_HEADER_VERSION_ONE);
    pipeline_disk_put32(driver_blob + 8, properties.vendorID);
    pipeline_disk_put32(driver_blob + 12, properties.deviceID);
    memcpy(driver_blob + 16, properties.pipelineCacheUUID, VK_UUID_SIZE);
    memcpy(disk_expected, driver_blob, sizeof disk_expected);
    expect_blob(0);
    assert(pipeline_disk_write(driver_blob, sizeof driver_blob));
    expect_blob(1);
    const off_t corrupt_offsets[] = {0, 8, 40, 48, 80, 84, 88, 92, 96, 120};
    for (unsigned i = 0; i < sizeof corrupt_offsets / sizeof *corrupt_offsets; i++) {
        int fd = openat(pipeline_disk_dir, pipeline_disk_name, O_RDWR);
        assert(fd >= 0);
        uint8_t byte;
        assert(pread(fd, &byte, 1, corrupt_offsets[i]) == 1);
        byte ^= 0xff;
        assert(pwrite(fd, &byte, 1, corrupt_offsets[i]) == 1);
        close(fd);
        expect_blob(0);
        assert(pipeline_disk_write(driver_blob, sizeof driver_blob));
    }
    int fd = openat(pipeline_disk_dir, pipeline_disk_name, O_WRONLY);
    assert(fd >= 0 && !ftruncate(fd, PIPELINE_DISK_HEADER + 31));
    close(fd);
    expect_blob(0);
    assert(!unlinkat(pipeline_disk_dir, pipeline_disk_name, 0));
    assert(!symlinkat("writer.lock", pipeline_disk_dir, pipeline_disk_name));
    expect_blob(0);
    assert(!unlinkat(pipeline_disk_dir, pipeline_disk_name, 0));
    assert(pipeline_disk_write(driver_blob, sizeof driver_blob));

    char original[80];
    strcpy(original, pipeline_disk_name);
    for (unsigned field = 0; field < 10; field++) {
        disconnect_disk();
        VkPhysicalDeviceProperties changed = properties;
        switch (field) {
            case 0: changed.vendorID++; break;
            case 1: changed.deviceID++; break;
            case 2: changed.driverVersion++; break;
            case 3: changed.apiVersion++; break;
            case 4: changed.pipelineCacheUUID[0]++; break;
            case 5: gpu_subgroup_size = 32; break;
            case 6: vk_api.actual_driver_id++; break;
            case 7: strcpy(vk_api.driver_name, "other-driver"); break;
            case 8: vk_api.using_custom = 0; break;
            case 9: assert(!setenv("DM_VK_SHADERS", "/other/profile", 1)); break;
        }
        pipeline_cache_initialize(&changed);
        assert(strcmp(original, pipeline_disk_name));
        expect_blob(0);
        gpu_subgroup_size = 64;
        vk_api.actual_driver_id = 1;
        vk_api.using_custom = 1;
        strcpy(vk_api.driver_name, "fixture-turnip");
        unsetenv("DM_VK_SHADERS");
    }
    disconnect_disk();
    reject_initial = 1;
    unsigned before = create_calls;
    pipeline_cache_initialize(&properties);
    assert(create_calls == before + 2 && initial_bytes == sizeof driver_blob);
    expect_blob(1);

    atomic_store(&pipeline_cache_generation, 2);
    driver_blob[40] = 0x5a;
    incomplete_snapshot = 1;
    pipeline_disk_save();
    assert(snapshot_calls == 4 && atomic_load(&pipeline_cache_saved) == 0);
    expect_blob(1);
    incomplete_snapshot = 0;
    fail_rename = 1;
    pipeline_disk_save();
    assert(atomic_load(&pipeline_cache_saved) == 0);
    expect_blob(1);
    fail_rename = 0;
    assert(pipeline_create_graphics(NULL, NULL) == VK_SUCCESS);
    assert(pipeline_create_compute(NULL, NULL) == VK_SUCCESS);
    pipeline_disk_poll();
    pipeline_disk_stop_join();
    assert(!pipeline_disk_started && atomic_load(&pipeline_cache_saved) == 4);
    memcpy(disk_expected, driver_blob, sizeof disk_expected);
    expect_blob(1);
    atomic_fetch_add(&pipeline_cache_generation, 1);
    pipeline_disk_stop_join();
    assert(!pipeline_disk_started && atomic_load(&pipeline_cache_saved) == 5);
    disconnect_disk();
    puts("PASS: production pipeline cache corruption, namespace, failed-save preservation and final flush");
    return 0;
}
