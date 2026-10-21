// #157 宿主測試：驅動真實的偏好模組，覆蓋往返、應用、放棄、默認、失敗保存、畸形輸入與實例隔離。
// #157 host tests: drive the real preferences module across roundtrip, apply, discard, defaults, a failing save,
// malformed input and instance isolation.
#include "../app/custom_preferences.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    char document[DM_CP_MAX_BYTES + 1];
    size_t length;
    int calls;
    int fail;
} fake_store;

static int store_fn(void *ctx, const char *data, size_t len) {
    fake_store *s = ctx;
    s->calls++;
    if (s->fail) return 1;
    if (len > sizeof s->document) return 1;
    memcpy(s->document, data, len);
    s->document[len] = '\0';
    s->length = len;
    return 0;
}

static int load_fn(void *ctx, char *out, size_t capacity, size_t *len) {
    const char *text = ctx;
    const size_t n = strlen(text);
    if (n + 1 > capacity) return 1;
    memcpy(out, text, n + 1);
    *len = n;
    return 0;
}

static int contains(const char *haystack, const char *needle) { return strstr(haystack, needle) != NULL; }

static void test_frozen_key_table(void) {
    static const char *const expected[DM_CP_KEY_MAX] = {
        "lang_en", "auto_mana", "merc_pot", "drop_light", "map_mark", "item_notes", "icloud", "pot_pick",
        "drop_haptic", "gem_pick", "store_btn", "eco_fps", "q3d", "loot_filter", "auto_buff"};
    static const int defaults[DM_CP_KEY_MAX] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1};
    CHECK(dm_cp_key_count() == DM_CP_KEY_MAX);
    for (int i = 0; i < DM_CP_KEY_MAX; i++) {
        const dm_cp_key *key = &dm_cp_keys()[i];
        CHECK(key->key && !strcmp(key->key, expected[i]));
        CHECK(key->def == defaults[i]);
        CHECK(key->defaultable == (i == 0 ? 0 : 1));
    }
    CHECK(dm_cp_key_index("eco_fps") == 11);
    CHECK(dm_cp_key_index("q3d") == 12);
    CHECK(dm_cp_key_index("drop_filter") == -1);
    CHECK(dm_cp_key_index("nope") == -1);
}

static void test_init_and_pending(void) {
    dm_cp_instance p;
    dm_cp_init(&p);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);
    CHECK(dm_cp_get(&p, "q3d") == 0);
    CHECK(dm_cp_get(&p, "lang_en") == 0);
    CHECK(dm_cp_get(&p, "auto_mana") == 1);
    CHECK(dm_cp_get(&p, "unknown_key") == 1);
    CHECK(dm_cp_dirty(&p) == 0);
    CHECK(dm_cp_pending(&p, "eco_fps") == -1);

    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_pending(&p, "eco_fps") == 1);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);          // 待定不等於生效 | pending is not effective
    CHECK(dm_cp_dirty(&p) == 1);
    CHECK(dm_cp_set(&p, "eco_fps", 2) == DM_CP_INVALID);
    CHECK(dm_cp_set(&p, "nope", 1) == DM_CP_UNKNOWN_KEY);

    dm_cp_discard(&p);
    CHECK(dm_cp_pending(&p, "eco_fps") == -1);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);
    CHECK(dm_cp_dirty(&p) == 0);
}

static void test_apply_commit_and_failure(void) {
    dm_cp_instance p;
    fake_store store;
    dm_cp_init(&p);
    memset(&store, 0, sizeof store);
    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_set(&p, "q3d", 1) == DM_CP_OK);

    // 保存被拒：生效值不變、待定保留、計數如實。| A rejected save: no commit, pending kept, failure counted.
    store.fail = 1;
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_SAVE_FAILED);
    CHECK(p.save_failures == 1);
    CHECK(p.store_calls == 1);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);
    CHECK(dm_cp_get(&p, "q3d") == 0);
    CHECK(dm_cp_pending(&p, "eco_fps") == 1);
    CHECK(dm_cp_dirty(&p) == 1);

    // 重試成功後才提交。| Only a successful retry commits.
    store.fail = 0;
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "eco_fps") == 1);
    CHECK(dm_cp_get(&p, "q3d") == 1);
    CHECK(dm_cp_dirty(&p) == 0);
    CHECK(p.applies == 1);
    CHECK(store.calls == 2);
    CHECK(contains(store.document, "version=1\n"));
    CHECK(contains(store.document, "eco_fps=1\n"));
    CHECK(contains(store.document, "q3d=1\n"));
    CHECK(!contains(store.document, "lang_en="));   // 語言沒寫過就不寫 | lang_en unwritten when never changed
}

