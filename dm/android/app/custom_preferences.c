// #157 實現：有界解析 + 事務式保存。解析失敗不改實例；保存失敗不提交生效值。
// #157 implementation: bounded parsing plus transactional saves. A failed parse changes nothing; a failed save
// commits nothing.
#include "custom_preferences.h"

#include <stdio.h>
#include <string.h>

// 凍結自 dm/host/dm_custom.c 的 k_keys / k_def（順序 = 自定義頁行序）。
// Frozen from k_keys / k_def in dm/host/dm_custom.c (order = Custom page row order).
static const dm_cp_key k_keys[DM_CP_KEY_MAX] = {
    {"lang_en", 0, 0},
    {"auto_mana", 1, 1},
    {"merc_pot", 1, 1},
    {"drop_light", 1, 1},
    {"map_mark", 1, 1},
    {"item_notes", 1, 1},
    {"icloud", 1, 1},
    {"pot_pick", 1, 1},
    {"drop_haptic", 1, 1},
    {"gem_pick", 1, 1},
    {"store_btn", 1, 1},
    {"eco_fps", 0, 1},
    {"q3d", 0, 1},
    {"loot_filter", 1, 1},
    {"auto_buff", 1, 1},
};

const dm_cp_key *dm_cp_keys(void) { return k_keys; }
int dm_cp_key_count(void) { return DM_CP_KEY_MAX; }

int dm_cp_key_index(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < DM_CP_KEY_MAX; i++)
        if (!strcmp(key, k_keys[i].key)) return i;
    return -1;
}

void dm_cp_init(dm_cp_instance *instance) {
    if (!instance) return;
    memset(instance, 0, sizeof *instance);
    for (int i = 0; i < DM_CP_KEY_MAX; i++) instance->effective[i] = k_keys[i].def;
}

int dm_cp_get(const dm_cp_instance *instance, const char *key) {
    const int i = dm_cp_key_index(key);
    if (i < 0) return 1;                        // 未知鍵與上游一致返回 1 | unknown keys return 1, as upstream
    if (!instance) return k_keys[i].def;
    return instance->effective[i];
}

int dm_cp_pending(const dm_cp_instance *instance, const char *key) {
    const int i = dm_cp_key_index(key);
    if (i < 0 || !instance || !instance->has_pending[i]) return -1;
    return instance->pending[i];
}

int dm_cp_dirty(const dm_cp_instance *instance) {
    if (!instance) return 0;
    for (int i = 0; i < DM_CP_KEY_MAX; i++)
        if (instance->has_pending[i]) return 1;
    return 0;
}

dm_cp_status dm_cp_set(dm_cp_instance *instance, const char *key, int value) {
    if (!instance) return DM_CP_INVALID;
    const int i = dm_cp_key_index(key);
    if (i < 0) return DM_CP_UNKNOWN_KEY;
    if (value != 0 && value != 1) return DM_CP_INVALID;
    instance->pending[i] = value ? 1 : 0;
    instance->has_pending[i] = 1;
    return DM_CP_OK;
}

void dm_cp_discard(dm_cp_instance *instance) {
    if (!instance) return;
    memset(instance->has_pending, 0, sizeof instance->has_pending);
    instance->discards++;
}

void dm_cp_defaults(dm_cp_instance *instance) {
    if (!instance) return;
    // 上游從第 1 行起：語言那一格不歸「默認」管。| Upstream starts at row 1: the language row is not covered.
    for (int i = 1; i < DM_CP_KEY_MAX; i++) {
        if (!k_keys[i].defaultable) continue;
        instance->pending[i] = k_keys[i].def;
        instance->has_pending[i] = 1;
    }
    instance->default_resets++;
}

static int merged_value(const dm_cp_instance *instance, int include_pending, int i) {
    if (include_pending && instance->has_pending[i]) return instance->pending[i];
    return instance->effective[i];
}

static dm_cp_status serialize_into(const dm_cp_instance *instance, int include_pending, char *out, size_t capacity,
                                   size_t *out_len) {
    size_t used = 0;
    int written = snprintf(out, capacity, "version=%d\n", DM_CP_VERSION);
    if (written < 0) return DM_CP_INVALID;
    used = (size_t)written;
    if (used >= capacity) return DM_CP_SAVE_TOO_LARGE;

    for (int i = 0; i < DM_CP_KEY_MAX; i++) {
        const int lang = i == dm_cp_key_index("lang_en");
        const int merc = i == dm_cp_key_index("merc_pot");
        // lang_en 只在寫過/改過之後才寫；否則上游語義是"不動原版語言"。
        // lang_en is written only once written/changed before; otherwise upstream leaves the original language alone.
        if (lang && !instance->lang_explicit && !(include_pending && instance->has_pending[i])) continue;
        written = snprintf(out + used, capacity - used, "%s=%d\n", k_keys[i].key, merged_value(instance, include_pending, i));
        if (written < 0) return DM_CP_INVALID;
        used += (size_t)written;
        if (used >= capacity) return DM_CP_SAVE_TOO_LARGE;
        if (merc && (instance->merc_explicit || (include_pending && instance->has_pending[i]))) {
            written = snprintf(out + used, capacity - used, "merc_pot_v2=%d\n", merged_value(instance, include_pending, i));
            if (written < 0) return DM_CP_INVALID;
            used += (size_t)written;
            if (used >= capacity) return DM_CP_SAVE_TOO_LARGE;
        }
    }
    if (out_len) *out_len = used;
    return DM_CP_OK;
}

