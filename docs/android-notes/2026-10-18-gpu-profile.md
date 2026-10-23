# GPU profiling on the Redmi K50 Ultra, and 120 Hz (2026-10-18)

中文：[2026-10-18-gpu-profile.zh-CN.md](2026-10-18-gpu-profile.zh-CN.md)

Measured 17:40–18:40 NZDT on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1, Android 15). Build `android/linux-build` with the Android defaults, camp, 3D scale held at 0.6 unless noted. The diagnostics are on branch `android/pipe-stats`, which is not for merging. The 120 Hz change is PR #17 (branch `android/frame-rate`).

## Which GPU profilers can work here

- **Perfetto** on this phone offers only `android.gpu.memory` for the GPU: no `gpu.counters` and no `gpu.renderstages`. So Android GPU Inspector's system profile gets no GPU counters or render stages.
- **KGSL performance counters**, which AGI and Snapdragon Profiler read: a probe run as the shell and inside the app (`run-as`) opens `/dev/kgsl-3d0`, lists the counter groups (SP 24 counters, UCHE 12, TP 12, RB 8, LRZ 4 and others) and reserves counters. Every read fails with `EPERM`: the kernel does not let apps or the shell read them on this unrooted phone.
- **Vulkan:** no `VK_KHR_performance_query`. The driver does offer `VK_KHR_pipeline_executable_properties`: per-pipeline compile statistics. That is the profiler used below.

## What the driver says about the shaders

`gfx-pipe-stats` (with `gfx-no-warm`, so every pipeline is created on demand) logs the statistics of every pipeline and the draws per frame of each one.

| Fragment shader (main 3D pass) | Draws per frame | Instructions (ALU) | Registers | Max active invocations |
|---|---|---|---|---|
| `5c1ca4e7c3e4657c` | 284 | 3,710 (1,800) | 22 | 1 |
| `38f344adf801b153` | 119 | 3,938 (1,959) | 25 | 1 |
| `358197ac3db59cbc` | 72 | 3,393 (1,685) | 22 | 1 |
| `acd655d88042767c` | 18 | 4,163 (2,135) | 25 | 1 |
| Simple shaders (UI, depth, shadow) | | 1–400 | 0–5 | 3 |

- The heavy lighting shaders run at the lowest occupancy the driver reports (1 against 3 for simple shaders), so they hide little texture latency.
- No shader uses 16-bit ALU. Only one compute shader uses scratch memory.
- The heaviest vertex shaders are 545–983 instructions; the shadow pass's 910.
- The main-pass shaders do not discard, so early depth rejection works after the depth pre-pass.

## Tried, no gain

- **`spirv-opt -O` on the five heaviest shaders** (loaded through the diagnostic `files/dm/spv-override/<key>.spv`): the translator stores each Maxwell register in its own variable (about 2,800 variables, 5,000 loads, 3,000 stores per shader), and `spirv-opt` removes almost all of them (37 variables left, a third of the size). The driver's output does not change: registers and invocations equal, instructions within 1%, main pass 6.71–6.79 ms against 6.78. The Adreno compiler already does this work.
- The translated shaders contain no half-float emulation and no function-local arrays.
- **128-wide fragment waves** (`VK_EXT_subgroup_size_control`): the driver allows a required subgroup size (64 or 128) only for compute shaders (`requiredSubgroupSizeStages` = compute).
- **Without `robustBufferAccess`** (diagnostic `gfx-no-robust`): the same compiled code, 89,662 instructions over 108 shaders against 89,663, the same registers. Bounds checks are free here, so the engine keeps relying on them.
- **Transient command pools** (`VK_COMMAND_POOL_CREATE_TRANSIENT_BIT`): resetting a slot's pool still takes 404–415 µs a frame and ending its command buffer 268–290 µs, the same as without the flag.
- **Buffer-to-texture copies** happen while loading (136 a frame at first, none in the camp), so skipping repeated ones would not help play.

## 120 Hz

The panel does 60, 90 and 120 Hz, and the system setting is 120 (`user_refresh_rate=120`; the launcher runs at 120 Hz). While the game runs, the display drops to 60 Hz: the game's surface casts no frame-rate vote, and Android then picks the lowest rate that fits.

The presenter now votes 120 Hz for the game surface (`ANativeWindow_setFrameRate`, from `libnativewindow` looked up at run time), and the display stays at 120 Hz in the game. The game still paces to 60 fps (`nvn_pace_hz` takes the largest whole fraction of the refresh rate at or above 60). The difference is a frame that misses 16.7 ms: it now stays on screen for 25 ms instead of 33.3 ms.

SurfaceFlinger timestats (present-to-present) in the camp, scale held at 0.75, the same build, 47.9 fps in both runs:

