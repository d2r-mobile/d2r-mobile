#pragma once
#include <stdint.h>
#include <string.h>

static inline int dm_android_custom_visible(const char *key) {
    return strcmp(key, "icloud") != 0;
}

/* Imported overlays also contain this iOS row, so filter the live page. */
static inline int dm_android_custom_hide_cloud(uint64_t rows, void (*layout)(uint64_t)) {
    if (!rows) return 0;
    uint64_t *children = *(uint64_t **)(uintptr_t)(rows + 0x58);
    uint64_t count = *(uint64_t *)(uintptr_t)(rows + 0x60), label = 0, box = 0;
    if (!children || count > 256) return 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t w = children[i];
        const char *name = w ? *(const char **)(uintptr_t)(w + 8) : NULL;
        if (name && !strcmp(name, "TouchCustom_icloud_Text")) label = w;
        if (name && !strcmp(name, "TouchCustom_icloud")) box = w;
    }
    if (!label || !box) return 0;
    int32_t *rect = (int32_t *)(uintptr_t)(label + 0x70);
    int32_t y = rect[1], height = rect[3];
    if (!height && !*(uint8_t *)(uintptr_t)(box + 0x50)) return 1;
    int32_t *extent = (int32_t *)(uintptr_t)(rows + 0x70);
    if (height <= 0 || extent[3] < height) return 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t w = children[i];
        if (!w) continue;
        int32_t *r = (int32_t *)(uintptr_t)(w + 0x70);
        if (w == label || w == box) {
            *(uint8_t *)(uintptr_t)(w + 0x50) = 0;
            *(uint8_t *)(uintptr_t)(w + 0x51) = 0;
            r[1] = y; r[3] = 0;
            layout(w);
        } else if (r[1] > y) {
            r[1] -= height;
            layout(w);
        }
    }
    extent[3] -= height;
    layout(rows);
    return 1;
}