static void test_defaults_and_language_row(void) {
    dm_cp_instance p;
    fake_store store;
    dm_cp_init(&p);
    memset(&store, 0, sizeof store);
    CHECK(dm_cp_set(&p, "lang_en", 1) == DM_CP_OK);
    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "lang_en") == 1);

    // 「默認」只改待定，且不含語言行（上游從第 1 行起）。| Defaults only changes pending and skips the language row.
    dm_cp_defaults(&p);
    CHECK(dm_cp_pending(&p, "lang_en") == -1);
    CHECK(dm_cp_pending(&p, "eco_fps") == 0);
    CHECK(dm_cp_pending(&p, "q3d") == 0);
    CHECK(dm_cp_pending(&p, "auto_mana") == 1);
    CHECK(dm_cp_get(&p, "eco_fps") == 1);            // 還沒應用 | not applied yet
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);
    CHECK(dm_cp_get(&p, "lang_en") == 1);            // 默認沒動語言 | Defaults left the language alone
    CHECK(contains(store.document, "lang_en=1\n"));  // 已顯式寫過，仍保留 | still written once it is explicit
}

static void test_roundtrip_and_isolation(void) {
    dm_cp_instance a, b;
    fake_store store;
    dm_cp_init(&a);
    dm_cp_init(&b);
    memset(&store, 0, sizeof store);
    CHECK(dm_cp_set(&a, "auto_mana", 0) == DM_CP_OK);
    CHECK(dm_cp_set(&a, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_set(&a, "lang_en", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&a, store_fn, &store) == DM_CP_OK);

    // 兩個實例互不污染。| Two instances do not contaminate each other.
    CHECK(dm_cp_get(&b, "auto_mana") == 1);
    CHECK(dm_cp_get(&b, "eco_fps") == 0);
    CHECK(dm_cp_get(&b, "lang_en") == 0);
    CHECK(dm_cp_dirty(&b) == 0);

    // 新實例從同一份字節加載：值與顯式狀態一致。| A fresh instance loads the same bytes and matches.
    CHECK(dm_cp_parse(&b, store.document, store.length) == DM_CP_OK);
    CHECK(dm_cp_get(&b, "auto_mana") == 0);
    CHECK(dm_cp_get(&b, "eco_fps") == 1);
    CHECK(dm_cp_get(&b, "lang_en") == 1);
    CHECK(b.lang_explicit == 1);
    CHECK(b.loaded == 1);
    CHECK(dm_cp_dirty(&b) == 0);
    CHECK(b.applies == 0);

    // 加載回調路徑同樣可用。| The load-callback path works too.
    dm_cp_instance c;
    dm_cp_init(&c);
    CHECK(dm_cp_load(&c, load_fn, store.document) == DM_CP_OK);
    CHECK(dm_cp_get(&c, "eco_fps") == 1);
    CHECK(c.loads == 1);
}

static void test_malformed_and_bounds(void) {
    dm_cp_instance p;
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, NULL, 0) == DM_CP_OK);     // 空文件 = 全缺省 | empty input = defaults
    CHECK(dm_cp_get(&p, "auto_mana") == 1);

    // 未知鍵/舊鍵跳過並計數；非法值不同於上游的"非 0 即 1"，直接跳過。
    // Unknown/legacy keys are skipped and counted; an invalid value is not coerced to 1 as upstream does.
    const char doc[] =
        "version=1\n"
        "eco_fps=2\n"
        "drop_filter=1\n"
        "ver=2\n"
        "no_equals_line\n"
        "auto_mana=0\n"
        "auto_mana=1\n"
        "q3d=\n"
        "map_mark=1\n";
    CHECK(dm_cp_parse(&p, doc, strlen(doc)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);            // 非法值被忽略，保留缺省 | ignored, default kept
    CHECK(dm_cp_get(&p, "auto_mana") == 1);          // 重複鍵後者生效 | last duplicate wins
    CHECK(dm_cp_get(&p, "q3d") == 0);
    CHECK(dm_cp_get(&p, "map_mark") == 1);
    CHECK(p.unknown_keys == 2);                      // drop_filter / ver
    CHECK(p.invalid_values == 2);                    // eco_fps=2 / q3d=
    CHECK(p.lines_skipped == 1);                     // no '=' line
    CHECK(p.duplicates == 1);

    // 不支持版本：整份拒絕且實例不變。| Unsupported version: refused as a whole, instance unchanged.
    const char bad_version[] = "version=2\neco_fps=1\n";
    CHECK(dm_cp_parse(&p, bad_version, strlen(bad_version)) == DM_CP_LOAD_VERSION);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);

    // 超長單行被跳過，其他行照常。| An over-long line is skipped while the rest still applies.
    char long_line[DM_CP_MAX_LINE + 16];
    memset(long_line, 'k', sizeof long_line - 1);
    long_line[sizeof long_line - 1] = '\0';
    CHECK(dm_cp_parse(&p, long_line, strlen(long_line)) == DM_CP_OK);
    CHECK(p.long_lines == 1);

    // 整份超過上限：拒絕且保留原狀態。| Whole input over the bound: refused, previous state kept.
    char big[DM_CP_MAX_BYTES + 2];
    memset(big, '0', sizeof big);
    CHECK(dm_cp_parse(&p, big, sizeof big) == DM_CP_LOAD_TOO_LARGE);
    CHECK(dm_cp_get(&p, "map_mark") == 1);
}

