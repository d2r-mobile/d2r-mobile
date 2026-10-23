# GPU clock governor, driver probes, two CPU trims, full-coverage first draws and the new touch controls (2026-10-20)

中文：[2026-10-20-probes-touch.zh-CN.md](2026-10-20-probes-touch.zh-CN.md)

Measured on 2026-10-20 (NZDT) on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1, Android 15), camp, cold starts, unless noted otherwise. GPU times are the per-pass timer (`gfx-pass-profile`), frame totals from `NVN PERF v=1`.

## First: at a steady 60 fps, GPU time per frame follows the clock governor

With the eight main-pass fragment shaders replaced by ones that write a constant (a timing probe; the picture is wrong), the main pass fell from 5,465 to 2,984 µs, but the frame total only from 14.02 to 13.55 ms: every other pass got slower (shadow map 678 → 884 µs, the rest 6,852 → 8,364 µs). The GPU's clock governor lowered the clock when the work shrank. At a steady 60 fps it seems to settle near 14 ms of the 16.7 ms period, which is why the camp lands there whatever changes. Neither phone exposes the GPU clock or busy counters without root.

So at 60 fps, per-frame GPU milliseconds understate both savings and costs: a saving shows up as a lower clock (less power and heat), a cost as a higher one, until the clock is at its maximum. GPU comparisons need a saturated GPU, where frame time is the work at the top clock. The sections below that compare GPU times at 60 fps carry that caveat.

Two ways to see the work anyway:

- **Saturate the GPU.** At a fixed 3D scale of 1.0 the camp runs at 56.8–57.2 fps with GPU 17.3 ms a frame, over the 16.7 ms period, while the render thread stays at 9.6 ms: the GPU is the limit and runs at its top clock. That is the bench for GPU A/Bs (merged `linux-build`, APK `2faa2215…`).
- **Use the shadow map as a clock meter.** In the camp it is a fixed 1024 × 1024 pass with fixed geometry, so its time tracks the clock: 678 µs at 0.75 (N0), 651 µs at 0.8, 653 µs at 1.0 (top clock), 884 µs with the trivial fragment shaders (clock about 23% lower). Dividing pass times by it compares work at equal clock.

At equal clock, 0.8 costs about 5% more GPU work than 0.75 and 1.0 about 28% more. The main pass at 0.75 fits about 3.1 ms that does not depend on resolution plus 2.1 ms that scales with pixels, and its fragment shaders are about 3 ms of it.

## What a higher 3D scale costs (read with the caveat above)

| Scale | Build | GPU per frame | Main pass | fps |
|---|---|---|---|---|
| 0.75 | `526f03f` | 14.11 ms | 5,232 µs | 60.0 |
| 0.80 | `d20ca16` | 14.24 ms | 5,302 µs | 60.0 |
| 0.85 | `d20ca16` | 14.93 ms | 5,546 µs | 60.0 |

(Main pass: median over the profiled frames whose main pass has the same 547 draws and 601,923 vertices.)

From 0.75 to 0.8 the full-screen post passes grow about 10% for 13.8% more pixels, while the fixed-size passes (UI, shadow map, compute) come out about 3–4% faster, and the main pass grows only about 1%: the governor raised the clock to absorb the extra work. Corrected for clock (above), 0.8 costs about 5% more GPU work and 1.0 about 28%. A higher scale is not free: it costs clock, power and the headroom heavier scenes need, and at 1.0 the camp no longer holds 60 fps. The dynamic scale steps down from the configured value to 0.70, 0.65 and 0.60, so a higher default would also need finer levels.

## Small Vulkan probes run from adb shell

- **Memory types for buffers.** For every buffer usage the driver offers only types 0 (device-local) and 6 (host-visible, coherent and cached; `memoryTypeBits` 0x41). GPU reads are equally fast from both: 23 GB/s through a compute shader and 13.6 GB/s through vertex fetch, 64 MB each. Moving vertex data to device-local memory would gain nothing. CPU reads from type 6 run at 0.94 GB/s.
- **Descriptor-set binds.** With 6 sampled images and 9 dynamic uniform buffers, pinned to the A710 cores: about 0.7 µs to rebind the same set with the same offsets (the driver does not skip it), and about 1.5–1.9 µs once any offset changes, whether 1 or all 9 change. Binding the uniform buffers as a second set costs about the same (1.6–1.7 µs), so splitting the set would not help. The camp does about 1,356 binds a frame with 9.1 offsets each, 508 of them rebinding the same set with new offsets. `0e00099` adds which offsets change in those rebinds to `NVN PERF binds`, to size a push-constant path for the uniform block that changes on every draw.
- **Uniform-only arithmetic.** The Adreno 730 compiler does not hoist it: a 1,500-op chain on uniform-buffer values costs the same per vertex as the same chain seeded by a vertex input (about 13 ps per vertex per op). But after the engine's SPIR-V clean-ups the game has little of it: 45 ops in the main mesh vertex shader, 38 in its pre-pass shader, and in the main fragment shaders mostly bitcasts and negations. Doubling the main and pre-pass vertex shaders' uniform work added 72 µs to the pre-pass and nothing to the main pass. Not worth moving to the CPU.

