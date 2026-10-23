# Night runs, a race between the frame slots, and the full merge of main (2026-10-21)

中文：[2026-10-21-night-merge.zh-CN.md](2026-10-21-night-merge.zh-CN.md)

Measured on 2026-10-21 between 01:02 and 03:40 (NZDT) on the Redmi K50 Ultra (Adreno 730), production defaults
(3D scale ceiling 0.85, dynamic) unless noted otherwise, runs of about 3 minutes, saves restored after each session.
`NVN PERF v=1` windows of 60 frames; "long-frame lines" are the HUD's `[长帧]` lines.

| Run | Build | Scene | fps median / p10 / p1 / min | GPU ms median (p99) | CPU ms median (p99) | long-frame lines | battery °C |
|---|---|---|---|---|---|---|---|
| R1 | `17c2894` | camp, a screenshot every 10 s | 60.0 / 58.7 / 48.3 / 31.4 | 15.33 (15.48) | 10.08 (12.33) | 135 | 32.7 → 37.0 |
| R2 | `17c2894` | Den of Evil fight, cold caches | 60.0 / 59.6 / 31.4 / 28.6 | 13.96 (16.04) | 7.97 (19.82) | 156 | 34.4 → 36.9 |
| R3 | `17c2894` | the same, `gfx-async-pipelines` = 1 | 60.0 / 59.9 / 30.9 / 30.0 | 14.13 (15.47) | 8.26 (18.53) | 116 | 34.4 → 37.0 |
| R4 | `17c2894` | Cow Level fight, life raised to 10,000 | 60.0 / 59.9 / 49.5 / 28.6 | 13.82 (15.29) | 8.47 (10.33) | 87 | 34.4 → 37.0 |
| E2 | `7234175` | Den fight, cold caches | 60.0 / 59.9 / 28.8 / 24.1 | 14.11 (15.21) | 8.02 (19.11) | 106 | 34.0 → 36.3 |
| E1 | `7234175` | the same, background builds on | 60.0 / 59.8 / 28.7 / 26.5 | 13.84 (15.22) | 7.75 (19.57) | 135 | 34.0 → 36.3 |
| M0b | `e9d8bf8` | camp | 60.0 / 60.0 / 49.3 / 33.4 | 15.21 (15.31) | 9.94 (10.12) | 86 | 30.7 → 35.0 |
| M1b | merge `50af02c` | camp | 60.0 / 59.9 / 46.1 / 33.4 | 15.17 (15.26) | 9.80 (10.14) | 62 | 34.4 → 38.0 |
| M2b | merge `50af02c` | Den fight | 60.0 / 59.9 / 47.2 / 28.3 | 14.13 (15.18) | 8.06 (10.22) | 95 | 34.4 → 36.7 |

The camp is not seeded and draws either about 1,060 or about 2,300 calls a frame, switching within and between runs, so
separate runs can differ in CPU by more than a change does; compare CPU in-process, or only at matched draw counts.

## Copies per frame

The camp asks for 18.67 MB of storage snapshots a frame but copies none of them in full: write tracking copies only the
dirty pages of the large read-only buffer (26.5 KB a frame). Copied in full each frame: uniform 2.08 MB, texel 1.00 MB,
vertex 0.06 MB, index 0.01 MB (`snap_mb_per_frame`, full copies against requested since `e27cbab`).

## The dynamic 3D scale and stalls that are not the GPU

With a screenshot every 10 s (each stalls about 20 frames), the scale stayed at 0.85 for the whole of R1. Before `e267a6b`
the same harness stepped it down to 0.60 (T4 on 2026-10-20): now a run of late frames counts only its first 3 frames unless
it lasts 1 s.

## First-time pipeline builds and area changes

- From cold caches (warm list and driver cache moved aside) the Den fight creates 214-231 pipelines, 3.2-3.7 s of driver
  time; one 60-frame window built 15 of them in 536 ms, and the slowest single build took 118 ms. The fight's p1 is then
  29-31 fps. Warm launches create none (R1, R4).
- Background builds for translucent draws (`fc46144`) never engaged: D2R resets its samples counter 21 times a frame and
  counts samples for every draw from its first reset on, but records no samples-passed report (`ops=21.0/4.0/0.0`), so the
  rule "a counted draw is never skipped" excluded every draw. `7234175` excludes counted draws only once the game has
  recorded a samples report. With it (E1) 517 draws waited for 67 background builds, and the graphics thread compiled 147
  pipelines (2.2 s) instead of 214 (3.2 s), but the tail did not move (p1 28.7 against 28.8 fps): the worst frames are
  opaque pipelines, which still compile in the frame. The switch stays off.
- An area change with warm caches costs 3-4 frames of 25-38 ms (R4, into the Cow Level); from cold caches single frames of
  238 and 312 ms follow it.
