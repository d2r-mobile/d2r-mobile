# Android custom-preferences transaction module (#157)

`dm/android/app/custom_preferences.{h,c}` is a standalone, host-testable preferences module for the Android side. It
freezes the real keys, defaults and Apply/Discard/Defaults semantics of the upstream module
`dm/host/dm_custom.c` / `.h`, and stores them through an injected persistence callback instead of the game's
`sw_custom.txt` handling. No game, save, JNI, UI or App integration is involved.

## Frozen upstream facts

Taken from `dm/host/dm_custom.c` (verified against the source at this base):

- Keys, in Custom-page row order (`k_keys`):
  `lang_en, auto_mana, merc_pot, drop_light, map_mark, item_notes, icloud, pot_pick, drop_haptic, gem_pick,
  store_btn, eco_fps, q3d, loot_filter, auto_buff`.
- Defaults (`k_def`): `0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1` — `lang_en` off, `eco_fps` off (60 fps),
  `q3d` 0 (smoothness first), everything else on.
- File format: one `key=0/1` per line; a key that is absent takes its default; unknown/legacy lines (`drop_filter`,
  `ver`) are skipped.
- `lang_en` is written only after the player actually changes it; otherwise the original game's language setting is
  left alone.
- `merc_pot_v2` is the explicit-value line for `merc_pot` under the new default. The migration is resolved after the
  whole document, independent of line order: a valid `merc_pot_v2` wins over the legacy `merc_pot` line in either
  order (last valid v2 wins if repeated), and without a valid v2 the source default applies — the legacy line alone
  never changes it.
- Apply (`AcceptChanges`) commits pending values and saves only if something changed; Discard (`CancelChanges`)
  clears pending values; Defaults (`ResetToDefaults`) resets the pending values of rows 1..N-1 and deliberately
  leaves the language row alone.
- An empty or equal transaction therefore succeeds without touching storage and never invents an explicit language
  or mercenary choice; pending values are still cleared, as upstream does.

## Android-specific choices in this slice

Explicit decisions, not upstream behaviour:

1. **Strict values.** Only a single `0` or `1` is accepted. Anything else (`eco_fps=2`, `q3d=`, padded keys) is
   counted and skipped, leaving the default in force. Upstream's `eq[1] != '0'` test would coerce those to 1.
2. **Whole-file refusal.** Input larger than 2048 bytes, or a `version=` other than 1, refuses the whole document and
   leaves the instance unchanged. Per-line problems (unknown key, invalid value, over-long line, missing `=`) are
   skipped and counted so one bad line cannot discard a whole profile.
3. **Transactional save.** `dm_cp_apply` compares merged pending values with effective values first. If nothing
   really changed it returns `DM_CP_OK` without calling storage, clears the pending transaction state and leaves the
   explicit language/mercenary markers untouched. Otherwise it renders the post-commit document into the module's own
   buffer, hands it to the store callback, and commits only when the callback returns 0. A failing store leaves
   effective and pending values untouched, counts `save_failures`, and can be retried; no success is claimed.
4. **No partial writes.** Serialisation uses bounded buffers; insufficient capacity returns `DM_CP_SAVE_TOO_LARGE`
   without leaving a partial document in the caller's buffer and without calling the store.
5. **Instance isolation.** All state lives in `dm_cp_instance`; there is no global, so two instances (for example two
   profiles) cannot contaminate each other.
6. **Defaults are pending-only.** `dm_cp_defaults` never changes effective values and excludes the language row, as
   upstream does.
7. **Parse-before-commit.** `dm_cp_parse`/`dm_cp_load` build the next state in a local copy and replace the instance
   only after the whole document parsed successfully.

## Data model

```c
typedef int (*dm_cp_store_fn)(void *ctx, const char *data, size_t len);            // 0 = persisted
typedef int (*dm_cp_load_fn)(void *ctx, char *out, size_t capacity, size_t *len);  // 0 = read ok

dm_cp_instance prefs;
dm_cp_init(&prefs);
dm_cp_set(&prefs, "eco_fps", 1);          // pending only
dm_cp_apply(&prefs, store_fn, &store);    // commits only if the store accepts
dm_cp_defaults(&prefs);                   // pending defaults, language row excluded
dm_cp_discard(&prefs);                    // drops pending values
```

