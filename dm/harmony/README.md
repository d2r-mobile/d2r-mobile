# HarmonyOS port

中文：[README.zh-CN.md](README.zh-CN.md)

A HarmonyOS NEXT (API 26) app that runs the game on the shared host and the Vulkan engine from `dm/gfx/vulkan`,
the same engine as the Android port. The port was developed on a Huawei Pura X (VDE-AL00, Kirin 9020) and a
Mate 90 Pro Max (CMM-AL10, Maleoon 955), both on HarmonyOS 7.0.0.109; this revision, on the shared engine, was run on the Mate 90 Pro Max only. It is a development build for people who
own the game: every file generated from the game stays on the player's machine and phone.

## Source layout

- `app/`: the DevEco (hvigor) project. ArkTS shell (`entry/src/main/ets`): XComponent surface, touch forwarding,
  lifecycle, folder import of the game data, unpacking of the runtime pack into `files/dm`.
- `native/`: the N-API library `libdm.so`.
  - `napi_ohos.c`: start-up, resource locations, HarmonyOS defaults (see below), lifecycle.
  - `front_ohos.c`, `touch_ohos.c`: front-end and touch, bridged to the shared touch state machine
    `dm/ios/Sources/dm_touch_controls.c`, as on Android.
  - `gameperf_ohos.c`: Game Service Kit GamePerformance (thermal level, GPU load and clock) into the perf log.
  - `android_shim/`: maps the few Android NDK interfaces the reused Android files call to their exact HarmonyOS
    counterparts (`ANativeWindow` → `OHNativeWindow`, the Vulkan surface → `VK_OHOS_surface`, log → hilog), so
    `dm/android/vulkan_present.cpp`, `dm/android/overlay_validate.c` and `dm/android/compat` build unchanged.
- `probe/tsd_probe.c`: the device probe that measured musl's thread-slot layout (`tpidr_el0 − 272`), used by the
  module patcher's `DM_TARGET=ohos`.
- `tools/gen_strings.py`: generates the ArkTS string table from `dm/i18n/ui_text.tsv`.

Shared code touched for this port is gated on `__OHOS__` (or is a new, unused-elsewhere file); Android and Apple
builds do not change behaviour. Each piece is listed in the pull request.

## Build

Requirements: DevEco Studio 6 with the HarmonyOS 26 SDK, Python 3 with the dm tools' dependencies (Pillow), and
your own game files.

1. Generate the per-player files from your game, outside the repository, as for Android: host tables and the
   skill index (`dm/ios/tools/native_art/generate.py`, `gen_vault_table.py`, `gen_hud_exp.py`,
   `dm/tools/gen_bright_levels.py`), the overlay (`dm/ios/tools/gen_romfs_overlay.py`, then
   `dm/android/tools/stage_touch_assets.py`), the patched modules (`DM_TARGET=ohos DM_RULESET=touch-run-v4
   dm/tools/patch_nso.py` for `main`, `sdk`, `subsdk0`) and the shader bundles (see "Shader bundles" in
   `dm/android/README.md`).
2. Copy `app/entry/src/main/cpp/local.cmake.example` to `local.cmake` and point it at the generated headers and
   your Python. Copy `app/build-profile.template.json5` to `app/build-profile.json5` and enter your own signing
   configuration in DevEco. Neither file is committed.
3. Debug builds can carry the runtime pack: `app/scripts/stage_debug_pack.sh <dir with out shaders overlay ui-art>`
   puts a zip into `entry/src/debugpack` (ignored by git); the app unpacks it on first launch. Without it the app
   asks for a folder with `dm/` and `data/` and imports it. Release packages must not carry the pack:
   `app/scripts/check_release_clean.py <hap>`.
4. `hvigorw --mode module -p module=entry@default -p product=default assembleHap`, then
   `hdc install -r entry/build/default/outputs/default/entry-default-signed.hap`.

## HarmonyOS defaults and measurements

`napi_ohos.c` sets these only when the environment does not (`files/dm/env` and the marker files still override):

| Setting | Why |
|---|---|
| `DM_NVN_SYNC=0` | Translation on its own thread (as iOS). Marker `dm/nvn-async` = `0` reverts |
| `DM_PACE_ADAPT=1` | Adaptive present tier (`dm/gfx/nvn/nvn_adaptive_pace.h`): game frames are released evenly at the highest divisor of the refresh rate the device sustains, stepping down on missed display slots and probing up with back-off |
| completion wait, `gfx-legacy-waitall=0` | Engine defaults on HarmonyOS only (`xbg_completion_wait.h`): fence waits are woken by the retire of the target fence instead of holding the engine lock |
| desired present timestamp, 4 swapchain images | `dm/android/vulkan_present.cpp` under `__OHOS__` |
| `DM_GFX_NATIVE_BC=0` | Maleoon has no BC textures (`textureCompressionBC = 0`): CPU decode, with a whole-block fast path in `xbg_texture.c` |
| `DM_GFX_READBACK_CACHED=1` | GPU-written, CPU-read buffers in host-cached memory (Maleoon best practice) |
| `DM_GFX_DEFER_EVENTS=2`, `DM_GFX_READ_PREFLIGHT=0`, `DM_GFX_NOLOAD=1`, `DM_GFX_PIPE_MEMO=1`, `DM_GFX_QUAD_DERIVATIVES=1`, `DM_GFX_WARP32=1`, `DM_GFX_NAN_MINMAX=1` | As Android |
| `DM_3D_SCALE_GPU=1`, `DM_3D_SCALE_DYN=1` | Dynamic 3D scale |

Mate 90 Pro Max, holding the joystick in the Rogue Encampment for 12 s (hitrace): 59.7 fps, on-screen frame
interval p50 16.6 / p99 18.1 ms, jitter 0.8 ms. In-process A/B of the completion wait on the same build: game-frame
jitter 3.7 vs 8.2 ms, frames over 16.7 ms 40 vs 179.

Not done yet: `gfx-present-native` (present at the game's output size) is off on HarmonyOS until buffer geometry
versus compositor scaling is checked on a device; the patcher's no-red-zone layout is Android-only for now
(`NOREDZONE` in `patch_nso.py`), pending regenerated modules and a device run on HarmonyOS.