- The driver pipeline cache was written by the graphics thread every 300 frames while pipelines were new: the whole cache
  (17.7 MB here), six times in each warm launch, because the warm-up's 392 builds counted as new although the driver had
  them. Each of those six saves in a warm camp launch was followed by a 24-46 ms frame. `e9d8bf8` saves on its own thread
  (39-86 ms a save there) and counts a creation as new only when it took 2 ms or more: the same launch then saved 3 times.
  Warm-up builds of a warm cache average 2.7 ms on this driver, so that threshold still lets some through.

## Heavier scenes

The Cow Level with the level-40 Sorceress and raised life held 60 fps (median), p1 49.5; GPU 13.8 ms a frame at 0.85, CPU
8.5 ms. The game's main thread is the busiest (71% of a core); the phone was short of memory (166 MB free, 3.4 GB in swap).

## Heat and battery

Each 3-minute run took the battery from 34.4 to about 37.0 °C (38.0 for the merge camp run, 39.2 for 180 s at scale 1.0);
it cooled back to 34.4 °C in 5-6 minutes at the home screen. No run came near 42 °C. The battery stayed at 99-100% on the
charger.

## The occlusion queries nobody reads are worth keeping

On the saturated bench (camp, fixed scale 1.0, in-process A/B every 10 s, target `occ`), turning the ~47 occlusion queries
a frame off made the GPU slower: 18.78 against 17.09 ms a frame (on − off −1.75 ± 0.24 ms, 20 pairs), 52.6 against
57.1 fps. D2R never reads them. A guess is that an active query changes how the Adreno driver renders a pass; it was not
followed up. Recording queries only after a samples report would have cost those 1.75 ms. Beginning them without
`VK_QUERY_CONTROL_PRECISE_BIT` (Q1, the same bench) changed nothing: GPU +0.01 ± 0.06 ms, CPU −0.05 ± 0.12 ms.

## Pending views across slots (`17c2894`)

A pending view (a snapshot or copy source over a GPU result not yet written back) points into the producing slot's
readback arena. When the producer retires and records again while the other slot, still on the GPU, holds such views, the
CPU copies of the new recording go into the memory those reads use; with `gfx-wb-skip` the copied bytes can be older guest
memory, since the superseded writeback never landed. `NVN PERF xview` counts it: 6 views and 1 such reuse every frame in the
camp, the Den of Evil and the Cow Level (views of 16 KB, 64 KB and two of 5 KB). Main gates pending-view chains for the same
lifetime question.

`gfx-xview-arena` (`cf72c52`, off; A/B target `xarena`, `91d6956`) records into a second readback arena in that case, so
the views keep their memory; no wait is added. It swapped once a frame, as intended, with the same GPU time, but in-process
(XA, camp, 20 pairs) it costs 1.72 ± 0.21 ms of CPU a frame: descriptor memo keys hold buffer handles, and with each slot's
readback buffer alternating, about 145 sets a frame are rebuilt (memo misses 145 against 0, set-cache misses 155 against
10). So it stays off; a fix has to keep each slot's buffers stable. It remains a switch for testing whether this race
causes the one-frame black lighting tiles on the OnePlus 13 (2026-10-20); that is not shown.

## The full merge of main (`4f859cb`, PR #27 as `6213e2c`, then `c2f9fa4`, `2594e7a`, `50af02c`)

Main (`d187015`) carries the maintainer's adapted ports of PRs #3, #9-#10, #12-#16, #19-#21 and #23, the debug environment,
the pool-flags registry, the `Download/S2X/D2R Mobile/data` path and iOS changes. Where both sides changed the same engine
code, this branch's later Android versions stay and main's fixes come in where they apply to them:

- Kept: the previous-draw pipeline memo (its shaders, layouts and passes are freed only at shutdown, after the `clear_*`
  calls that drop it), the inline `get_image` fast path (PR #30), the occlusion runs (now under main's helper names, so
  main's `occlusion_batch_test` runs against them) and the texture dirty-page checks.
- From main: overflow-safe writeback ranges and supersession, `copy_texture_buffer` (CPU reads resolved before any image
  reference; BC copies through the row/image-stride plan), the ASTC-to-decoded-BC blit plan, the resident texel bounds,
  the quad/warp wrappers (per-shader counts; capabilities rechecked after a warp rewrite) and the write-tracking range guard.
- Main's per-operation read preflight (`beebb5b`) is behind `gfx-read-preflight` / `DM_GFX_READ_PREFLIGHT`, on by default
  as on main. The Android app turns it off: with deferred events about 8 reports are always pending, so it would sync the
  GPU before nearly every draw, and in the camp and Den of Evil logs no CPU access ever met a pending result
  (`why=...a0`).
- The resident texel path falls back on any overlap with a pending writeback, not only a fully covering one: without the
  preflight a partly covering one can be pending when a set is built.
- The presenter keeps the 120 Hz vote by default (main resets it to 0 at each initialization).
- The App keeps `Download/data` when `Download/S2X/D2R Mobile/data` does not exist. The first merged build looked for
  `.build.config` in `Download/data` and stopped at start-up on the Redmi, whose resource directory has none (the overlay
  provides it).
- The touch overlay fingerprint moves from `2457068d` to `364c0b0b` (main changed `dm/i18n/ui_text.tsv`); only the
  strings archive and `.build.config` of the overlay differ. A merged build needs the re-staged overlay.

On the phone (M1b, M2b against M0b, same linux-build commit): the same frame rate, GPU and CPU (at matched draw counts CPU
9.81 against 9.95 ms and 8.00 against 8.11 ms, GPU 15.18 against 15.22 and 14.94 against 14.92 ms); no errors; quad
rewrites in 45 sites against 27, all but one of the translator's helper calls rewritten (`00f1902d880682c7` keeps one,
`call_skip_use`), warp32 in two shaders. A 60 s recording of the Den fight (M3b) shows no black squares; its only
transient dark blocks are the lightning spell's own flash.