## Two CPU trims (`1ca118c`, `d19b6d3`)

- **`thread_info` through `getrusage`.** The compat layer's `thread_info(THREAD_BASIC_INFO)` opened `/proc/self/task/<tid>/stat` on every call, and `hle_nvn.c` asks for the calling thread twice per submit. For the calling thread it is now one `getrusage(RUSAGE_THREAD)` call. In the call graph of the render thread, `thread_info` went from 0.72% to 0.04%. PR #32 brings it to `main`.
- **No condition signal for a spinning waiter.** With `DM_HLE_SPIN_US` the guest main thread polls its woke flag before a short wait sleeps; a waker that finds it spinning now skips `pthread_cond_signal`, which on bionic makes a futex system call whether anyone waits or not. In 60 s of camp, 21.8 of 67.6 guest wakes per frame (32%) skipped the signal. These wakes come from the job workers, so the render thread's own share did not change.

## Depth attachments nothing uses (`a0b961c`, diagnostic)

`NVN PERF pass_ends depth_idle` and `dz=1` on `NVN GPASS` lines mark passes that carry a depth attachment no draw tests and no clear touches. The camp has about 7 per frame, but they are tiny (draw-less clears and small blits, 7–36 µs each, about 117 µs a frame together). The large full-screen passes that carry depth do use it. No change follows.

## Full-coverage first draws (`93b7ca7`, `56ada6d`; off by default)

The upstream no-load path was removed because a vertex count and a full viewport do not prove that a draw covers every pixel. `56ada6d` brings back the Metal side's proof (`first_draw_covers` in `nvn_mtl.m`, the pure function `nvn_cover.h` and its unit test `t_cover.c`) for the Vulkan engine. The vertex position must be one input as it is: `93b7ca7` finds that input in the SPIR-V, with a unit test, and on the game's library it accepts the four full-screen vertex shaders and rejects the mesh shaders. The positions are read from the vertex and index snapshots the draw hands to the GPU. A proven first draw begins its pass with a twin render pass whose color `loadOp` is `DONT_CARE`.

In the camp the proof holds for 13 passes a frame (9 more fail on blending, 2 on shaders, 1 on depth). Culling had to be passed to `nvn_cover` as the Metal side does: with culling required off, none qualified. In an in-process A/B over 12 adjacent 10 s phase pairs, GPU on − off was −0.19 ± 0.27 ms, the 13 passes themselves differed by under 5 µs, and the other passes did not move either, so the governor does not explain it away: the driver evidently renders these single-draw passes in place. Screenshots with it always on show the camp unchanged. It stays behind marker `gfx-noload` (env `DM_GFX_NOLOAD`, A/B target `noload`) for GPUs where the load does cost.

## The new touch controls (`android/touch-port`, merged as `c5ba12a`)

`main`'s Android touch series is now on `linux-build`: the community design from upstream #74 (dark socket buttons with a metal rim, the right-thumb skill fan with the game's own skill icons, the I/II skill-set switch on release with slide-out cancel), the native bottom HUD, the overlay contract and its startup check. `linux-build`'s four original touch fixes (`2fa66d5`, `6f37d61`, `722f0db`, `cfd8f56`) gave way to `main`'s versions of them. Two additions: the map button draws its original icon in a socket, as on iOS, and the joystick image carries no caption; `stage_touch_assets.py` now stages the repository's `dm_touch_joy.png`, which nothing put into the overlay before. Tests: `test_touch_snapshot` 13 cases, `t_touch_controls` 48/48, `t_skill_aim` 82/82, the asset and overlay contract checks.

Builds now need `dm_touch_native_art.c` in `DM_GEN_DIR` and an overlay staged from the same source fingerprint (here `2457068d…`); `README.md` describes the generation. On the Redmi the overlay was verified file by file after import, startup logged `overlay verified`, and the camp ran at 60.0 fps with render-thread CPU 9.4–9.7 ms, as before.

## A short free-play session

