# `main`'s preflight, the memo-path CPU trims on `main`, and where the camp's GPU time goes (2026-10-19)

中文：[2026-10-19-main-preflight-gpu.zh-CN.md](2026-10-19-main-preflight-gpu.zh-CN.md)

Measured on the afternoon of 2026-10-19 (NZDT) on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1, Android 15) with Game Turbo on. Every run is a cold start in the camp with the 3D scale held at 0.75 (`DM_3D_SCALE_DYN=0`). Under the cooling rule (the user set it today), a run stops if the battery passes 42 °C, and the next one starts at 34 °C or lower.

## Frame overlap on `main` (#9) and the preflight (#29)

`main` (`recorded build`) with local test-only hooks: the data path, the Debug env file, and a skip of the overlay manifest check. The full A/B, with APK hashes, is on #9.

| Run | Settings | fps | Submit CPU ms | GPU ms | Submits / waits per frame |
|---|---|---|---|---|---|
| A | `main` defaults | 5.79–5.92 | 146.0–149.1 | 74.6–74.9 | 11 / 11 |
| B | frame overlap (`gfx-defer-events` = 2) | 5.87–6.12 | 141.3–146.5 | 74.2–74.9 | 10 / 10 |
| E | preflight off (local hook `DM_GFX_PREFLIGHT=0`) | 11.81–12.72 | 61.8–67.4 | 24.6–28.4 | 3 / 3 |
| C′ | preflight off, frame overlap, present overlap | 40.49–40.88 | 19.65–19.70 | 20.54–20.59 | 1 / 1 |
| F | C′ plus `main`'s other opt-ins | 43.09–43.60 | 17.62–17.73 | 20.11–20.14 | 1 / 1 |
| F′ | F with the preflight on | 7.37–7.46 | 121.8–123.1 | 73.8–73.9 | 10 / 10 |

`recorded build` calls `resolve_pending()` before every draw, dispatch, clear, copy, present and readback. Whenever a writeback or counter report is pending, that call is a full GPU sync. In the camp that happens about 6 times a frame for writebacks and 3–4 times for reports, so every frame becomes 10–11 submit-and-wait round trips. While it is on, frame overlap and the other opt-ins change almost nothing. Issue #29 proposes a range-based preflight: resolve only when a pending result overlaps a range the operation reads on the CPU, and keep the descriptor-entry invariant as a check over those ranges.

## The memo-path CPU trims on `main` (the changes)

The four trims from `recorded build` (`memofront`, `descreserve`, `tlsgate`, `imgfast`), ported to `main`. `main`'s `get_image` wraps the lookup in a CPU-read sync (`xbg_image_after_cpu_access`). The fast path takes only calls that read no guest memory, and so it skips the map lookup, the descriptor copy and the pending-writeback scan.

The trims can't show up in `main` as it is. So they were measured with the preflight off and `main`'s opt-ins on, using a local A/B target that switches all four together: 360 s at 10 s phases, battery 31.5 → 41.5 °C.

| | Trims on | Trims off | On − off, 38 adjacent pairs (95% CI) |
|---|---|---|---|
| Submit CPU ms | 16.69 | 17.00 | −0.45 ± 0.24 |
| fps | 45.70 | 45.07 | +0.95 ± 0.48 |
| GPU ms | 20.00 | 19.83 | +0.15 ± 0.19 |

That is about 2.5 times the saving on the Linux test build (about −0.17 ms), because `main`'s `get_image` did more per call.

## Where the camp's GPU time goes (the Linux test build)

