# Android diagnostic APK

中文：[README.zh-CN.md](README.zh-CN.md)

This NativeActivity development app bundles **only owned synthetic fixtures**. It is not a playable D2R build. No game data or Switch keys enter the APK. Package: `org.d2rmobile.android.diagnostic`; ARM64 only, minimum API 29, target API 35. No network or shared-storage permission is requested.

At startup it reads a synthetic artifact, rebinds gates, maps RX/R/RW pages and attempts a native guest-TLS round trip in the actual app process. Log tag: `D2R-Diagnostic`. When a window exists and the lifecycle is ready it draws the owned indexed-textured scene (#117, see below), reads it back and copies it into an Android native window. This is a one-frame engineering diagnostic, not an optimized game presenter. Pause/resume are logged; low memory releases the GPU context, and redraw recreates it. No background rendering loop, game resource importer, saves, game controls or audio is implemented here.

## Owned indexed-textured scene (#117)

`app/diagnostic_scene.c` owns the whole scene: a nonuniform 4x4 RGBA8 texture (16 pairwise distinct texels), four vertices at NDC ±0.75 with UV 0..1 and six `uint16` indices. `app/native.c` copies those vertices into the fixed 16-byte layout of the accepted #115 call `dm_vk_draw_indexed_textured_offscreen()` and renders a 256x256 RGBA8 readback. The packaged shaders are `indexed_textured.vert.spv` and `indexed_textured.frag.spv`; `app/build.sh` stages exactly those two files from `android/arm64-v8a/vulkan/`, and no other shader asset is packaged.

Verification is the same scene data: 20 probes (the centre of every texel plus the four clear corners) are derived from the documented geometry by `dm_diag_scene_check_pixels()`. A mismatch logs the first failing pixel with its expected and observed RGBA and the frame is not presented, so a wrong image is visible as a log failure rather than a plausible-looking window.

Drawing is bounded by the existing lifecycle controller. `dm_diag_scene_ready()` requires the `owned-diagnostic` state and the Surface the controller currently owns, so there is no draw before a Surface exists, while paused, while stopping with outstanding work, after stop, in `failed`, for a stale window token or from a late callback after stop. The window path additionally re-checks readiness and the staged shader assets.

`app/present_policy.c` is the presentation seam: it requires `WINDOW_FORMAT_RGBA_8888`, `stride >= width`, a capacity covering exactly `width*height*4` bytes and 64-bit size arithmetic. It clears only the visible row area, copies a centred crop (cropping the source when the window is smaller) and never reads or writes stride padding. A rejected copy writes zero destination bytes. Asset-copy, Vulkan-create, draw and window-lock failures are logged with their actual code; the readback buffer is freed on every path and the Vulkan context is destroyed only by stop/low-memory.

Tests added with the integration:

| Test | What it verifies independently |
|---|---|
| `app_diagnostic_scene` | Scene literals (16 distinct texels, ±0.75 geometry, fixed indices) and the probe checker against a canvas the test computes itself; one changed texel and one dirty corner are located; short capacity/null/zero-size canvases are rejected; a real lifecycle controller suppresses drawing across pause, resume, Surface loss, stopping with pending work, stop and late callbacks. |
| `app_present_policy` | Format/stride/capacity/overflow boundaries, zero writes on every rejection, centred placement, centred crop and untouched stride padding when resizing. |
| `vulkan_app_diagnostic_scene` | The same owned scene through the accepted #115 API on the host Vulkan device (MoltenVK on this Mac), 20 probes then a changed-texel control that must fail at the expected probe. |

```sh
python3 dm/i18n/scan_literals.py
bash dm/android/test.sh
PATH=/opt/homebrew/bin:$PATH bash dm/android/app/build.sh
bash ci/scripts/ci.sh host
DM_CI_ALLOW_ARM_CROSS=1 bash ci/scripts/ci.sh android
```

Unverified: no install or launch on a phone, no device `ANativeWindow_lock` format/stride behaviour, no Adreno or Android Vulkan driver validation, no swapchain, frame scheduling or game presenter. The 2026-10-17 checkpoint below belongs to the earlier fragment-coordinate pattern; the current APK has not been reinstalled or observed on any device.

## Lifecycle ownership

`app/lifecycle.c` is the production NativeActivity lifecycle controller. Its
states are `created -> starting -> loading -> owned-diagnostic`, with `paused`,
`stopping`, `stopped` and `failed` transitions. The diagnostic backend is
explicitly labelled `owned-diagnostic`; `running` is reserved for an admitted
game backend and is never entered by this app. A load failure is visible as
`failed`, performs one host cleanup, and requires a stopped/restarted controller
before another start.

The controller owns one acquired `ANativeWindow` reference, not a
callback-scoped borrowed pointer. `onNativeWindowCreated` acquires and
`onNativeWindowDestroyed`/stop releases it. Replacement acquires the new
reference first; with work outstanding the old reference is retained in one
retired slot until the final `work_end`, and matching destroy callbacks defer
rather than release it early. A second replacement while that retired slot is
occupied is rejected as explicit backpressure. Stale callbacks are ignored.
Engine cleanup is called once, stop is idempotent, and a stop requested while
asynchronous work is pending remains in `stopping` until `work_end` completes it.
The controller is single-threaded on the Android activity/control thread.

Failure is also drain-aware. If pause, resume or a Surface acquire fails while
work is outstanding, the controller remains `failed`, preserves the pending
count and owns the Surface until the last `work_end`; cleanup then runs exactly
once. `stop` acknowledges a drained failure and allows restart, but does not
erase the outstanding count early. `dm_app_lifecycle_destroy()` returns 0 and
keeps the pointer valid while work is pending or an active controller has not
been stopped; the caller must retain it and retry after `work_end`/`stop`.

The current diagnostic draw and gate load are synchronous, so the NativeActivity
caller normally stops and destroys immediately. A future asynchronous game/engine
owner must keep the controller pointer, call `work_end` from the owning thread,
and retry `stop`/destroy only when `dm_app_lifecycle_pending_work()` reaches zero.
It must not free the controller or release a Surface on the Activity callback's
behalf while work remains.

## MotionEvent delivery and diagnostic consumer (#32)

The NativeActivity input looper now translates Android `MotionEvent`s into the
existing reusable r2d touch core through `app/touch_bridge.c`. `DOWN`,
`POINTER_DOWN`, `MOVE`, `UP`, `POINTER_UP` and `CANCEL` are handled on the same
looper/core thread. Pointer IDs are opaque Android IDs; event time is
`CLOCK_MONOTONIC` nanoseconds; physical pixels are divided by the activity
density before the adapter call. Action-index pointer selection supports
non-contiguous IDs. `app/view_policy.c` keeps the physical view dimensions,
density and logical point bounds separate from the diagnostic render buffer; the
draw path no longer forces 256x256 window geometry, so a later redraw cannot
turn a 1440x900 physical input view into a 256x256 input domain.

A `timerfd` is attached to the same looper so `dm_android_touch_tick` advances
timers (ZL lead, deferred pulses, settle and repeat) even when no further touch
arrives. Timer ticks and MotionEvents are accepted only while
`app/input_policy.c` reports resumed, focused, input-queue, window and bridge
readiness together. A timer start failure, fatal read error or hangup is part of
that acceptance decision: input is gated, touch state is neutralized, and the
failure is logged until a later callback deliberately retries the timer. Pause,
focus loss, queue replacement, window loss and destruction stop the timer, cancel
active pointers and queued pulses, then consume a neutral snapshot exactly once.
A replacing input queue cancels and destroys the old bridge before attaching the
new queue. Resume/focus are not assumed at process creation; they are accepted
only after the corresponding NativeActivity callbacks observe them. Duplicate/stale
input queue and timer callbacks are ignored or cleaned up without stale pointer use.
Timer error/hangup and read failures stop the timer, cancel touch state and log;
there is no synthetic input event used as a tick fallback.

The bridge is the explicit diagnostic consumer: it drains at most one ordered
pulse per read and stores a bounded snapshot for log-throttled observation. Its
`diagnostic_reads` counter means this bridge consumed a snapshot, not that a
guest/game HID layer received it. The bridge drains one pulse and retries a
rejected `UP`; if the release still cannot be queued it falls back to
`cancel_all`, so a full 16-pulse ring cannot leave a pointer stuck. `CANCEL`
works even when the platform frame has no usable pointers/coordinates.

The current app remains an owned diagnostic: the consumed snapshot is only
logged, and no game-state/HID/HUD consumer is attached. Real guest HID and the
game-specific gesture/acceptance path remain future work in #32/#33–40.

## Build

```sh
PATH=/opt/homebrew/bin:$PATH bash dm/android/app/build.sh
```

Requires NDK r29, CMake, host glslc, a JDK/keytool, zip and SDK platform/build-tools 35. Defaults remain on T2 via `paths.sh`; override `ANDROID_SDK_ROOT` if needed. The build uses aapt2, zipalign and apksigner directly, without Gradle. The SDK packages were downloaded from Google's repository, with checksums verified against `repository2-1.xml`:

| Package | SHA-1 from repository metadata |
|---|---|
| `platform-35_r02.zip` | `0bb560a90a7a2cbd0dd8348224d518b638fe7949` |
| `build-tools_r35_macosx.zip` | `93ab8ce91230e067b5add4bfa79919c52b27f072` |

Installed under `../d2r-work/tools/android/sdk`; package archives/metadata remain under `tools/android/sdk-packages`. SDK notices/licenses remain with the packages. Label text comes from `dmAndroidDiagnosticName` in the shared i18n table.

Output: `../d2r-work/build/android/apk-diagnostic/d2r-diagnostic-debug.apk`. A local debug signing key lives in `../d2r-work/keys/android-diagnostic-debug.p12`; never commit keys or generated APKs. The binary is stored uncompressed and aligned to 16 KiB. The build verifies its APK signature, ZIP alignment, package metadata and ARM64 ABI. It does not install or launch automatically.

## Phone checkpoint, 2026-10-17

After pairing through Android wireless debugging, installation and log inspection can be performed with (USB debugging is also supported):

```sh
adb install -r ../d2r-work/build/android/apk-diagnostic/d2r-diagnostic-debug.apk
adb shell am start -W -n org.d2rmobile.android.diagnostic/android.app.NativeActivity
adb logcat -d -s D2R-Diagnostic
```

For multiple devices use `adb -s SERIAL` consistently. The Xiaomi 25010PN30G / SM8750, Android 16 API 36, 4096-byte pages, successfully installed and launched the diagnostic APK. Its actual NativeActivity process logged `synthetic APK-process ARM64 gate smoke=PASS`, `256x256 pixel-check=PASS` and `CPU-readback window post=PASS`. Launching the home activity and returning produced paused/resumed logs. Evidence: `../d2r-work/build/dump-inspection/device-diagnostic.log`. An injected Home key was denied by the phone; the activity intent was used instead, without changing security settings.

These results cover owned synthetic fixtures in the app process. Game code, full lifecycle/low-memory stress, screenshot/accessibility QA and game compatibility remain unverified. Later source changes, including SVC monitor clearing, have host/Android build coverage but have not been reinstalled or tested on the phone. No visual QA PASS is claimed.

The surface is the existing deterministic graphics-test pattern, without product typography, controls or brand layout. Web/React design research is outside this native engineering harness. A real game UI and its visual review remain separate work.
