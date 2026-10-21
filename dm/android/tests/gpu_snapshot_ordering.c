// #253 consumer-order and bounded A/B policy regression.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_VK_POLICY_TEST 1
#include "../native/nvn_vulkan_snapshot_policy.h"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static void reset_inputs(const char *env, const char *property) {
    if (env) setenv("DM_VK_GPU_SNAPSHOT_REUSE", env, 1); else unsetenv("DM_VK_GPU_SNAPSHOT_REUSE");
    gpu_snapshot_policy_set_property_for_test(property);
    gpu_snapshot_policy_reset_for_test();
}

int main(void) {
    // Policy arithmetic only: encoded=5 has not yet advanced for the current
    // draw, so the second binding must clone; encoded=6 is a prior encoded draw.
    CHECK(gpu_snapshot_inplace_allowed(6, 5) == 0);
    CHECK(gpu_snapshot_inplace_allowed(6, 6) == 1);
    CHECK(gpu_snapshot_inplace_allowed(6, 7) == 1);

    reset_inputs(NULL, NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 1);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "default"));

    reset_inputs(NULL, "0");
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "property"));

    reset_inputs(NULL, "1");
    CHECK(gpu_snapshot_reuse_enabled() == 1);

    // Explicit env wins over the property.
    reset_inputs("0", "1");
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "env"));

    reset_inputs("1", "0");
    CHECK(gpu_snapshot_reuse_enabled() == 1);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "env"));

    // Invalid env falls through to the property.
    reset_inputs("garbage", "0");
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "property"));

    // Read once per process: changing inputs without restart does not alter
    // the cached mode; reset simulates the next process start.
    reset_inputs("0", NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    setenv("DM_VK_GPU_SNAPSHOT_REUSE", "1", 1);
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    gpu_snapshot_policy_reset_for_test();
    CHECK(gpu_snapshot_reuse_enabled() == 1);

    if (failures) return 1;
    puts("PASS gpu snapshot policy: precedence/default/off/on/cache; ordering helper is policy only");
    return 0;
}
