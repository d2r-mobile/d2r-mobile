#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
