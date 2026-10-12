# native_binding — test fixtures for skill binding parsing

中文：[README.zh-CN.md](README.zh-CN.md)

For unit tests only. The app does not read these two files at run time, and the tests do not modify them.

**The public repository does not ship these two fixtures**: they are two tables captured from the memory of the running game. Without them, the three tests below that read the fixtures are skipped automatically with a one-line notice, and the other tests run as usual. To run those three tests you have to capture the two tables from the running game yourself and put them in this directory under the file names below; the assertions in the tests are written against the contents of the snapshot described below, so with different contents the assertions have to be changed accordingly.

## Background

The skill buttons on the right need to show "which skill this button is bound to right now". In the memory of the running game this information is two tables, which the shell fetches read-only and then parses (`../../../Sources/dm_touch_game_bridge.c` does the fetching, and `dm_tg_parse` in `../../../Sources/dm_touch_game_state.c` does the parsing):

- **Button mapping table**: for each of the six gamepad buttons (B, A, Y, X, R, ZR), which normal skill slot and which alternate skill slot it corresponds to. The alternate slots are the set used while ZL is held.
- **Binding table**: the skill ID bound in each skill slot; an empty slot is `0xFFFFFFFF`.

## Files

| File | Size | Contents |
|---|---|---|
| `mapping.bin` | 114 bytes | A snapshot of the button mapping table. Parsing uses only the first 72 bytes: 6 entries, each of 3 32-bit little-endian integers (logical button, normal slot, alternate slot) |
| `bindings.bin` | 896 bytes | A snapshot of the binding table: 32 entries of 28 bytes each; the 32-bit little-endian integer at the start of each entry is the skill ID |

The contents of this snapshot, which the assertions in the tests are written against:

- Button mapping: B → normal slot 1 / alternate slot 7, A → 0 / 6, Y → 2 / 8, X → 3 / 9, R → 4 / 10, ZR → 5 / 11.
- Bindings: slots 0, 2 and 7 of the first group have skills, the rest are empty; the second group, starting from entry 16, is entirely empty. Slot 7 is the town portal skill, so in the parse result the way to press town portal is ZL + B.

Checksums (SHA-256):

```
b9777515f225f235ac1223dd04bf15db250f1240f893511737ceecf357367362  mapping.bin
4d4f4cde5d08f35f9a988ac61aff54913499d32322ac95fa3ce7763c16a11262  bindings.bin
```

## Who uses them

The `test` target of `dm/ios/tests/Makefile` passes these two files as arguments to three tests (when the files are absent, these three tests are skipped):

| Test | What it verifies with them |
|---|---|
| `t_native_binding` | Parsing: in the normal case it reads out the button mapping and the town portal button combination above; when the length is too short, a slot number is out of range, a button is duplicated, or the observed skill ID does not match, it rejects in every case |
| `t_game_bridge` | Puts the two tables into a block of simulated game memory and verifies the whole path from reading memory to producing a snapshot: reading does not modify registers or the tables; no snapshot is produced when not in a game |
| `t_native_hud_bridge` | Same as above, and additionally verifies that a snapshot is produced only after confirming that the call comes from the HUD of the current game, and that after switching to another game it has to be confirmed again |

To run: `make -C dm/ios/tests test`.
