# Per-Tick Game State Hash Format

中文：[tick_state_format.zh-CN.md](tick_state_format.zh-CN.md)

The game's server-side logic advances in "ticks" (each tick processes all units once). Before each tick begins, the key fields of all units in the game are computed into a few hashes and recorded as one line; if the two sides are identical tick by tick, the game logic has run exactly the same way. The two sides are the **ground truth** (reference data obtained by running the original game in a patched Ryujinx emulator) and the **host** (`dmhost` from `dm/host`). Both sides run the same Switch version 3.3.94012 program, and are compared byte by byte.

- Ground-truth side: `dm/verify/oracle/swtick.py` reads it through the emulator's GDB interface, and it is written as `<run dir>/ticks.jsonl` when a run is recorded. The emulator driver scripts used for recording are not shipped with the repository, so the checks that depend on ground truth cannot be run with this repository alone.
- Host side: enabled by the environment variable `DM_TICKS=<path>`; with `DM_TICKS_N=<tick count>` it exits normally once that many have been recorded; `DM_TICKS_FULL=1` additionally outputs per-unit detail.
- Comparison: two `ticks.jsonl` files are aligned by tick number and the first differing tick number is reported for each slot; with fewer than 10 common ticks no conclusion is drawn. The comparison script `selfcmp.py` belongs to the emulator driver scripts and is not shipped with the repository.

## Capture point and base address

- **Capture point**: the server tick entry `main + 0x5d3bc0`, taken before the first instruction of the entry executes (the tick number has not yet been incremented).
- **Base address G** = `x0` at entry. G is the game object, embedded in a game hash-table node: G = node + 0x10 (node = the result of looking up the ID from the slot array `main + 0x2d665d8` in the hash table `*(main + 0x2d685e0)`).
  **All offsets in this document are relative to G**: the `+0x1b8` that the game-creation function `0x5d0bd0` writes relative to the node is `G + 0x1a8` here.
- **Tick number** = `u32 [G+0x170]` (read at entry, equal to the value before this tick begins).
- Each tick number is taken only once (the emulator reports the same breakpoint hit repeatedly; deduplicate by tick number); tick numbers must be consecutive one by one, and a gap is reported red.

## One line of JSON per tick

```
{"tick":39,"n":[4,12,24,0,21],"h":["…16 lowercase hex digits…" ×6],"bad":0}
```

- `n`: the counts of the 5 unit classes (0 player / 1 monster / 2 object / 3 missile / 4 item).
- `h`: 6 hash slots (5 unit classes + global seed).
- `bad`: the number of unit pointers that could not be read + the number of units whose self-reported type does not match the table they are in.
- Other fields (game, dt, reads) are for human reading only and take no part in the comparison.

## Unit tables and fields

All offsets are relative to the start of their respective structure.

| Class t | Table | Notes |
|---|---|---|
| 0 player | G+0x2230 | Each table has 128 buckets (u64 pointers), each bucket a singly linked list `next = [u + 0x158]`, at most 4096 per chain |
| 1 monster | G+0x2630 | |
| 2 object | G+0x2a30 | |
| 3 missile | G+0x3230 | Note that the table order and addresses of 3 and 4 are not monotonic |
| 4 item | G+0x2e30 | |

Fields of unit u:

| Field | How to read |
|---|---|
| type, cls, id, mode | u32 [u+0x0], [u+0x4], [u+0x8], [u+0xC] |
| act | u8 [u+0x18] |
| rlo, rhi (unit seed) | u32 [u+0x28], [u+0x2C] |
| Coordinates (t = 0/1/3 only) | path = u64 [u+0x38]; u16 xf=[path+0], x=[path+2], yf=[path+4], y=[path+6]; when path is 0 or unreadable all four are taken as 0; for the other classes all four are always 0 |
| statlist (t = 0/1/4) | sl = u64 [u+0x88]; arr = u64 [sl+0x30], cnt = u32 [sl+0x38]; take min(cnt, 96) entries, each 8 bytes as-is (u32 a = layer<<16 \| statId, u32 b = value) |
| Item extras (t = 4 only) | d = u64 [u+0x10]; iq=[d+0x0], is_lo=[d+0x4], is_hi=[d+0x8], iflags=[d+0x18], iinit=[d+0x10] (u32); when d is 0 all five are taken as 0 |

## Hash

FNV-1a 64, offset basis `0xcbf29ce484222325`, multiplier `0x100000001b3`, byte by byte, all numeric values as little-endian u32.

1. The hash of each class t starts from the offset basis; all units are sorted by (type, id) ascending and then folded in turn into the slot corresponding to their type (those with type ≥ 5 are not counted).
2. Each unit folds in, in order:
   - 9 u32: type, cls, id, mode, act, x, y, xf, yf
   - 2 u32: rlo, rhi
   - t = 4 only: 5 u32: iq, is_lo, is_hi, iflags, iinit
   - t = 0/1/4 only: u32 entry count, then each entry's 8 bytes as-is
3. Slot 6 (global seed) = the offset basis with the 8 bytes of `[G+0x1a8]` folded in (u32 lo + u32 hi as-is).
4. Output is a string of 16 lowercase hex digits.

## Determinism (the precondition for comparison)

For the two sides to be identical tick by tick, the inputs must be identical:

- **Game-creation seed**: the game-creation function `0x5d0bd0` reads `u32 [main + 0x20a0074]`; when it is ≠ `0xffffffff` the seed = {that value, 666}, otherwise it is taken from the system clock. Both sides must write the same value before game creation: on the ground-truth side the driver script's `--game-seed N`, host `DM_GAME_SEED=N`.
- **Game-creation time**: the system time is read once at game creation. On the ground-truth side the driver script's `--fix-time <seconds>`, host `DM_FIXED_TIME=<seconds>`; give the same value.
- **Save**: both sides each start from a fresh copy of the same standard save (`$DM_ROOT/data/switch/save_ref_warlock`); the save content fingerprint is computed with `dm/verify/savesha.sh` and noted down before the run.
- **Input**: the input the client sends to the server always follows the command packet sequence recorded from the ground truth (see [command_packet_format.md](command_packet_format.md)). Both command packets and **system-class messages** (`0x6d` heartbeat, `0x6f` progress, etc.) must have their original bytes injected at their original tick numbers, and messages of the same kind produced by the host itself are discarded (the `0x67` join at game entry is let through as usual). The tick number at which a system-class message arrives depends on how fast the client is; among them `0x6f` determines when the server inserts a sync, so it is timing-dependent input; without injecting it the per-tick state will not match.
