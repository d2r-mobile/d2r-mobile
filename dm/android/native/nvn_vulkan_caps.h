// SPIR-V capability query for the Android Vulkan backend.
// Kept separate from Vulkan types so the owned host test can exercise the
// capability decision without a Vulkan device.
#pragma once
#include <stddef.h>
#include <stdint.h>

// SPIR-V OpCapability = 17. Current required IDs:
// 3=Tessellation, 56=StorageImageWriteWithoutFormat, 57=MultiViewport,
// 4427=ShaderDrawParameters (SPV_KHR_shader_draw_parameters).
static inline int dm_nvn_spirv_has_capability(const void *data, size_t bytes, uint32_t capability) {
    if (!data || bytes < 5 * sizeof(uint32_t)) return 0;
    const uint32_t *words = (const uint32_t *)data;
    size_t count = bytes / sizeof(uint32_t);
    if (words[0] != 0x07230203u) return 0;
    for (size_t i = 5; i < count;) {
        uint32_t instruction = words[i];
        uint32_t opcode = instruction & 0xffffu;
        uint32_t length = instruction >> 16;
        if (length == 0 || length > count - i) break;
        if (opcode == 17u && length >= 2 && words[i + 1] == capability) return 1;
        i += length;
    }
    return 0;
}

static inline int dm_nvn_spirv_uses_shader_draw_parameters(const void *data, size_t bytes) {
    return dm_nvn_spirv_has_capability(data, bytes, 4427u);
}

typedef enum {
    DM_NVN_SHADER_FEATURE_OK = 0,
    DM_NVN_SHADER_FEATURE_NEED_DRAW_PARAMETERS,
    DM_NVN_SHADER_FEATURE_TESS_UNSUPPORTED,
    DM_NVN_SHADER_FEATURE_TESS_PIPELINE_PENDING,
    DM_NVN_SHADER_FEATURE_STORAGE_WRITE_UNSUPPORTED,
    DM_NVN_SHADER_FEATURE_MULTIVIEW_UNSUPPORTED
} dm_nvn_shader_feature_status;

// Production decision shared by shader_load_by_key and the owned regression.
// Feature arguments are the actual queried/enabled physical values: 0 means
// unsupported or not enabled. Do not replace this with an independent mirror.
static inline dm_nvn_shader_feature_status dm_nvn_shader_feature_decision(
    const void *data, size_t bytes, int tessellation_supported,
    int multiview_supported, int storage_write_supported,
    int draw_parameters_supported) {
    if (dm_nvn_spirv_has_capability(data, bytes, 3u))
        return tessellation_supported ? DM_NVN_SHADER_FEATURE_TESS_PIPELINE_PENDING
                                      : DM_NVN_SHADER_FEATURE_TESS_UNSUPPORTED;
    if (dm_nvn_spirv_has_capability(data, bytes, 56u) && !storage_write_supported)
        return DM_NVN_SHADER_FEATURE_STORAGE_WRITE_UNSUPPORTED;
    if (dm_nvn_spirv_has_capability(data, bytes, 57u) && !multiview_supported)
        return DM_NVN_SHADER_FEATURE_MULTIVIEW_UNSUPPORTED;
    if (dm_nvn_spirv_uses_shader_draw_parameters(data, bytes) && !draw_parameters_supported)
        return DM_NVN_SHADER_FEATURE_NEED_DRAW_PARAMETERS;
    return DM_NVN_SHADER_FEATURE_OK;
}

// Optional warm-up paths may skip an unsupported optional shader; required
// draw/dispatch must still take the fatal rejection path downstream.
static inline int dm_nvn_shader_feature_can_skip(dm_nvn_shader_feature_status status, int fatal) {
    return status != DM_NVN_SHADER_FEATURE_OK && !fatal;
}