Status values: `DM_CP_OK`, `DM_CP_SAVE_FAILED`, `DM_CP_SAVE_TOO_LARGE`, `DM_CP_LOAD_IO`,
`DM_CP_LOAD_TOO_LARGE`, `DM_CP_LOAD_VERSION`, `DM_CP_UNKNOWN_KEY`, `DM_CP_INVALID`.

Counters (`loads`, `applies`, `discards`, `default_resets`, `save_failures`, `store_calls`, `lines_skipped`,
`invalid_values`, `unknown_keys`, `duplicates`, `long_lines`) make the parser's decisions observable instead of
silent.

## Private directory adapter (#167)

`app/custom_preferences_store.c/h` is a minimal adapter for the injected
`dm_cp_load_fn`/`dm_cp_store_fn` contract. `dm_cp_store_create(dirfd)` duplicates
the caller's private directory descriptor and owns only that duplicate; callers
keep their original fd. It uses the fixed `custom_preferences.txt` name and a
fixed `.custom_preferences.tmp` name in that directory.

- Load accepts only a bounded regular file (`DM_CP_MAX_BYTES`) opened with
  `O_NOFOLLOW|O_NONBLOCK`; absent files return an empty document (the existing
  module then supplies defaults). A FIFO or other non-regular object is rejected
  without blocking. IO errors and oversize files leave the instance's current
  settings unchanged.
- Save rejects an existing destination object that is a symlink or otherwise
  non-regular before creating anything; the link and its target are preserved.
  The private directory is expected to have a single owning control thread;
  this is not a defense against arbitrary concurrent writers.
- Save rejects content over 2048 bytes, creates a same-directory temporary file
  with `O_CREAT|O_EXCL|O_NOFOLLOW`, handles short writes/EINTR, fsyncs the file,
  and uses `renameat()` as the commit point. Failures before rename remove only
  the temporary file this call created and leave the previous file intact.
  Stale or symlink temporary names are unknown collisions and are not removed.
- A test-only compiled hook simulates failure after fsync and before rename; it
  verifies the old file, pending settings and temp cleanup and a successful
  retry. The production adapter exposes no operation table or hook.
- There is no directory-fsync/power-loss durability claim and no UI/JNI/App
  wiring. This is an offline file adapter, not device persistence acceptance.

Tests use real temporary directories and the real transaction module for
roundtrip/restart loading, missing and invalid inputs, symlink and oversize
rejection, pre-rename collision failure with pending/disk preservation, retry,
independent directories and adapter ownership. A private actual-store mutation
that skips the rename commit must fail.

## Tests

`dm/android/tests/custom_preferences.c` (160 checks) drives the real module:

- frozen key table and defaults, including `lang_en=0`, `eco_fps=0`, `q3d=0`;
- new → modify → apply → serialise → load in a fresh instance (roundtrip), with and without the load callback;
- apply with a failing store: no commit, pending kept, failure counted, retry then commits;
- discard and defaults do not change effective values; defaults exclude the language row;
- `lang_en` is written only once explicit; `merc_pot_v2` is authoritative and preserved;
- malformed, oversized, unknown, duplicate and unsupported-version inputs, CRLF and padded keys;
- two instances stay isolated;
- short-capacity serialisation leaves no partial document.

`dm/android/tests/custom_preferences_mutation.py` (CMake test `custom_preferences_mutation`) mutates the real
controller twice — ignoring the store result, and always writing `lang_en` — and requires the real tests to fail in
both cases; a missing anchor fails closed.

## Limits

This is a data/policy module plus an offline private-directory file adapter.
There is no UI, no JNI, no `native.c`/App integration, and no read or write of
the game's own settings save or of `sw_custom.txt`. Stored values do not imply
that the matching feature works on Android: several keys refer to platform
features (for example `icloud` iCloud sync) that are not implemented here.
Parent issues #39 and #19 remain open.
