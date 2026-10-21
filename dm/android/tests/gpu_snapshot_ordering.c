// #253 consumer-order, in-place isolation and bounded A/B policy regression.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_VK_POLICY_TEST 1
#include "../native/nvn_vulkan_snapshot_policy.h"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static void reset_inputs(const char *reuse_env, const char *reuse_property,
        const char *inplace_env, const char *inplace_property) {
    if (reuse_env) setenv("DM_VK_GPU_SNAPSHOT_REUSE", reuse_env, 1);
    else unsetenv("DM_VK_GPU_SNAPSHOT_REUSE");
    if (inplace_env) setenv("DM_VK_GPU_SNAPSHOT_INPLACE", inplace_env, 1);
    else unsetenv("DM_VK_GPU_SNAPSHOT_INPLACE");
    gpu_snapshot_policy_set_property_for_test(reuse_property);
    gpu_snapshot_policy_set_inplace_property_for_test(inplace_property);
    gpu_snapshot_policy_reset_for_test();
}

int main(void) {
    // This is the production branch selector, but it is policy arithmetic only:
    // it is not proof of actual GPU execution ordering.
    CHECK(gpu_snapshot_choose_action(0, 0, 1, 6, 5) == GPU_SNAPSHOT_ACTION_HIT);
    CHECK(gpu_snapshot_choose_action(0, 0, 0, 6, 5) == GPU_SNAPSHOT_ACTION_HIT);
    CHECK(gpu_snapshot_choose_action(0, 4, 1, 6, 5) == GPU_SNAPSHOT_ACTION_COW); // pending consumer
    CHECK(gpu_snapshot_choose_action(0, 4, 1, 6, 6) == GPU_SNAPSHOT_ACTION_INPLACE); // encoded consumer
    CHECK(gpu_snapshot_choose_action(0, 4, 0, 6, 6) == GPU_SNAPSHOT_ACTION_COW); // disabled in-place
    CHECK(gpu_snapshot_choose_action(0, 4, 0, 6, 5) == GPU_SNAPSHOT_ACTION_COW);
    CHECK(gpu_snapshot_choose_action(1, 0, 1, 6, 6) == GPU_SNAPSHOT_ACTION_FULL);

    reset_inputs(NULL, NULL, NULL, NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 1);
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "default"));
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "default"));

    // Independent caches: an explicit in-place override does not change reuse.
    reset_inputs(NULL, NULL, "1", NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 1);
    CHECK(gpu_snapshot_inplace_enabled() == 1);

    // An explicit reuse override does not change the conservative in-place default.
    reset_inputs("0", NULL, NULL, NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(gpu_snapshot_inplace_enabled() == 0);

    reset_inputs(NULL, "0", NULL, NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_reuse_source_name(), "property"));

    reset_inputs(NULL, NULL, NULL, "0");
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "property"));

    reset_inputs(NULL, NULL, NULL, "1");
    CHECK(gpu_snapshot_inplace_enabled() == 1);
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "property"));

    reset_inputs(NULL, NULL, "0", "1");
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "env"));

    reset_inputs(NULL, NULL, "1", "0");
    CHECK(gpu_snapshot_inplace_enabled() == 1);
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "env"));

    reset_inputs(NULL, NULL, "garbage", "0");
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "property"));

    reset_inputs(NULL, NULL, NULL, "garbage");
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    CHECK(!strcmp(gpu_snapshot_inplace_source_name(), "default"));

    // Read once per process: changing inputs without restart does not alter
    // either cached mode; reset simulates the next process start.
    reset_inputs("0", NULL, "0", NULL);
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    setenv("DM_VK_GPU_SNAPSHOT_REUSE", "1", 1);
    setenv("DM_VK_GPU_SNAPSHOT_INPLACE", "1", 1);
    CHECK(gpu_snapshot_reuse_enabled() == 0);
    CHECK(gpu_snapshot_inplace_enabled() == 0);
    unsetenv("DM_VK_GPU_SNAPSHOT_REUSE");
    unsetenv("DM_VK_GPU_SNAPSHOT_INPLACE");
    gpu_snapshot_policy_reset_for_test();
    CHECK(gpu_snapshot_reuse_enabled() == 1);
    CHECK(gpu_snapshot_inplace_enabled() == 0);

    if (failures) return 1;
    puts("PASS gpu snapshot policy: conservative COW default, explicit inplace override, restart cache; policy only");
    return 0;
}
