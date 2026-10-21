# Android diagnostic APK

中文：[README.zh-CN.md](README.zh-CN.md)

This NativeActivity development app bundles **only owned synthetic fixtures**. It is not a playable D2R build. No game data or Switch keys enter the APK. Package: `org.d2rmobile.android.diagnostic`; ARM64 only, minimum API 29, target API 35. No network or shared-storage permission is requested.

At startup it reads a synthetic artifact, rebinds gates, maps RX/R/RW pages and attempts a native guest-TLS round trip in the actual app process. Log tag: `D2R-Diagnostic`. When a window exists it draws the tested Vulkan fragment-coordinate pattern, reads it back and copies it into an Android native window. This is a one-frame engineering diagnostic, not an optimized game presenter. Pause/resume are logged; low memory releases the GPU context, and redraw recreates it. No background rendering loop, game resource importer, saves, game controls or audio is implemented here.

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

## Input delivery gap (issue #32)

The current input looper handles only the Android Back key. It does not deliver
`MotionEvent` touches to the r2d core. The next input slice must create one
`dm_android_pad` and one `dm_android_touch` on the same input/core thread, map
`MotionEvent` pointer IDs as opaque IDs, stamp non-negative `CLOCK_MONOTONIC`
nanoseconds, and convert physical pixels to logical points using the view
density before calling `dm_android_touch_down/move/up/cancel`. The consumer must
call `dm_android_touch_read` (never `dm_android_pad_read`) for the ordered
queued-pulse view.

The adapter's 16-pulse ring is bounded. If a mutating event is rejected because
the ring is full, the app must drain reads on the input thread and retry; it must
not silently ignore the event, and `ACTION_UP`/`ACTION_CANCEL` must be resolved
on that thread even when the ring is full (fall back to `cancel_all` if a release
cannot be queued). This wiring is not part of the lifecycle slice and remains
open in #32.

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
