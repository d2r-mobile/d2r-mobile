// #253 GPU snapshot consumer-ordering and bounded A/B policy.
// A cached buffer may be patched in place only when every consumer that has
// touched it is already encoded; a second binding in the same draw must fork a
// clone instead. This policy is not proof of actual GPU ordering.
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

typedef enum {
    GPU_SNAPSHOT_SOURCE_DEFAULT = 0,
    GPU_SNAPSHOT_SOURCE_ENV,
    GPU_SNAPSHOT_SOURCE_PROPERTY
} gpu_snapshot_policy_source;

typedef enum {
    GPU_SNAPSHOT_ACTION_HIT = 0,
    GPU_SNAPSHOT_ACTION_INPLACE,
    GPU_SNAPSHOT_ACTION_COW,
    GPU_SNAPSHOT_ACTION_FULL
} gpu_snapshot_action;

// Actual production branch selection for a certified snapshot plan:
// no dirty runs -> existing cache hit; dirty runs -> in-place only when the
// mode is enabled and every prior consumer is encoded; otherwise GPU COW.
// A full plan (including the dense-patch fallback) never uses either delta arm.
static gpu_snapshot_action gpu_snapshot_choose_action(int full, unsigned run_count,
        int inplace_enabled, uint64_t last_acquired, uint64_t encoded) {
    if (full) return GPU_SNAPSHOT_ACTION_FULL;
    if (!run_count) return GPU_SNAPSHOT_ACTION_HIT;
    if (inplace_enabled && gpu_snapshot_inplace_allowed(last_acquired, encoded))
        return GPU_SNAPSHOT_ACTION_INPLACE;
    return GPU_SNAPSHOT_ACTION_COW;
}

static int gpu_snapshot_reuse_cached = -1;
static int gpu_snapshot_inplace_cached = -1;
static gpu_snapshot_policy_source gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_SOURCE_DEFAULT;
static gpu_snapshot_policy_source gpu_snapshot_inplace_source_cached = GPU_SNAPSHOT_SOURCE_DEFAULT;

#ifdef DM_VK_POLICY_TEST
static const char *gpu_snapshot_test_reuse_property;
static const char *gpu_snapshot_test_inplace_property;
static void gpu_snapshot_policy_set_property_for_test(const char *value) {
    gpu_snapshot_test_reuse_property = value;
}
static void gpu_snapshot_policy_set_inplace_property_for_test(const char *value) {
    gpu_snapshot_test_inplace_property = value;
}
#endif

// 0/1 are the only accepted values; anything else falls through to the next
// input. The properties are app-specific and debug-namespaced.
static int gpu_snapshot_parse_mode(const char *value, int *mode) {
    if (!value || !mode) return 0;
    if (value[0] == '0' && value[1] == 0) { *mode = 0; return 1; }
    if (value[0] == '1' && value[1] == 0) { *mode = 1; return 1; }
    return 0;
}

static const char *gpu_snapshot_read_property(const char *name) {
#ifdef __ANDROID__
    static char value[PROP_VALUE_MAX];
    if (__system_property_get(name, value) <= 0) return NULL;
    return value;
#elif defined(DM_VK_POLICY_TEST)
    if (!strcmp(name, "debug.d2r.gpu_snapshot_reuse")) return gpu_snapshot_test_reuse_property;
    if (!strcmp(name, "debug.d2r.gpu_snapshot_inplace")) return gpu_snapshot_test_inplace_property;
    return NULL;
#else
    (void)name;
    return NULL;
#endif
}

// Read once per process. Changing env/property requires an app restart.
// Precedence: explicit env > app debug property > default enabled.
static int gpu_snapshot_mode_enabled(const char *env_name, const char *property_name,
        int *cached, gpu_snapshot_policy_source *source) {
    if (*cached < 0) {
        int mode = 1;
        const char *env = getenv(env_name);
        if (gpu_snapshot_parse_mode(env, &mode)) {
            *source = GPU_SNAPSHOT_SOURCE_ENV;
        } else if (gpu_snapshot_parse_mode(gpu_snapshot_read_property(property_name), &mode)) {
            *source = GPU_SNAPSHOT_SOURCE_PROPERTY;
        } else {
            mode = 1;
            *source = GPU_SNAPSHOT_SOURCE_DEFAULT;
        }
        *cached = mode;
    }
    return *cached;
}

static int gpu_snapshot_reuse_enabled(void) {
    return gpu_snapshot_mode_enabled("DM_VK_GPU_SNAPSHOT_REUSE",
        "debug.d2r.gpu_snapshot_reuse", &gpu_snapshot_reuse_cached,
        &gpu_snapshot_reuse_source_cached);
}

static int gpu_snapshot_inplace_enabled(void) {
    return gpu_snapshot_mode_enabled("DM_VK_GPU_SNAPSHOT_INPLACE",
        "debug.d2r.gpu_snapshot_inplace", &gpu_snapshot_inplace_cached,
        &gpu_snapshot_inplace_source_cached);
}

static const char *gpu_snapshot_source_name(gpu_snapshot_policy_source source) {
    switch (source) {
    case GPU_SNAPSHOT_SOURCE_ENV: return "env";
    case GPU_SNAPSHOT_SOURCE_PROPERTY: return "property";
    default: return "default";
    }
}

static const char *gpu_snapshot_reuse_source_name(void) {
    return gpu_snapshot_source_name(gpu_snapshot_reuse_source_cached);
}

static const char *gpu_snapshot_inplace_source_name(void) {
    return gpu_snapshot_source_name(gpu_snapshot_inplace_source_cached);
}

#ifdef DM_VK_POLICY_TEST
static void gpu_snapshot_policy_reset_for_test(void) {
    gpu_snapshot_reuse_cached = -1;
    gpu_snapshot_inplace_cached = -1;
    gpu_snapshot_reuse_source_cached = GPU_SNAPSHOT_SOURCE_DEFAULT;
    gpu_snapshot_inplace_source_cached = GPU_SNAPSHOT_SOURCE_DEFAULT;
}
#endif