Tests: all of main's engine tests run here now (the CPU ones, and the WASM ones with the NDK clang and a node binary).
Four were adapted to this branch (`test_read_preflight_order`, `pipeline_memo_source_test`, `resident_texel_test`,
`occlusion_batch_test`) and one fixed (`texture_cache_regression`: stubs for the dirty-page tracker its extracted code
calls; it had failed on this branch since `e1c5366`).

## Morning: the three open leads (09:00-11:10)

### Where the occlusion queries' 1.75 ms comes from

The per-pass profile on the saturated bench (camp, fixed scale 1.0, one profiled frame in 600; P1 with the queries, P2
with marker `gfx-no-occlusion`) puts it in the main 3D pass: 6.46 ms a frame with the queries, 7.70 ms without. A
3-target pass of 34 draws took 0.66 against 1.00 ms. The depth pre-pass, which has the main pass's draws and vertices,
did not move (1.21 against 1.19 ms), nor did the shadow map or the compute dispatches. The HUD pass (744 four-vertex
draws) took 0.82 ms with the queries and 0.52 ms without, but leaving the queries out of four-vertex draws alone (A/B
target `occquad`, E5) changed nothing (GPU −0.07 ± 0.12 ms), so that difference comes with the queries elsewhere. No
query placement worth changing was found.

### The race between the slots' readback arenas, closed

Two ways of moving the producer's buffers out of the reader's way both cost CPU: a second readback arena 1.72 ± 0.21 ms
a frame, and the other half of the same chunks 1.85 ± 0.22 ms. About 145 memoized descriptor sets a frame bind read-only
storage at a fixed buffer and offset, and those missed every frame.

`gfx-xview-guard` (`f03bdf7`) leaves the buffers where they are. A new readback buffer that overlaps a range the reader
viewed is not written by the CPU: its contents go to a stream buffer, and a prologue command buffer submitted ahead of
the slot's own copies them in after a barrier that orders the reader's work first. In-process (XG, camp, 20 pairs) it
wrote 6 buffers, 1.37 MB, a frame from the GPU at no measurable cost: CPU −0.15 ± 0.21 ms, GPU −0.17 ± 0.15 ms. It is on
by default in the Android app (`4529e08`) and replaces `gfx-xview-arena`. Whether it changes the one-frame black tiles on
the OnePlus 13 is still to be seen there.

### A first launch: a seed for the warm-up list

A first launch (`DM_GAME_SEED=12345`, driver pipeline cache removed) from the menus and the camp into a Den of Evil
fight, from the warp on:

| Run | warm-up list | pipelines compiled in frames | long frames | ≥ 50 ms | ≥ 100 ms | worst | p1 fps |
|---|---|---|---|---|---|---|---|
| W0 | none | 55 (1.2 s) | 39 | 13 | 6 | 207 ms | 43.0 |
| W1 | the list file | 0 | 12 | 0 | 0 | 45 ms | 59.6 |
| W2 | from the seed | 0 | 82 | 2 | 0 | 52 ms | 58.5 |

Before the warp W0 compiled another 171 pipelines in frames (2.2 s), W1 and W2 none. (The first version of this table
gave W0's 226 pipelines and the p1 values 30.7, 44.4 and 48.5 fps for the whole run.) With a list the warm-up threads
built its 392 pipelines within about 6 s of start-up (9.8 s of compile time on 3 threads, none failed), and the game took
215 of them. W2's long frames are not pipeline builds: neither W1 nor W2 compiled one in a frame. `9dec4ac` copies
`shaders/vk-pipeline-list.seed` to `files/dm/vk-pipeline-list.bin` while that file does not exist: make the seed from
devices' lists with `dm/android/tools/merge_warm_lists.py` and ship it with the shaders (PR #33 for main).