`gfx-pass-profile` now also counts the vertices (or indices) times instances each pass draws (`verts=`, source snapshot`). Build experimental build plus the local diagnostic source snapshot` (driver statistics and SPIR-V overrides, not for merging), APK SHA-256 `d7bf79427c9d7f97e2a68f6253ee20499a6d3076d9cf72cb9bc454cd6c044950`. These are medians over 12 profiled frames; the run averaged 60.0 fps and 15.84 ms of GPU time.

| Part of the frame | GPU µs | Notes |
|---|---|---|
| Main 3D pass, 1500×675 | 5,988 | 547 draws, 601,923 vertices |
| 16 full-size single-draw passes | 2,618 | post-processing, 149–256 µs each |
| Shadow maps, 1024×1024 | 1,780 | the first: 92 draws, 237,561 vertices, 1,500 µs |
| Depth pre-pass, 1500×675 | 1,360 | the same 547 draws and 601,923 vertices as the main pass |
| Compute, 10 dispatches | 1,314 | |
| UI, 2000×900 | 1,030 | 871 draws |
| 31 small passes | 987 | downsample and blur chains |
| Effects, 3 RGBA16F targets | 634 | 19 draws |
| Total | 15,703 | |

The driver's statistics for those shaders:

| Shader | Instructions (ALU) | Registers | Max active invocations |
|---|---|---|---|
| Main-pass vertex shader `af64b417689666bf` | 546 (455) | 8 | 2 |
| Pre-pass vertex shader `b28286da8956e9a4` | 545 (424) | 7 | 2 |
| Shadow-map vertex shader `3deaf68fd65b5298` | 892 (300) | 16 | 1 |
| Main-pass fragment shader `358197ac3db59cbc` | 3,360 (1,657) | 22 | 1 |
| Post-processing fragment shaders | 33–259 | 2–5 | 3 |

What the numbers suggest:
- **Vertex work is a large share of the frame.** The pre-pass does almost no pixel work, yet it takes 1.36 ms for 602k vertices through a 545-instruction vertex shader. The main pass runs a vertex shader of the same size over the same vertices again. The shadow pass's 892-instruction shader runs at the lowest occupancy, at about 6.3 ns a vertex.
- **About a third of the main and pre-pass vertex shaders' arithmetic is the same for every vertex of a draw.** An SSA analysis finds 146–149 of their roughly 470 arithmetic operations fed only by constants and uniform buffers. It looks like 2D simplex noise of the object's position (skew factors `(√3−1)/2` and `(3−√3)/6`, `mod 289` hashing). Only 17 of those values reach the per-vertex code. Hoisting them would need a per-draw evaluation (CPU or a compute pass), which this engine doesn't have yet.
- **The shadow shader's skinning reads constants one 32-bit word at a time.** The translator turns each Maxwell `LDC` with a computed address into `(addr >> 2) >> 2` for the vec4 and `(addr >> 2) & 3` for the component. The bone offsets are multiples of 48 bytes, so every component is in fact a constant: all 48 fold with a known-bits analysis.
- **The full-size post-processing passes are short shaders over 4–8 MB targets,** so they are most likely limited by memory traffic rather than ALU.

## Shader experiments (`gfx-pass-profile`, the same camp run each time)

Diagnostic overrides through `files/dm/spv-override`, each run from a phone at 34 °C or lower. The table gives medians over 12 profiled frames, in µs.

| Run | What changed | Main pass | Pre-pass | Shadow map | Frame total | Picture |
|---|---|---|---|---|---|---|
| G0 | nothing | 6,019 | 1,356 | 1,498 | 15,703 | correct |
| G1 | nothing (the overrides failed to load) | 5,997 | 1,352 | 1,494 | 15,760 | correct |
| G1b | 9 lighting shaders: half precision (upper bound); shadow skinning shader: `spirv-opt -O` + component fold | 6,038 | 1,008 | 663 | 14,647 | broken |
| G5 | shadow skinning shader: component fold only, on the raw module | 6,004 | 1,342 | 1,491 | 15,684 | correct |
| G2 | the same 10 shaders: `spirv-opt -O` only | 6,021 | 1,364 | 1,502 | 15,706 | correct |
| G8 | every shader, in the engine: component fold, then single-store temporaries forwarded | 5,435 | 1,059 | 711 | 14,337 | correct |

- **Half precision gains nothing and breaks the picture.** Marking about three quarters of the heavy lighting shaders' float arithmetic `RelaxedPrecision` (everything except coordinates, depth, LOD and exact bit patterns) left the main pass unchanged. The picture showed flat red props, black blocks, a black character and magenta noise on the waypoint.
- **G0 and G1 are the same run twice,** so differences under about 0.1 ms are noise.
- **The shadow skinning shader got 2.3× faster only when both changes were applied.** The component fold alone (G5) and the optimizer alone (G2) changed nothing. With both, the folded constants sit directly in the access chains, and the driver presumably no longer treats the indexes as dynamic. The pre-pass gained too, because the same shader draws about 42 skinned objects there. All 48 folded indexes were checked against random inputs (500 and 2,000 per index, on both module forms) with no mismatch.

## Two exact clean-ups of the translator's output (source snapshot`)

G8 is what the engine now does at shader load on Android: two passes after the quad and warp rewrites, both exact.
- **Component fold (`xbg_spirv_ldc.cpp`).** The low four bits of every 32-bit integer are tracked through multiplies, adds, subtracts, constant shifts, bitcasts, integer conversions and single-store temporaries. An `& mask` (mask below 16) whose result is then fully known becomes that constant.
- **Single-store temporaries forwarded (`xbg_spirv_temps.cpp`).** A Function variable stored once, with the store and every load in one block and no name or decoration, is removed: each load reads the stored value instead.

Over the whole shader library (6,931 modules), the fold changes 3,017 modules (29,838 indexes), and the forwarding removes 5.1 million variables from 6,920 modules (42% fewer SPIR-V words). Every rewritten module passes `spirv-val` (Vulkan 1.1). Unit tests: `tests/ldc_fold_test.cpp` and `tests/forward_temps_test.cpp`. In the game, 1,212 indexes folded and 138,770 variables were forwarded in the shaders the camp loads.

G0 → G8, by pass:

| Pass | G0 µs | G8 µs |
|---|---|---|
| Shadow map (skinning shader) | 1,498 | 711 |
| Main 3D pass | 6,019 | 5,435 |
| Depth pre-pass | 1,356 | 1,059 |
| Effects | 611 | 487 |
| UI | 1,030 | 1,114 |
| Light-culling compute | 639 | 699 |
| Frame total | 15,703 | 14,337 |

The `NVN PERF` GPU time went from 15.84 to 14.44 ms a frame. The UI and compute increases are within the noise between G0 and G1.

**Confirmed on the committed build.** L1 is the same camp run (20:16, battery 31.3 °C at the start) with experimental build `recorded build`, APK SHA-256 `b6df8760e71b5a587f66ef1cd9f2195eb85a49f8edf21aac2cfac5d76fcfb9d7`, with no overrides and no diagnostics (1,283 indexes folded, 141,737 temporaries forwarded):

| Pass | G0 µs | L1 µs |
|---|---|---|
| Shadow map | 1,498 | 674 |
| Main 3D pass | 6,019 | 5,272 |
| Depth pre-pass | 1,356 | 1,028 |
| Effects | 611 | 494 |
| UI | 1,030 | 1,067 |
| Light-culling compute | 639 | 668 |
| Frame total | 15,703 | 14,020 |

`NVN PERF` GPU time was 14.12 ms a frame against 15.84, 10.7% less. L2, the seeded Den of Evil fight on the same build, rendered the Fallen, the zombies and the Sorceress correctly in every screenshot, so skinned models are unaffected. The dynamic scale only goes down from the configured 0.75, so this headroom keeps it at 0.75 in heavier scenes. It could also justify a higher default. Switches: `DM_GFX_LDC_FOLD` and `DM_GFX_FORWARD_TEMPS`, which the Android app sets to 1; the markers `gfx-ldc-fold` and `gfx-forward-temps` (1 / 0) still win.

The warp rewrite's table of literal operand positions treats every word of an image instruction after the coordinate as a literal. Only the image-operands mask is one; the dref value, the gather component and the operand values are ids. The new passes use the correct table. In the warp rewrite the table only decides whether a tracked subgroup value has another use, so an image operand derived from a lane id, a ballot or a mask would be missed. In D2R's 26 compute modules no image operand after the coordinate is derived from one (checked by following every value computed from them), so this changes nothing today.