The user played 2.5 minutes on `a0b961c` with production defaults (dynamic scale on, per-pass profile on). 150 one-second windows: fps median 60.0, 10th percentile 59.9; the 4 windows under 58 fps are all in the first 7 seconds of loading in. GPU 13.9 ms median (maximum 14.6), render-thread CPU 9.0 ms median (maximum 10.3 after loading). The scale stayed at 0.75; the battery went from 35.3 to 37.0 °C.

## A second phone: OnePlus 13 (Adreno 830)

PJZ110, Snapdragon 8 Elite (SM8750), Android 17, 2376 × 1080 at 120 Hz, 4 KB pages. It runs the game from the same files as the Redmi (`06ac8b59…`, pre-touch overlay): character select at 60 fps with GPU about 10.5 ms and render-thread CPU about 5.5 ms; camp at fixed 0.75 at 59.1–59.7 fps, render-thread CPU 10.8–11.2 ms, GPU 13.3–14.2 ms. ColorOS caps its CPUs when the app is not in its Games app (the 4.32 GHz cores at 1.2–1.7 GHz during the run), so its CPU numbers are not yet comparable. ColorOS refuses `adb install` while the screen is locked; once it is unlocked the install goes through.

With `88e3e959…` (merged `linux-build` with the touch overlay) and production defaults the camp runs at 59.5–60.3 fps at 3D scale 0.75, with render-thread CPU 10.7–12.2 ms and GPU 13.2–13.6 ms. The user then played the level-40 Sorceress below for 93 s in the camp and the Blood Moor. Per 1 s window: fps median 59.9, 10th percentile 57.5; GPU median 11.2 ms (maximum 13.9); render-thread CPU median 7.1 ms (90th percentile 9.6), with the CPU caps moving between 1.2 and 4.3 GHz. Of the 12 windows below 58 fps, 4 fall in the first 10 s after entering the game and 3 at the two area changes; the other 5 coincide with first-time pipeline builds for the new spell effects (22–50 ms each, up to 157 ms in one second). The warm-up list records those pipelines, so later runs build them at start. The battery stayed at 33–34 °C.

## Where the scale-1.0 frame goes: barriers and the driver's shader statistics

On the saturated bench (fixed 3D scale 1.0, camp, 17.3 ms of GPU a frame) the main pass takes 6.9 ms, the depth pre-pass 1.16 ms, two compute passes 0.70 and 0.48 ms, and 14 single-draw full-screen post passes about 4.5 ms together. Comparing passes of 1000 × 450 and 2000 × 900 with similar shaders gives about 40 µs per pass plus about 170 µs per million pixels.

**Barriers are not that fixed cost.** Every game barrier and every compute dispatch records a full `ALL_COMMANDS` barrier, about 99 a frame (66 game barriers, 26 at dispatches). Branch `android/barrier-ab` (`bd02b78`) adds diagnostic A/B targets whose off phases end the pass but record no barrier. In-process A/B, 10 s phases, GPU on − off:

| Target | Barriers skipped per frame | GPU on − off |
|---|---|---|
| `gamebar` | 66 | +0.44 ± 0.65 ms (12 pairs) |
| `dispbar` | 26 | −0.44 ± 0.54 ms (13 pairs); no pass moved by more than 50 µs |

Narrower barriers could at best recover what skipping them all does, so they are not worth writing. Image layouts are not it either: the engine keeps images in `GENERAL` and transitions only around uploads and copies.

**The main-pass fragment shaders are heavy and run at the lowest occupancy.** With `gfx-pipe-stats` the driver reports, for the most-used main-pass fragment shader (`38f344ad…`, 133 draws a frame), 3,614 instructions, 1,872 32-bit ALU instructions, 11 16-bit ones and a footprint of 22 registers, with `Max_active_invocations` 1; the other three main-pass shaders are alike (17–21 registers, 1). Post-chain shaders have 33–260 instructions, 2–5 registers and 3. The SPIR-V of `38f344ad…` holds 775 multiplies, 527 FMAs, 166 adds, 30 inverse square roots and about 40 exp / log against 26 texture operations: real lighting work, nearly all of it 32-bit. An exact rewrite has little to take out; half precision would, but it changes the image.

## Dynamic 3D scale: every 0.05 step below a higher ceiling (`8564533`)

Resolution is the lever left, so the dynamic scale may now start higher. Its levels were the configured value and then a fixed 0.70 / 0.65 / 0.60, so a configured 0.85 fell straight to 0.70 on the first drop. They are now the configured value and every 0.05 step below it down to the floor (0.60): the default 0.75 keeps 0.75 / 0.70 / 0.65 / 0.60, and `DM_3D_SCALE=0.85` steps down through 0.80 and 0.75. `t_oe` checks the table.