dm_cp_status dm_cp_serialize(const dm_cp_instance *instance, int include_pending, char *out, size_t capacity,
                             size_t *out_len) {
    if (!instance || !out || capacity == 0) return DM_CP_INVALID;
    // 先寫進模組自有的暫存區，容量不足時不再動調用方的緩衝區，避免留下半份文件。
    // Render into the module's own scratch buffer first, so an insufficient capacity never leaves a partial document
    // in the caller's buffer.
    char scratch[DM_CP_MAX_BYTES];
    size_t length = 0;
    const dm_cp_status status = serialize_into(instance, include_pending, scratch, sizeof scratch, &length);
    if (status != DM_CP_OK) return status;
    if (length + 1 > capacity) return DM_CP_SAVE_TOO_LARGE;
    memcpy(out, scratch, length);
    out[length] = '\0';
    if (out_len) *out_len = length;
    return DM_CP_OK;
}

dm_cp_status dm_cp_apply(dm_cp_instance *instance, dm_cp_store_fn store, void *ctx) {
    if (!instance || !store) return DM_CP_INVALID;

    // 與上游 AcceptChanges 一致：只有待定值真的不同於生效值才算改動。
    // As upstream AcceptChanges: only a pending value that really differs from the effective value is a change.
    int changed[DM_CP_KEY_MAX];
    int any_change = 0;
    int lang_index = dm_cp_key_index("lang_en");
    int merc_index = dm_cp_key_index("merc_pot");
    for (int i = 0; i < DM_CP_KEY_MAX; i++) {
        changed[i] = instance->has_pending[i] && instance->pending[i] != instance->effective[i];
        any_change |= changed[i];
    }
    if (!any_change) {
        // 空/等價事務：不調用存儲、不產生顯式語言/傭兵選擇，但清空待定（上游同樣清空）。
        // Empty/equal transaction: no storage call and no explicit language/mercenary choice, but pending values are
        // cleared exactly as upstream does.
        memset(instance->has_pending, 0, sizeof instance->has_pending);
        instance->apply_skips++;
        return DM_CP_OK;
    }

    // 用"提交後的視圖"序列化：只有真正改動的鍵才更新數值與顯式標誌。
    // Serialise a post-commit view: only genuinely changed keys update values and explicit markers.
    dm_cp_instance view = *instance;
    for (int i = 0; i < DM_CP_KEY_MAX; i++) {
        if (changed[i]) view.effective[i] = instance->pending[i];
        view.has_pending[i] = 0;
    }
    if (changed[lang_index]) view.lang_explicit = 1;
    if (changed[merc_index]) view.merc_explicit = 1;

    char document[DM_CP_MAX_BYTES];
    size_t length = 0;
    const dm_cp_status serialised = serialize_into(&view, 0, document, sizeof document, &length);
    if (serialised != DM_CP_OK) return serialised;

    instance->store_calls++;
    if (store(ctx, document, length) != 0) {
        // 保存被拒：不提交、不清待定，調用方可直接重試。| Store rejected: no commit, pending kept for a retry.
        instance->save_failures++;
        return DM_CP_SAVE_FAILED;
    }

    for (int i = 0; i < DM_CP_KEY_MAX; i++) {
        if (changed[i]) instance->effective[i] = instance->pending[i];
        instance->has_pending[i] = 0;   // 上游：應用後待定全部清空，包括與生效值相同的項
    }
    if (changed[lang_index]) instance->lang_explicit = 1;
    if (changed[merc_index]) instance->merc_explicit = 1;
    instance->applies++;
    return DM_CP_OK;
}

static int value_is_bool(const char *value, size_t len) {
    // 嚴格：只接受單字節 '0' 或 '1'（上游 eq[1] != '0' 會把任何非 0 當 1）。
    // Strict: only a single '0' or '1'; upstream's 'eq[1] != 0' would coerce anything else to 1.
    return len == 1 && (value[0] == '0' || value[0] == '1');
}

