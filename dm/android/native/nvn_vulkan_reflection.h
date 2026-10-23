#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline int dm_nvn_shader_entry_valid(const uint32_t *code, size_t bytes, uint32_t stage) {
    if (!code || bytes < 20 || bytes % 4 || code[0] != 0x07230203 ||
        code[1] < 0x00010000 || code[1] > 0x00010300 || !code[3] || code[4]) return 0;
    uint32_t model = stage == 0 ? 0 : stage == 1 ? 4 : stage == 5 ? 5 : UINT32_MAX;
    if (model == UINT32_MAX) return 0;
    unsigned entries = 0;
    for (size_t i = 5; i < bytes / 4;) {
        uint32_t length = code[i] >> 16, opcode = code[i] & 65535;
        if (!length || length > bytes / 4 - i) return 0;
        if (opcode == 15) {
            if (length < 5 || code[i + 1] != model || !code[i + 2] || code[i + 2] >= code[3] ||
                code[i + 3] != 0x6e69616d || code[i + 4] != 0 || ++entries != 1) return 0;
        }
        i += length;
    }
    return entries == 1;
}

static inline int dm_nvn_shader_reflection_valid(const uint32_t *meta, size_t bytes) {
    if (!meta || bytes < 28 || memcmp(meta, "DMVK", 4) || meta[1] != 1 ||
        meta[4] > 256 || meta[5] > 65536 || meta[6] != 32 ||
        bytes != 28 + meta[4] * 40 + meta[5] + 128) return 0;
    if (meta[2] != 0 && meta[2] != 1 && meta[2] != 5) return 0;
    if (meta[3] != (meta[2] == 1 ? 1u : 0u)) return 0;
    for (unsigned i = 0; i < meta[4]; i++) {
        const uint32_t *binding = meta + 7 + i * 10;
        if (binding[0] > 4 || !binding[7] || binding[7] > 128) return 0;
        for (unsigned j = 0; j < i; j++)
            if (binding[1] == meta[7 + j * 10 + 1]) return 0;
    }
    return 1;
}
