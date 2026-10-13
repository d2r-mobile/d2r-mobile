# Rift minion life: regression checklist

**English** | [简体中文](qa-rift-pet-life.zh-CN.md)


The verification matrix here was proposed by the reporter of [#23](https://github.com/jhaizhou-ops/d2r-mobile/issues/23). It has been checked against the fixed behaviour and rewritten as a regression checklist. "Verified" rows were actually run and read by the maintainers; "not run" rows have not been run by anyone yet — contributions welcome.

## Root cause and fix (merged)

- The original game's get-stat function returns the **total** for units like minions (base plus skill / aura bonuses, with percentage bonuses already applied), while the set-stat function writes the **base**. The old code read the total, multiplied it by the Rift multiplier and wrote it back as the base.
- When a minion has "+N% maximum life" (in the data tables only Battle Orders and Oak Sage give this stat to minions), the game multiplies our written value by (1 + N%) again; the next read differs from the last write and was taken as a new base. Life grew by (1 + N%) every 10 ticks.
- At the stat cap (8,388,607 life) the total overflows to a negative number and the get-stat function returns the minimum, 1 — the "back to 1 after the cap" seen in logs. At that moment the minion really has only a few points of life.
- Fix: base, comparison and the sidecar file all use the base stat; the game applies the bonus itself. The clamp leaves headroom so that the total stays within the stat cap. Abandoning restores the base on the same tick (as completion does). A bound demon whose base exceeds the original cap formula (left in saves by the old code) is reset to the cap on load.

## Reading the numbers

- `DM_RIFT_TEST_PETDUMP=1`: every 125 ticks a line with, per minion, pet type, class, id, level, total maximum life, base, and current life.
- One line per adjustment, showing base a → b (cached base, tier, multiplier) and total c → d. **Normally: one line on entering, one on leaving (completion / abandon), none in between.**
- One line when the one-time repair triggers: the character's bound demon base X exceeded the cap and was reset to Y.
- Test switches: `DM_RIFT_TEST_PETPCT=<percent>` (adds +N% maximum life to every minion), `DM_RIFT_TEST_START=<tick>:<tier>`, `DM_RIFT_TEST_ABANDON=<tick>`, `DM_RIFT_TEST_SAVE=<tick>` (asks the game to save).
- Expected values by hand: boosted base = base × tier multiplier; total = boosted base × (1 + N%). Example: base 27000, tier 77 ×3.891, +77% → base 105057, total 185950.

## Regression matrix

| # | Scenario | Expected | Status |
|---|---|---|---|
| 1a | Minion with +N% enters a Rift at tier 10 or above | 1 adjustment line; readings stay constant | Verified (bound demon, +77% via test switch; tiers 27 and 77) |
| 1b | Same, without a bonus | 1 line; total = base | Verified |
| 1c | Staying in town (with bonus) | 0 lines | Verified |
| 1d | Battle Orders applied / expiring, entering / leaving Oak Sage range inside a Rift | base unchanged, total follows the bonus; 0 lines | **Not run** (real skills never exercised) |
| 1e | Weapon swap, skill level-up, resummon | a resummoned unit is boosted once from its new base; otherwise no lines | **Not run** |
| 1f | Two minions of the same class (the reporter's case: two Defilers) | one line each, independent | **Not run** |
| 3 | Very large base whose boosted total would exceed the stat cap | clamped so the total stays within the cap; never drops to 1 | Unit test only (such a base cannot be produced in a real game) |
| 4a | Abandon | base restored on the same tick (1 line), before the pause | Verified |
| 4b | Completion in time / over time | base restored on the completion tick | By code reading; that line was not read separately |
| 4c | Town and back, next tier, minion death and resummon, quitting to a new game | no extra lines; a new game starts from a clean base | **Not run** |
| 5a | Save while boosted, then reload | a "base taken from sidecar" line, then 1 restore line | Verified |
| 5b | Save left by the old code (bound demon base above the original cap) | 1 "exceeded the cap, reset" line on load; 0 after saving and reloading | Verified (one demon type, one cap tier) |
| 5c | Same, but written to a very small value | cannot be told apart from a legitimate value; left alone — re-bind the demon | Known limitation |
| 5d | Several same-class minions boosted at save time; a save overlapping the restore | each returns to its own base after reload | **Not run** |

## What was not adopted

The diagnostic patch submitted with this checklist (a detailed, rate-limited record per adjustment) was not merged: after the fix a normal run has only one or two adjustments, and records designed around the total no longer discriminate. The minion dump line described above already carries total, base and current life, which is enough to check this table.
