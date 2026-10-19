// #235 owned decision regression: drives the same helper used by
// shader_load_by_key, with tiny SPIR-V inputs and explicit feature values.
#include "nvn_vulkan_caps.h"

#include <stdint.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    uint32_t tess[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 3u};
    uint32_t storage_write[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 56u};
    uint32_t multiview[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 57u};
    uint32_t draw_parameters[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 4427u};
    uint32_t unrelated[7] = {0x07230203u, 0x00010000u, 0, 0, 0, (2u << 16) | 17u, 1u};

    // StorageImageWriteWithoutFormat: reject only when unsupported.
    CHECK(dm_nvn_shader_feature_decision(storage_write, sizeof storage_write, 0, 0, 0, 0) ==
          DM_NVN_SHADER_FEATURE_STORAGE_WRITE_UNSUPPORTED);
    CHECK(dm_nvn_shader_feature_decision(storage_write, sizeof storage_write, 0, 0, 1, 0) ==
          DM_NVN_SHADER_FEATURE_OK);

    // MultiViewport: reject only when unsupported.
    CHECK(dm_nvn_shader_feature_decision(multiview, sizeof multiview, 0, 0, 0, 0) ==
          DM_NVN_SHADER_FEATURE_MULTIVIEW_UNSUPPORTED);
    CHECK(dm_nvn_shader_feature_decision(multiview, sizeof multiview, 0, 1, 0, 0) ==
          DM_NVN_SHADER_FEATURE_OK);

    // Tessellation: unsupported and feature-enabled-but-pipeline-pending are distinct.
    CHECK(dm_nvn_shader_feature_decision(tess, sizeof tess, 0, 0, 0, 0) ==
          DM_NVN_SHADER_FEATURE_TESS_UNSUPPORTED);
    CHECK(dm_nvn_shader_feature_decision(tess, sizeof tess, 1, 0, 0, 0) ==
          DM_NVN_SHADER_FEATURE_TESS_PIPELINE_PENDING);

    // DrawParameters remains a requirement, not a hard feature check here.
    CHECK(dm_nvn_shader_feature_decision(draw_parameters, sizeof draw_parameters, 0, 0, 0, 0) ==
          DM_NVN_SHADER_FEATURE_NEED_DRAW_PARAMETERS);
    CHECK(dm_nvn_shader_feature_decision(draw_parameters, sizeof draw_parameters, 0, 0, 0, 1) ==
          DM_NVN_SHADER_FEATURE_OK);

    // Unrelated shader continues even with every optional feature absent.
    CHECK(dm_nvn_shader_feature_decision(unrelated, sizeof unrelated, 0, 0, 0, 0) ==
          DM_NVN_SHADER_FEATURE_OK);

    // Production optional/required refusal boundary: optional skips, required
    // must continue into the fatal switch; a supported or unrelated shader
    // is never skipped.
    const dm_nvn_shader_feature_status rejected[] = {
        DM_NVN_SHADER_FEATURE_NEED_DRAW_PARAMETERS,
        DM_NVN_SHADER_FEATURE_TESS_UNSUPPORTED,
        DM_NVN_SHADER_FEATURE_TESS_PIPELINE_PENDING,
        DM_NVN_SHADER_FEATURE_STORAGE_WRITE_UNSUPPORTED,
        DM_NVN_SHADER_FEATURE_MULTIVIEW_UNSUPPORTED,
    };
    for (unsigned i = 0; i < sizeof rejected / sizeof *rejected; i++) {
        CHECK(dm_nvn_shader_feature_can_skip(rejected[i], 0) == 1);
        CHECK(dm_nvn_shader_feature_can_skip(rejected[i], 1) == 0);
    }
    CHECK(dm_nvn_shader_feature_can_skip(DM_NVN_SHADER_FEATURE_OK, 0) == 0);
    CHECK(dm_nvn_shader_feature_can_skip(DM_NVN_SHADER_FEATURE_OK, 1) == 0);

    // Guard sanity: an input with no capability is not accepted as storage/tess.
    CHECK(dm_nvn_spirv_has_capability(unrelated, sizeof unrelated, 56u) == 0);

    if (failures) return 1;
    puts("PASS nvn shader feature decisions: storage/viewport/tess/unrelated");
    return 0;
}
