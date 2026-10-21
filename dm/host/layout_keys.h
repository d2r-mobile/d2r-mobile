// 实际 loader layout 行解析与键槽校验；测试直接调用这同一段代码。
// Parser/validation used by the real loader for KEY_*_IDX lines; tests call this same code.
#pragma once
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { DM_LAYOUT_NOT_KEY = 0, DM_LAYOUT_KEY_OK = 1, DM_LAYOUT_KEY_MALFORMED = -1 };

static inline int dm_layout_key_slot(const char *name) {
    static const char *const names[8] = {"K18", "KTLS", "KTLR", "KGATE", "KRES0", "KRES1", "KASPECT", "KASPECTUI"};
    for (int i = 0; i < 8; ++i) {
        size_t n = strlen(names[i]);
        if (!strncmp(name, "KEY_", 4) && !strncmp(name + 4, names[i], n) &&
            !strcmp(name + 4 + n, "_IDX")) return i;
    }
    return -1;
}

// 解析 "# KEY_K18_IDX\t5\n"；必须使用 tab，数字非负且不溢出，行尾只能是换行/结束。
// Parse "# KEY_K18_IDX\t5\n"; tab is mandatory, the number is non-negative and must not overflow,
// and only a newline/end may follow it. Returns NOT_KEY for unrelated header lines.
static inline int dm_layout_parse_key_line(const char *line, unsigned *slot, unsigned *index) {
    if (!line || strncmp(line, "# ", 2)) return DM_LAYOUT_NOT_KEY;
    const char *key = line + 2;
    const char *end = key;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n') ++end;
    if (end == key || (size_t)(end - key) >= 32) return DM_LAYOUT_NOT_KEY;
    char name[32];
    memcpy(name, key, (size_t)(end - key));
    name[end - key] = 0;
    int found = dm_layout_key_slot(name);
    if (found < 0) return DM_LAYOUT_NOT_KEY;
    const char *p = end;
    if (*p != '\t') return DM_LAYOUT_KEY_MALFORMED;
    ++p;
    if (!isdigit((unsigned char)*p)) return DM_LAYOUT_KEY_MALFORMED;
    unsigned long long value = 0;
    for (; *p && *p != '\n'; ++p) {
        if (!isdigit((unsigned char)*p)) return DM_LAYOUT_KEY_MALFORMED;
        value = value * 10 + (unsigned)(*p - '0');
        if (value > UINT_MAX) return DM_LAYOUT_KEY_MALFORMED;
    }
    if (*p == '\n' && p[1]) return DM_LAYOUT_KEY_MALFORMED;   // trailing bytes after newline
    *slot = (unsigned)found;
    *index = (unsigned)value;
    return DM_LAYOUT_KEY_OK;
}

// 校验八个槽：全部不同、都在键页内，且不占用 Android 的 context 入口槽。
// Validate all eight slots: unique, inside the key page, and not the Android context-entry slot.
static inline int dm_layout_slots_valid(const unsigned *slots, unsigned count, unsigned page_slots,
                                        int reserved, char *error, size_t cap) {
    for (unsigned i = 0; i < count; ++i) {
        if (slots[i] >= page_slots) {
            if (error && cap) snprintf(error, cap, "slot %u outside key page (%u slots)", slots[i], page_slots);
            return -1;
        }
        if (reserved >= 0 && slots[i] == (unsigned)reserved) {
            if (error && cap) snprintf(error, cap, "slot %u reserved for context entry", slots[i]);
            return -1;
        }
        for (unsigned j = 0; j < i; ++j) if (slots[i] == slots[j]) {
            if (error && cap) snprintf(error, cap, "duplicate slot %u", slots[i]);
            return -1;
        }
    }
    return 0;
}