| Display | Frames on screen for 16.7 ms | 25 ms | 33.3 ms | 50 ms |
|---|---|---|---|---|
| No vote: 60 Hz | 71% | — | 28% | a few |
| 120 Hz vote | 39% | 61% | 0 | 0 |

At the same frame rate, no frame now stays on screen for two whole 60 Hz refreshes: the steps are 1.5× instead of 2×, and the longest hold drops from 33–50 ms to 25 ms. No dropped frames in either run.

Marker `gfx-frame-rate` = N (Hz) changes the vote; 0 casts none.

## Presenting at the game's output size (PR #18)

The presenter's swapchain was 2712×1220. Every frame it cleared the whole image and scaled the game's 2000×900 output up with a nearest blit; the display engine then rotated the layer into the portrait panel (`DEVICE` composition, `ROT_90`; never GPU composition). Now the window's buffers take the game's output size (`ANativeWindow_setBuffersGeometry`), the presenter copies 1:1 without a clear, and the display engine scales the layer along with the rotation.

| | Swapchain | Presenter GPU per frame |
|---|---|---|
| Before | 2712×1220 | 230 µs |
| After | 2000×900 | 163 µs |

About 13 MB less memory traffic per frame, a smoother upscale than nearest, the same picture otherwise. Marker `gfx-present-native` = 0 keeps the old swapchain; `gfx-present-timing` logs the presenter's GPU time.

## The touch overlay redrew on every refresh (PR #19)

In a fight the busiest threads were the game thread (72% of the X2 core), two game workers (15–20% each) and Android's own `RenderThread` at 12%. `TouchOverlay`, the view that draws the touch controls over the game, asked for another redraw at the end of every `onDraw`, so HWUI rendered it on every display refresh: 1,228 frames in 10 s at 120 Hz. It now checks the native layout once per refresh and redraws only when that changes.

| Den of Evil | HWUI frames in 10 s | `RenderThread` CPU | Game GPU per frame |
|---|---|---|---|
| Before | 1,228 | 12.2% of a core | 14.55 ms |
| After | 0 (nothing pressed) | not among the busy threads | 14.43 ms |

Pressed buttons still highlight and clear on release.

## Async translation is still slower

With `DM_NVN_SYNC=0` (translation on its own thread, the iOS mode) the seeded Den fight ran at 35.2 fps against 59.6: the game waited 13–15 ms a frame on fences for work the translation thread had not submitted yet, half the frames dropped, and the dynamic scale went down to 0.6. Sync mode stays the Android default.

## MIUI Game Turbo: the biggest single step

MIUI keeps an app's CPU and GPU clocks capped unless the app is in its Game Turbo list (Security → Game Turbo / Game Space → +). The manifest's `appCategory="game"` is not enough. With D2R Mobile added, `settings get secure gb_boosting` reads 1 while the game runs and the caps lift:

| Camp | Not in Game Turbo | In Game Turbo |
|---|---|---|
| X2 core cap | 2.25 GHz | 3.19 GHz (runs at it) |
| A710 core cap | 2.57 GHz | 2.75 GHz |
| Scale held at 0.6 | 53 fps, GPU 17.7 ms, CPU 11.6 ms | **60.0 fps** (the pacing cap), GPU 13.8 ms, CPU 10.6 ms |
| Production defaults (dynamic scale) | settles at 0.65, 50–51 fps | **stays at 0.75**, 58–60 fps, GPU 15.6 ms; median frame interval 16.6 ms |

The GPU is about 28% faster, so its clock was capped too. The catch is heat: the battery went from 32.4 to 38.0 °C in about 4 minutes, and the X2 cap had already dropped to 3.00 GHz. Players on MIUI should add the game to Game Turbo; sustained play will run warmer, and the dynamic scale lowers the resolution if the clocks fall.

## A correctness bug found on the way

Four compute shaders (`098a126f92bf533a`, `7c16e695919ffc6e`, `fc85981c8d9148c9` with 8×8 workgroups, `fbc4a0eb955f5960` with 128×1) build per-tile lists with the warp-aggregated append. The highest active lane reserves `bitCount(ballot)` entries with one atomic add, and every lane writes at `base + bitCount(ballot & lanes below it)`.

The translation takes only `subgroupBallot(...).x`, the 32 lanes of a Maxwell warp. Adreno runs 64-wide subgroups, so lanes 32–63 are missing from the count and all compute the same offset past the reserved range: their entries overwrite each other. The fix is a SPIR-V rewrite that emulates 32-wide warps inside the 64-wide subgroup (the virtual lane `id & 31`, the ballot word `id >> 5`, shuffles kept inside the half). Done in PR #21: see [2026-10-19-review-fixes.md](2026-10-19-review-fixes.md), with what it costs.
