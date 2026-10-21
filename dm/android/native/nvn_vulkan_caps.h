// SPIR-V capability query for the Android Vulkan backend.
// Kept separate from Vulkan types so the owned host test can exercise the
// capability decision without a Vulkan device.
#pragma once
#include <stddef.h>
#include <stdint.h>

// SPIR-V OpCapability = 17, SpvCapabilityShaderDrawParameters = 4427.
static inline int dm_nvn_spirv_uses_shader_draw_parameters(const void *data, size_t bytes) {
    if (!data || bytes < 5 * sizeof(uint32_t)) return 0;
    const uint32_t *words = (const uint32_t *)data;
    size_t count = bytes / sizeof(uint32_t);
    if (words[0] != 0x07230203u) return 0;
    for (size_t i = 5; i < count;) {
        uint32_t instruction = words[i];
        uint32_t opcode = instruction & 0xffffu;
        uint32_t length = instruction >> 16;
        if (length == 0 || length > count - i) break;
        if (opcode == 17u && length >= 2 && words[i + 1] == 4427u) return 1;
        i += length;
    }
    return 0;
}