static void test_language_and_mercenary_compat(void) {
    dm_cp_instance p;
    dm_cp_init(&p);
    // 文件裡沒有 lang_en：不寫回、語言行保持缺省。| No lang_en in the file: not written back, row stays default.
    const char plain[] = "version=1\nauto_mana=0\n";
    CHECK(dm_cp_parse(&p, plain, strlen(plain)) == DM_CP_OK);
    CHECK(p.lang_explicit == 0);
    fake_store store;
    memset(&store, 0, sizeof store);
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(!contains(store.document, "lang_en="));

    // merc_pot_v2 存在時對 merc_pot 有權威性（與行序無關），並在寫回時保留。
    // merc_pot_v2 is authoritative for merc_pot regardless of line order, and is preserved on write.
    dm_cp_instance m;
    dm_cp_init(&m);
    const char merc[] = "version=1\nmerc_pot=1\nmerc_pot_v2=0\n";
    CHECK(dm_cp_parse(&m, merc, strlen(merc)) == DM_CP_OK);
    CHECK(dm_cp_get(&m, "merc_pot") == 0);
    CHECK(m.merc_explicit == 1);
    char doc[DM_CP_MAX_BYTES];
    size_t doc_len = 0;
    CHECK(dm_cp_serialize(&m, 0, doc, sizeof doc, &doc_len) == DM_CP_OK);
    CHECK(contains(doc, "merc_pot=0\n"));
    CHECK(contains(doc, "merc_pot_v2=0\n"));
    // 沒有待定改動時 apply 不寫盤（等價事務），與上游一致。| With no pending change apply writes nothing.
    fake_store mstore;
    memset(&mstore, 0, sizeof mstore);
    CHECK(dm_cp_apply(&m, store_fn, &mstore) == DM_CP_OK);
    CHECK(mstore.calls == 0);

    // 玩家改過 merc_pot：寫回時同時寫 merc_pot_v2。| Changing merc_pot also writes merc_pot_v2.
    dm_cp_instance c;
    dm_cp_init(&c);
    CHECK(dm_cp_set(&c, "merc_pot", 0) == DM_CP_OK);
    fake_store cstore;
    memset(&cstore, 0, sizeof cstore);
    CHECK(dm_cp_apply(&c, store_fn, &cstore) == DM_CP_OK);
    CHECK(contains(cstore.document, "merc_pot_v2=0\n"));
}

static void test_serialize_bounds(void) {
    dm_cp_instance p;
    dm_cp_init(&p);
    char small[16];
    memset(small, 'x', sizeof small);
    size_t len = 0;
    CHECK(dm_cp_serialize(&p, 0, small, sizeof small, &len) == DM_CP_SAVE_TOO_LARGE);
    CHECK(small[0] == 'x');                          // 容量不足不留半份文件 | no partial document on short capacity

    char full[DM_CP_MAX_BYTES];
    CHECK(dm_cp_serialize(&p, 0, full, sizeof full, &len) == DM_CP_OK);
    CHECK(len > 0 && full[len] == '\0');
    CHECK(contains(full, "version=1\n"));
    CHECK(contains(full, "eco_fps=0\n"));
    CHECK(contains(full, "q3d=0\n"));
    CHECK(contains(full, "auto_mana=1\n"));
}

static void test_crlf_and_whitespace(void) {
    dm_cp_instance p;
    dm_cp_init(&p);
    const char crlf[] = "version=1\r\nitem_notes=0\r\n";
    CHECK(dm_cp_parse(&p, crlf, strlen(crlf)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "item_notes") == 0);
    // 鍵名不做裁剪：帶空格的就是未知鍵，不會被誤認。| Keys are not trimmed: a padded key stays unknown.
    const char padded[] = "eco_fps =1\n";
    CHECK(dm_cp_parse(&p, padded, strlen(padded)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "eco_fps") == 0);
    CHECK(p.unknown_keys == 1);
}