dm_cp_status dm_cp_parse(dm_cp_instance *instance, const char *data, size_t len) {
    if (!instance || (!data && len)) return DM_CP_INVALID;
    if (len > DM_CP_MAX_BYTES) return DM_CP_LOAD_TOO_LARGE;

    dm_cp_instance next = *instance;            // 解析進暫存副本，成功才替換 | parse into a copy, replace only on success
    memset(next.has_pending, 0, sizeof next.has_pending);
    for (int i = 0; i < DM_CP_KEY_MAX; i++) next.effective[i] = k_keys[i].def;
    next.lang_explicit = 0;
    next.merc_explicit = 0;
    next.loaded = 0;
    int merc_v2 = -1;
    int merc_v2_seen = 0;

    size_t pos = 0;
    while (pos < len) {
        size_t end = pos;
        while (end < len && data[end] != '\n') end++;
        size_t line_len = end - pos;
        if (line_len && data[pos + line_len - 1] == '\r') line_len--;
        const char *line = data + pos;
        pos = end + 1;

        if (!line_len) continue;                 // 空行忽略 | blank lines are ignored
        if (line_len > DM_CP_MAX_LINE) { next.long_lines++; continue; }

        const char *eq = memchr(line, '=', line_len);
        if (!eq) { next.lines_skipped++; continue; }
        const size_t key_len = (size_t)(eq - line);
        const char *value = eq + 1;
        const size_t value_len = line_len - key_len - 1;

        if (key_len == 7 && !memcmp(line, "version", 7)) {
            // 版本必須是受支持的 1；其他值整份拒絕（不改實例）。| Only version 1 is supported; anything else refuses the file.
            if (!(value_len == 1 && value[0] == '1')) return DM_CP_LOAD_VERSION;
            continue;
        }
        if (key_len == 11 && !memcmp(line, "merc_pot_v2", 11)) {
            if (!value_is_bool(value, value_len)) { next.invalid_values++; continue; }
            // 多個 v2 時最後一個有效值勝出；非法值不影響已記下的有效值。
            // With several v2 lines the last valid one wins; an invalid line never discards an earlier valid value.
            if (merc_v2_seen) next.duplicates++;
            merc_v2 = value[0] - '0';
            merc_v2_seen = 1;
            continue;
        }

        int index = -1;
        for (int i = 0; i < DM_CP_KEY_MAX; i++) {
            const size_t n = strlen(k_keys[i].key);
            if (n == key_len && !memcmp(line, k_keys[i].key, n)) { index = i; break; }
        }
        if (index < 0) { next.unknown_keys++; continue; }   // 舊版/未知鍵跳過 | legacy/unknown keys are skipped
        if (!value_is_bool(value, value_len)) { next.invalid_values++; continue; }
        if (next.has_pending[index]) next.duplicates++;      // 重複鍵：後者生效，計數 | duplicate: last wins, counted
        next.effective[index] = value[0] - '0';
        next.has_pending[index] = 1;                          // 僅用作"此鍵已出現"的標記 | used only as a "seen" marker
        if (index == dm_cp_key_index("lang_en")) next.lang_explicit = 1;
    }

    memset(next.has_pending, 0, sizeof next.has_pending);
    // 傭兵遷移在整份文檔解析後才解析，與文件行序無關：有效 v2 勝過舊 merc_pot；沒有有效 v2 就用源缺省值（上游
    // load_once 正是先收集 merc_pot_v2，再用 merc<0 ? k_def : merc 覆蓋，舊行因此從不生效）。
    // Mercenary migration is resolved after the whole document, independent of line order: a valid v2 beats the legacy
    // merc_pot line, and without a valid v2 the source default applies (upstream load_once collects merc_pot_v2 and
    // then overwrites with merc < 0 ? k_def : merc, so the legacy line never wins).
    next.effective[dm_cp_key_index("merc_pot")] = merc_v2 >= 0 ? merc_v2 : k_keys[dm_cp_key_index("merc_pot")].def;
    next.merc_explicit = merc_v2_seen;
    next.loaded = 1;
    next.loads = instance->loads + 1;
    *instance = next;
    return DM_CP_OK;
}

dm_cp_status dm_cp_load(dm_cp_instance *instance, dm_cp_load_fn load, void *ctx) {
    if (!instance || !load) return DM_CP_INVALID;
    char document[DM_CP_MAX_BYTES + 1];
    size_t length = 0;
    if (load(ctx, document, sizeof document, &length) != 0) return DM_CP_LOAD_IO;
    if (length > DM_CP_MAX_BYTES) return DM_CP_LOAD_TOO_LARGE;
    return dm_cp_parse(instance, document, length);
}
