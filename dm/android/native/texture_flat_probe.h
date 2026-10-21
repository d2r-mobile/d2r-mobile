#pragma once

#include <errno.h>
#include <stdlib.h>
#include <string.h>

// 选择器格式固定为 <16 位小写十六进制 shader>:<十进制 stage>:<十进制 slot>。
// The selector is exactly <16 lowercase hex shader>:<decimal stage>:<decimal slot>.
#define TEXTURE_FLAT_PROPERTY_MAX 92u
#define TEXTURE_FLAT_SHADER_MAX 17u
_Static_assert(TEXTURE_FLAT_PROPERTY_MAX >= 92u, "Android property value capacity");

typedef struct {
    char shader[TEXTURE_FLAT_SHADER_MAX];
    unsigned stage, slot;
    int valid;
} texture_flat_selector;

static inline int texture_flat_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static inline int texture_flat_uint(const char *value, const char **end, unsigned *out) {
    if (!value || value[0] < '0' || value[0] > '9') return 0;
    char *stop = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &stop, 10);
    if (errno == ERANGE || !stop || stop == value || parsed > 0xfffffffful) return 0;
    *out = (unsigned)parsed;
    *end = stop;
    return 1;
}

// 解析失败一律 off；不把任意以 1 开头的值当作启用。
// Parse failure is always off; never treat an arbitrary value starting with 1 as enabled.
static inline int texture_flat_parse(const char *value, texture_flat_selector *out) {
    texture_flat_selector parsed = {0};
    if (!value || strlen(value) < 16u + 1u + 1u + 1u + 1u) return 0;
    for (unsigned i = 0; i < 16; i++) if (!texture_flat_hex(value[i])) return 0;
    if (value[16] != ':') return 0;
    const char *p = value + 17;
    if (!texture_flat_uint(p, &p, &parsed.stage) || *p != ':') return 0;
    p++;
    if (!texture_flat_uint(p, &p, &parsed.slot) || *p != 0) return 0;
    memcpy(parsed.shader, value, 16);
    parsed.shader[16] = 0;
    parsed.valid = 1;
    *out = parsed;
    return 1;
}

// 只允许明确选择的 2D 颜色采样；拒绝数组、深度/比较、整数和 storage。
// Only the explicitly selected 2D color sample is eligible; arrays, depth/comparison, integer and storage are rejected.
static inline int texture_flat_format_color(unsigned format) {
    return format == 0x25 || format == 0x29 || format == 0x44 || format == 0x48;
}

static inline int texture_flat_matches(const texture_flat_selector *selector, const char *shader, unsigned stage,
                                       unsigned slot, unsigned type, int storage, unsigned format) {
    if (!selector || !selector->valid || !shader || storage) return 0;
    if ((type & 255u) != 3u || (type & 256u) || (type & 1024u)) return 0;
    if (!texture_flat_format_color(format)) return 0;
    return !strcmp(selector->shader, shader) && selector->stage == stage && selector->slot == slot;
}

typedef struct {
    texture_flat_selector selector;
    int enabled;
} texture_flat_state;

static inline void texture_flat_state_apply(texture_flat_state *state, const char *value) {
    texture_flat_selector parsed = {0};
    int enabled = texture_flat_parse(value, &parsed);
    state->enabled = enabled;
    state->selector = parsed;
}

static inline int texture_flat_state_matches(const texture_flat_state *state, const char *shader, unsigned stage,
                                             unsigned slot, unsigned type, int storage, unsigned format) {
    return state->enabled && texture_flat_matches(&state->selector, shader, stage, slot, type, storage, format);
}