On the Redmi with `DM_3D_SCALE=0.85` (3D target 1700 × 765 instead of 1500 × 675), from 34.5 °C:

| Run | fps median / 10th percentile | GPU median | Level changes | Battery |
|---|---|---|---|---|
| Camp, 3 min | 60.0 / 60.0 | 14.95 ms | none | |
| Den of Evil fight (seed 12345), 2.8 min | 60.0 / 59.9 | 14.14 ms | none | 34.3 → 37.0 °C |
| The same fight, 9 min | 60.0 / 59.9 (1st percentile 59.0) | 14.07 ms | none | 34.4 → 42.0 °C |

The windows under 58 fps were all at the load and the warp. The phone was charging throughout. The same 9-minute fight at a fixed 0.75, without the harness's screenshots and profiler, gave fps 60.0 / 59.9 (1st percentile 58.6) with GPU 13.87 ms, and the battery went from 34.1 to 37.0 °C after 4 minutes, 39.0 °C after 6 and 40.0 °C at the end, against 39.0, 40.4 and 42.0 °C at 0.85. So 0.85 keeps the frame rate and costs about 2 °C after 9 minutes of fighting. An earlier 0.75 run with the screenshots is not usable: each screenshot stalled about 20 frames, and the controller, which counts any late frame, stepped down to 0.60 for stalls that resolution cannot fix. The Android app now ships 0.85 as its ceiling (`8584433`; other phone platforms keep 0.75, and `files/dm/env` still overrides it). On the Redmi, with no env file, the camp runs at 60.0 fps with GPU 15.3 ms and render-thread CPU 9.8 ms.

## A level-40 test character, levelled by the game itself

The host's test switches (`files/dm/env`) give a test character a full set of skills without editing its save:

| Line | Effect |
|---|---|
| `DM_TEST_STAT=13=17270791` | at tick 30, experience set to the level-40 mark (`experience.txt`; `k_exp[39]` in `dm_hud_exp.h`) |
| `DM_TEST_SPAWN=50:19:3,3` | at tick 50, a Fallen pack (class 19) next to the player |
| `DM_TEST_MON_KILL=19@10` | 10 ticks later, the game's own kill function with the player as the attacker |
| `DM_TEST_SAVE_AT=250` | the game's own timed save |

The kills' 18 experience points pushed the total past the mark, and the game's own level-up took the Sorceress from level 1 to 40 in one tick: 195 stat points, 39 skill points, life 40 → 79, mana 35 → 113. Skills then go in through the original add-skill-point packet 0x3b. `DM_TEST_SKILL_ADD` sends up to 12 skills a run, prerequisites first, and `DM_TEST_C2S` up to 8 more (`3b`, the skill id in 2 bytes, `00 00`). Two runs gave all 30 Sorceress skills one point each and left 9 skill points; `DM_TEST_GIVE=mp5:2:8,rvl:2:4` with `DM_TEST_GIVE_INV=1` added potions. The character moves between phones as its two save folders (`files/save/<name>` and `files/save.work/<name>`, 7 files each).

## Open: one-frame black lighting tiles at a new point light (OnePlus 13)

The user saw black squares while moving. Screen recordings of walks in the camp show them at each strike of Charsi's hammer: on the frame the strike's spark light appears, the ground in the 2 tiles of the engine's 32 × 20 light-culling grid around it renders black (the edges fall on whole tiles: columns 9.97–10.99 and rows 0.00–2.04 of 1488 × 675), with Charsi, the anvil and the flash drawn over it. The next frame is correct. There were 4 hits in 13 s near the forge. None on the Redmi so far, though Charsi never hammered in view in its runs.

Ruled out:

- **The quad-swap rewrite's gaps.** All 22 helper calls in the 108 fragment shaders of the session carry floats in uniform control flow, by the call-site analysis of PR #27.
- **The 32-lane-warp rewrite.** It covers both compute shaders with subgroup operations, at 64 and 128 lanes.
- **NaN handling of min / max / clamp.** With their NaN-safe forms in 118 shaders (through `spv-override`) the tiles remain.
- **The engine's newer features.** With all of them off (sync mode, about 24 fps) the tiles remain.
- **Float-to-integer conversion.** Both GPUs saturate and map NaN to 0 as Maxwell does (a compute probe).

Ready for the next try: overrides that turn NaN colour outputs white (is it a NaN at all?), and branch `android/robust2` (`2e9e589`), which with marker `gfx-robust2` enables `robustBufferAccess2` so that reads past a binding return zero as on the Switch.