static void test_merc_migration_order(void) {
    dm_cp_instance p;
    // 只有舊 merc_pot 行：不上報，使用源缺省值 1（上游從不用舊行的值）。
    // Legacy merc_pot alone is not honoured: the source default 1 applies (upstream never uses the legacy value).
    dm_cp_init(&p);
    const char legacy_only[] = "version=1\nmerc_pot=0\n";
    CHECK(dm_cp_parse(&p, legacy_only, strlen(legacy_only)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 1);
    CHECK(p.merc_explicit == 0);

    // 有效 v2 勝過舊行，與行序無關。| A valid v2 beats the legacy line in either order.
    const char v2_first[] = "version=1\nmerc_pot_v2=0\nmerc_pot=1\n";
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, v2_first, strlen(v2_first)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 0);
    CHECK(p.merc_explicit == 1);
    const char v2_last[] = "version=1\nmerc_pot=1\nmerc_pot_v2=0\n";
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, v2_last, strlen(v2_last)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 0);
    const char v2_on[] = "version=1\nmerc_pot=0\nmerc_pot_v2=1\n";
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, v2_on, strlen(v2_on)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 1);

    // 非法 v2 不建立顯式選擇，也不覆蓋已記下的有效 v2。| An invalid v2 creates no explicit choice and keeps a valid one.
    const char invalid_only[] = "version=1\nmerc_pot_v2=2\nmerc_pot=0\n";
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, invalid_only, strlen(invalid_only)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 1);
    CHECK(p.merc_explicit == 0);
    const char valid_then_invalid[] = "version=1\nmerc_pot_v2=0\nmerc_pot_v2=x\n";
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, valid_then_invalid, strlen(valid_then_invalid)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 0);
    CHECK(p.merc_explicit == 1);
    CHECK(p.invalid_values == 1);

    // 多個有效 v2：最後一個有效值勝出，並計為重複。| Several valid v2 lines: the last valid one wins and is counted.
    const char duplicate_v2[] = "version=1\nmerc_pot_v2=0\nmerc_pot_v2=1\n";
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, duplicate_v2, strlen(duplicate_v2)) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "merc_pot") == 1);
    CHECK(p.duplicates == 1);

    // 往返：寫回保留 v2 行，重新加載得到同一值。| Roundtrip keeps the v2 line and reloads the same value.
    dm_cp_init(&p);
    CHECK(dm_cp_parse(&p, v2_first, strlen(v2_first)) == DM_CP_OK);
    char document[DM_CP_MAX_BYTES];
    size_t length = 0;
    CHECK(dm_cp_serialize(&p, 0, document, sizeof document, &length) == DM_CP_OK);
    CHECK(contains(document, "merc_pot=0\n"));
    CHECK(contains(document, "merc_pot_v2=0\n"));
    dm_cp_instance again;
    dm_cp_init(&again);
    CHECK(dm_cp_parse(&again, document, length) == DM_CP_OK);
    CHECK(dm_cp_get(&again, "merc_pot") == 0);
}

static void test_noop_apply(void) {
    dm_cp_instance p;
    fake_store store;
    dm_cp_init(&p);
    memset(&store, 0, sizeof store);

    // 空事務：成功、不調用存儲、沒有顯式選擇。| Empty transaction: succeeds, no storage call, no explicit choice.
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(store.calls == 0);
    CHECK(p.apply_skips == 1);
    CHECK(p.applies == 0);
    CHECK(dm_cp_dirty(&p) == 0);

    // 等價待定（語言/傭兵都是缺省值）：仍然成功且不寫、不產生顯式選擇，但待定被清空。
    // Equal pending values (language and mercenary at their defaults): still succeeds without a write or an explicit
    // choice, while pending values are cleared.
    CHECK(dm_cp_set(&p, "lang_en", 0) == DM_CP_OK);
    CHECK(dm_cp_set(&p, "merc_pot", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(store.calls == 0);
    CHECK(p.lang_explicit == 0);
    CHECK(p.merc_explicit == 0);
    CHECK(dm_cp_dirty(&p) == 0);
    char document[DM_CP_MAX_BYTES];
    size_t length = 0;
    CHECK(dm_cp_serialize(&p, 0, document, sizeof document, &length) == DM_CP_OK);
    CHECK(!contains(document, "lang_en="));
    CHECK(!contains(document, "merc_pot_v2="));

    // 真正改動仍然保存一次，且只包含真正改動產生的顯式標誌。
    // A real change still writes once and only produces explicit markers for the real change.
    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, store_fn, &store) == DM_CP_OK);
    CHECK(store.calls == 1);
    CHECK(p.applies == 1);
    CHECK(!contains(store.document, "lang_en="));
    CHECK(!contains(store.document, "merc_pot_v2="));
    CHECK(contains(store.document, "eco_fps=1\n"));
}

int main(void) {
    test_frozen_key_table();
    test_init_and_pending();
    test_apply_commit_and_failure();
    test_defaults_and_language_row();
    test_roundtrip_and_isolation();
    test_malformed_and_bounds();
    test_language_and_mercenary_compat();
    test_serialize_bounds();
    test_crlf_and_whitespace();
    test_merc_migration_order();
    test_noop_apply();
    printf("custom_preferences: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
