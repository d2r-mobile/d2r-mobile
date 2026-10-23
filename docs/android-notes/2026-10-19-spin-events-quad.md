# Main-thread spin, event reads, sync validation and quad helper calls on the Redmi K50 Ultra (2026-10-19)

中文：[2026-10-19-spin-events-quad.zh-CN.md](2026-10-19-spin-events-quad.zh-CN.md)

Measured 09:40–11:30 NZDT on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1, Android 15), Game Turbo on, build `android/linux-build`, 3D scale held at 0.75 unless noted.

## A time-based A/B for host switches

The engine's in-process A/B (`gfx-ab-toggle`) only reaches engine switches. The per-frame TSV (`DM_NVN_TSV`) now carries `@mono_s`, the frame's absolute monotonic time. So any switch that follows the same phase (monotonic seconds / N even = on) can be split frame by frame offline, with the difference taken over adjacent on/off periods. Back-to-back separate runs aren't good enough for hitch rates. Two camp runs without the spin gave 9.3% and 5.3% of frames over 18 ms, as large a spread as the effect below.

## The guest main thread spins before its short waits sleep (PR #26)

D2R's main thread waits about 45 times a frame on guest mutexes its two job workers hold, and 81% of those waits last 10–25 µs. With `DM_HLE_SPIN_US=30` (the Android default) the thread polls its wake flag for up to 30 µs before it sleeps. `DM_HLE_SPIN_AB=10` alternated it every 10 s in one camp run (22 periods, about 6,100 steady frames per arm):

| | Spin on | Spin off | On − off, 21 adjacent pairs (95% CI) |
|---|---|---|---|
| Frames over 18 ms | 6.94% | 11.78% | −4.87 ± 0.97 points |
| Frames over 20 ms | 0.34% | 0.82% | |
| Frame time p99 | 19.08 ms | 19.76 ms | |
| Process CPU per frame | 21.67 ms | 22.22 ms | −0.51 ± 0.13 ms |

All 11 spin-on periods had fewer long frames than all 11 spin-off periods. The process uses less CPU in total with the spin.

## No direct reads of pending event words (#9)

Frame overlap (`gfx-defer-events` = 2) applies a SignalEvent's value when its slot retires. The maintainer asked whether the guest or the host reads that memory directly in the meantime.

The diagnostic marker `gfx-event-guard` makes the page of each pending event word `PROT_NONE` until the value lands. Faults are counted with their caller. The engine's own copies of guest memory open the page around the copy and are counted apart. Run with vcache, tex-dirty and wtrack off.

| Scene | Events guarded | Faults on a pending word | Other faults on its page | Engine copies |
|---|---|---|---|---|
| Camp, 110 s | 6,419 | 0 | 0 | 32,097 |
| Boot, warp into the Den of Evil, seeded fight, 108 s | 6,479 | 0 | 0 | 32,397 |

The only accesses to these pages are the engine's own:

- `stream_buffer` copies 64 bytes at page offset 0 (the word was at +0x54);
- `write_report` writes a counter report at +0x60 (next to a word at +0x108).

`nvnEventGetValue` applies pending events before it reads, and it never met a pending event (`cpu_reads_pending=0`). The CPU's `nvnEventSignal` is a plain store, and no write faulted.

## Present overlap under synchronization validation (#9, PR #28)

The Khronos validation layer (1.4.363.0 Android binaries) ran as a GPU debug layer for the app only, with `validate_sync` on and `validate_core` off, and with present overlap and frame overlap on.

It reported one hazard: the presenter's transition of each acquired swapchain image used `TOP_OF_PIPE` as its source stage, while the submit waits on the acquire semaphore at `TRANSFER` (`SYNC-HAZARD-WRITE-AFTER-READ`). That has nothing to do with overlap, and `main` has the same barrier. With the source stage set to `TRANSFER` (PR #28), the camp (about 100 s) and a warp plus seeded fight (about 110 s) gave no messages at all. So there is no hazard on the game's output image with two blits in flight, nor on the engine's resources with frames overlapped.

## Quad shuffles through the translator's helper functions (PR #27)

`main`'s guarded quad rewrite changes none of the 182 fragment modules with a shuffle. The translator emits every SHFL through a straight-line helper (Ryujinx's `ShuffleXor`), and the guard accepts shuffles only in the entry function. All 278 fragment calls pass xor 1 (86 calls) or xor 2 (192) with SHFL mask `0x1c03`, in conditions that are uniform per draw or follow the alpha-test kill.

**A second bug in those helpers.** Their clamp check is written for 32-lane warps: `(lane ^ x) <= ((lane & 0x1c) | 3)`. The device reports 64-lane subgroups in fragment shaders (`subgroup size=64 stages=0x30`). On lanes 32–63 the check fails, so every quad swap returns the lane's own value. That's half of each wave, with or without helper invocations.

PR #27 rewrites such a call at its call site, to the neighbour from fine derivatives, when three things hold:

1. Running the helper for each of 32 lanes proves a quad swap.
2. The call is in dynamically uniform control flow. A kill-only branch doesn't diverge, and a re-read of the kill predicate is uniform for the survivors.
3. The bits carry floats from producer to consumer, through Ryujinx's register variables and bitcasts.

It covers 271 of the 278 calls; the other 7 carry integers.

The call is replaced whole. Keeping the original call as a fallback for non-finite values brought the black squares back and cost about 3 ms of GPU. Seeded Den of Evil fight, scale 0.75:

| Rewrite | GPU ms | Draws | fps | Black squares |
|---|---|---|---|---|
| `linux-build`'s own (derivatives inside the helper) | 14.62–14.67 | 1,027 | 60 | none |
| Call sites, original call kept as fallback | 18.24–18.30 | 1,081 | 54 | yes |
| Call sites, call replaced (PR #27) | 15.09–15.27 | 1,077 | 60 | none |

## Tried, no measurable gain: memo hits recheck images only after a change

A descriptor memo hit rechecked every cached image's registration record. A generation counter now lets it skip them until a registration or image changes (branch `android/desc-hit-gen`, A/B target `hitgen`). In the camp, render-thread submit CPU moved by −0.066 ± 0.086 ms over 16 pairs, which isn't measurable. It's held back, since a missed generation bump would show stale descriptors.
