// #157 Android 独立「自定义」偏好事務模組：凍結上游 dm/host/dm_custom.c/h 的真實鍵、預設值與 Apply/Discard/Defaults 語義，
// 但以有界解析 + 注入持久化回調實現，不依賴遊戲、不讀寫遊戲存檔、不提供 UI/JNI/App 整合。
// #157 Android standalone Custom-preferences transaction module: freezes the real keys, defaults and Apply/Discard/
// Defaults semantics of the upstream dm/host/dm_custom.c/h, but implements them with a bounded parser and an injected
// persistence callback. No game dependency, no game save access, no UI/JNI/App integration.
//
// Upstream facts frozen here (dm/host/dm_custom.c):
//   keys, in Custom-page row order: lang_en, auto_mana, merc_pot, drop_light, map_mark, item_notes, icloud,
//     pot_pick, drop_haptic, gem_pick, store_btn, eco_fps, q3d, loot_filter, auto_buff
//   defaults: { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1 }
//     (lang_en 0, eco_fps 0 = 60 fps power saving off, q3d 0 = smoothness first, everything else on)
//   file: one 'key=0/1' per line; absent key = its default; unknown lines are skipped (older drop_filter / ver);
//     lang_en is written only after the player actually changed it; merc_pot_v2 is the explicit-value line for
//     merc_pot under the new default (authoritative when present); Defaults resets pending values of rows 1..N-1
//     and deliberately leaves the language row alone.
//
// Android-specific choices of this slice (not upstream behaviour):
//   - strict values: only "0" or "1" is accepted; anything else is counted and skipped instead of being coerced to 1
//     the way the upstream 'eq[1] != '0'' test would;
//   - the file is refused as a whole when it exceeds the bounded size or declares an unsupported version;
//   - applying is transactional: the persisted bytes are produced from the pending values, the store callback must
//     accept them, and only then do pending values become effective. A rejected store leaves effective and pending
//     values untouched and retryable.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DM_CP_KEY_MAX 15
#define DM_CP_MAX_BYTES 2048u   // 整份文件上限 | whole-file bound
#define DM_CP_MAX_LINE 64u      // 单行上限 | per-line bound
#define DM_CP_VERSION 1

typedef enum {
    DM_CP_OK = 0,
    DM_CP_SAVE_FAILED,          // 持久化回調拒絕；生效/待定值都保留 | the store rejected; effective and pending are kept
    DM_CP_SAVE_TOO_LARGE,       // 待寫內容超出呼叫方/模組上限，未調用回調 | content exceeded the bound, callback not called
    DM_CP_LOAD_IO,              // 讀取回調失敗 | the load callback failed
    DM_CP_LOAD_TOO_LARGE,       // 文件超出上限，整份拒絕 | file exceeded the bound and was refused as a whole
    DM_CP_LOAD_VERSION,         // 不支持的版本，整份拒絕 | unsupported version, refused as a whole
    DM_CP_UNKNOWN_KEY,          // 調用方給了不存在的鍵 | caller used a key that does not exist
    DM_CP_INVALID               // 參數非法 | invalid argument
} dm_cp_status;

typedef struct {
    const char *key;
    int def;                    // 上游缺省值 | upstream default
    int defaultable;            // 「默認」是否管這一項（lang_en 為 0） | whether Defaults covers this row
} dm_cp_key;

typedef int (*dm_cp_store_fn)(void *ctx, const char *data, size_t len);            // 0 = 已保存 | 0 = persisted
typedef int (*dm_cp_load_fn)(void *ctx, char *out, size_t capacity, size_t *len);  // 0 = 讀取成功 | 0 = read ok

typedef struct {
    int effective[DM_CP_KEY_MAX];
    int pending[DM_CP_KEY_MAX];
    unsigned char has_pending[DM_CP_KEY_MAX];

    int lang_explicit;          // 文件中出現過 lang_en，或玩家在本實例中改過 | lang_en present or changed here
    int merc_explicit;          // merc_pot_v2 存在，或玩家改過 merc_pot | merc_pot_v2 present or changed here
    int loaded;                 // 至少成功加載過一次 | at least one successful load

    uint32_t loads, applies, apply_skips, discards, default_resets;
    uint32_t save_failures, store_calls;
    uint32_t lines_skipped, invalid_values, unknown_keys, duplicates, long_lines;
} dm_cp_instance;

// 凍結的鍵表（只讀）。| Frozen key table (read-only).
const dm_cp_key *dm_cp_keys(void);
int dm_cp_key_count(void);
int dm_cp_key_index(const char *key);

// 實例初始化為「全部缺省、無待定」，不共享任何全局狀態。
// Initialises an instance to all-defaults with no pending values; no global state is shared.
void dm_cp_init(dm_cp_instance *instance);

// 已生效值；未知鍵返回缺省 1（與上游一致）。| Effective value; an unknown key returns the upstream default 1.
int dm_cp_get(const dm_cp_instance *instance, const char *key);
// 待定值：-1 表示沒有待定 | pending value: -1 when nothing is pending.
int dm_cp_pending(const dm_cp_instance *instance, const char *key);
// 只改待定值，不影響生效值，不觸碰持久化。| Changes only the pending value: no effect, no persistence.
dm_cp_status dm_cp_set(dm_cp_instance *instance, const char *key, int value);
int dm_cp_dirty(const dm_cp_instance *instance);

// 「應用」：只有待定值與生效值真的不同時才序列化並交回調保存，成功後才提交；空/等價事務直接成功、不調用存儲、
// 不產生顯式語言/傭兵選擇，但清空待定（與上游 AcceptChanges 一致）。保存被拒時生效值與待定值都不變，可重試。
// Apply: serialise and store only when a pending value really differs from the effective one, then commit on success.
// An empty/equal transaction succeeds without touching storage and without inventing explicit language/mercenary
// choices, while clearing pending values as upstream does. A rejected store keeps effective and pending values.
dm_cp_status dm_cp_apply(dm_cp_instance *instance, dm_cp_store_fn store, void *ctx);
// 「放棄」：丟掉待定值（生效值與持久化不變）。| Discard: drop pending values (effective and storage unchanged).
void dm_cp_discard(dm_cp_instance *instance);
// 「默認」：待定值回到上游缺省；語言行按上游語義不參與。只改待定值。
// Default: pending values go back to the upstream defaults; the language row is excluded, as upstream does. Pending
// only.
void dm_cp_defaults(dm_cp_instance *instance);

// 加載：先完整解析再一次性替換生效值；失敗時實例保持原狀。
// Load: parse the whole input before replacing anything; on failure the instance keeps its previous state.
dm_cp_status dm_cp_load(dm_cp_instance *instance, dm_cp_load_fn load, void *ctx);
// 直接解析一段字節（測試/調試用），語義與 dm_cp_load 相同。
// Parse a byte range directly (tests/diagnostics); same semantics as dm_cp_load.
dm_cp_status dm_cp_parse(dm_cp_instance *instance, const char *data, size_t len);

// 序列化當前生效值（不含待定）或合併視圖。容量不足返回 SAVE_TOO_LARGE 且不寫部分內容。
// Serialise effective values (or the merged pending view). Insufficient capacity returns SAVE_TOO_LARGE without
// writing a partial document.
dm_cp_status dm_cp_serialize(const dm_cp_instance *instance, int include_pending, char *out, size_t capacity,
                             size_t *out_len);

#ifdef __cplusplus
}
#endif
