// #253 GPU snapshot consumer-ordering and bounded A/B policy.
// A cached buffer may be patched in place only when every consumer that has
// touched it is already encoded; a second binding in the same draw must fork a
// clone instead. This helper is a policy check, not proof of GPU ordering.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

static int gpu_snapshot_inplace_allowed(uint64_t last_acquired, uint64_t encoded) {
    return last_acquired <= encoded;
}

typedef enum { GPU_SNAPSHOT_REUSE_DEFAULT = 0, GPU_SNAPSHOT_REUSE_ENV, GPU_SNAPSHOT_REUSE_PROPERTY } gpu_snapshot_reuse_source;
static int gpu_snapshot_reuse_cached = -1;
static gpu_snapshot_reuse_source gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_REUSE_DEFAULT;

#ifdef DM_VK_POLICY_TEST
static const char *gpu_snapshot_test_property;
static void gpu_snapshot_policy_set_property_for_test(const char *value) { gpu_snapshot_test_property = value; }
#endif

// 0/1 are the only accepted values; anything else falls through to the next
// input. The property is app-specific and debug-namespaced.
static int gpu_snapshot_parse_mode(const char *value, int *mode) {
    if (!value || !mode) return 0;
    if (value[0] == '0' && value[1] == 0) { *mode = 0; return 1; }
    if (value[0] == '1' && value[1] == 0) { *mode = 1; return 1; }
    return 0;
}

static const char *gpu_snapshot_read_property(void) {
#ifdef __ANDROID__
    static char value[PROP_VALUE_MAX];
    if (__system_property_get("debug.d2r.gpu_snapshot_reuse", value) <= 0) return NULL;
    return value;
#elif defined(DM_VK_POLICY_TEST)
    return gpu_snapshot_test_property;
#else
    return NULL;
#endif
}

// Read once per process. Changing env/property requires an app restart.
// Precedence: explicit env > app debug property > default enabled.
static int gpu_snapshot_reuse_enabled(void) {
    if (gpu_snapshot_reuse_cached < 0) {
        int mode = 1;
        const char *env = getenv("DM_VK_GPU_SNAPSHOT_REUSE");
        if (gpu_snapshot_parse_mode(env, &mode)) {
            gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_REUSE_ENV;
        } else if (gpu_snapshot_parse_mode(gpu_snapshot_read_property(), &mode)) {
            gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_REUSE_PROPERTY;
        } else {
            mode = 1;
            gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_REUSE_DEFAULT;
        }
        gpu_snapshot_reuse_cached = mode;
    }
    return gpu_snapshot_reuse_cached;
}

static const char *gpu_snapshot_reuse_source_name(void) {
    switch (gpu_snapshot_reuse_source_cached) {
    case GPU_SNAPSHOT_REUSE_ENV: return "env";
    case GPU_SNAPSHOT_REUSE_PROPERTY: return "property";
    default: return "default";
    }
}

#ifdef DM_VK_POLICY_TEST
static void gpu_snapshot_policy_reset_for_test(void) {
    gpu_snapshot_reuse_cached = -1;
    gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_REUSE_DEFAULT;
}
#endif
