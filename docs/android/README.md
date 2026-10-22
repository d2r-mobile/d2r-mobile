# D2R Mobile — Android

中文：[README.zh-CN.md](README.zh-CN.md)

An experimental Android port of [the project-ops/d2r-mobile](https://github.com/the project-ops/d2r-mobile), bringing offline **Diablo II: Resurrected** and its game-specific touch interface to Android ARM64. The current test device is a **Xiaomi 15 Ultra / Snapdragon 8 Elite / Adreno 830**.

**The development build now runs the game on the target phone.** Startup, character selection, entering camp and normal save/exit have been tested. The user has also verified combat, skills, pickup, inventory dragging and stash dragging. Full device compatibility, complete iOS touch parity and sustained 60 fps remain work in progress.

> **`main` is this fork's accepted Android development baseline.** The former Android integration is consolidated through PR. Use short-lived topic branches for changes; fetch the original project as `upstream/main` and review applicable updates before merging them into our `main`.

## Current status

Updated **2026-10-22**. Follow the project board for current work and acceptance boundaries.

| Area | Verified progress | Remaining work |
|---|---|---|
| Game startup and saves | Real game startup, character selection, camp and normal save/exit | Full game progression, failure recovery and lifecycle matrix |
| Rendering | Actual NVN-to-Vulkan game rendering; user reports normal character-selection and camp images with Turnip | Other scenes, resource lifetime and graphics conformance |
| Touch | Original D2R touch core connected; movement/release, attack, skills, pickup, inventory and stash dragging tested; MAP moved to the top-left with the original top-right map title still usable | UI/art finishing, complete shop-drag icon verification, remaining gestures, IME and physical-controller transitions |
| Launcher and drivers | Imported-data status, FPS/power display toggle, driver selection, custom ZIP import/delete and incompatible-driver rejection tested | Broader device/driver compatibility and release packaging |
| Performance | Reduced repeated buffer allocations, upload commands, range queries and inactive diagnostic work | CPU translation/command encoding, binding and copy costs; sustained frame times and energy measurement |

**Turnip is the rendering baseline.** The system Qualcomm driver remains available as a fallback, but its earlier black patches and lighting defects were not resolved by the experimental UBO workaround. This project does not currently prioritize that driver-specific repair.

Android skill buttons use transparent silver silhouettes following the upstream iOS mask. Normal and pressed frames are converted once and cached. The bound arrow and fist icons, pressed state and empty slots have been visually checked on the test phone; other class skills still need individual verification.

Android audio can reopen a failed or disconnected AAudio stream on a later start/resume, while retaining the single-consumer fallback. Production-code failure regressions pass; phone call/focus, headset/Bluetooth changes and audible recovery still require device checks. Immediate recovery from a route change without a resume is not established.

Performance varies by scene and over time. Reaching about 60 fps in some camp samples is not evidence of stable 60 fps in combat or a long session. Energy savings have not been established. Apple performance measurements apply to the Apple build.

The performance overlay reports a rolling two-second presented FPS and P95/worst presentation interval when the driver exposes display timing. Without that data it explicitly reports submitted FPS instead. For uniform 60/30 fps, intervals should stay near 16.7/33.3 ms; an average FPS alone cannot establish smoothness. The limiter offers Uncapped, 60 and 30; a cap cannot speed up a frame that already exceeds its time budget. Both caps request refresh-aligned present times (`debug.d2r.display_pacing=0` disables this), reducing 8.3 ms catch-up intervals after late frames on a 120 Hz panel.

The launcher's Debug collection switch is off by default and applies on the next game launch. FPS display is independent: with collection off it only shows FPS and presentation intervals. Detailed timing, battery/memory sampling, slow-frame files, optional captures and input traces require collection; fatal errors and gameplay synchronization remain active. With collection on, abnormal-frame logging uses a cap-aware threshold and normally samples at most once per second; per-draw timing still requires its advanced diagnostic flag. Disabling diagnostics does not establish sustained 60 fps.

The accepted CPU changes combine single-pass descriptor encoding/hash, reuse of identical compute bindings, and a shared sampler/texture lookup lock. Local checks and short stationary camp runs pass. In the measured sequence the combined60fps candidate averaged56.2FPS versus54.2/50.6FPS for baseline runs, but P95 remained25ms and temperature/order differed; stable60 and repeatable power savings remain unproven. The30fps candidate averaged30.0FPS with P95 33.3ms.

Fullscreen Medium and Low keep the UI/output at720 high and render internal3D at640 and480 high respectively. On a1600×720 output, Medium's nominal3D pixel count is about99% of1280×720; similar pixel counts do not guarantee equal frame times.

ADPF now receives paired translation-thread frame work and the actual capped frame budget; unavailable APIs fall back normally, and uncapped work retires any previous capped session. Vulkan keeps the conservative three-slot/256-draw defaults and the 32 MiB temporary-memory limit. A short six-slot camp trial reduced ring waits but did not establish higher FPS; eight-slot and larger-draw-limit comparisons remain pending. This is not a stable-60-FPS or power-saving claim.

Merged CPU work includes the thread-context cache collision fix, small guest reads under the existing heap permission lease with safe-read fallback, and coalesced host-write unprotection. The latter preserves exact page coverage and generations; an earlier six-minute camp run reduced protection calls from about 300 to 28–36 per frame and cumulative protection time from 1.25–1.35 to 0.42–0.50 ms/frame. These are work-reduction measurements, not proof of higher FPS or lower power. The user also confirmed that camp-boundary movement interruption and the transient question-mark skill icons no longer reproduce. Lock spinning remains disabled. Attachment-clear folding and narrower image usage (#331) are enabled by default following the user's enabled-path device test without observed problems. Set `debug.d2r.attachment_clear=0` or `debug.d2r.narrow_image=0` before restarting to disable either path. FPS and energy gains have not been established.

Read diagnostic values within their scope: CPU time covers NVN command translation, GPU time covers measured command batches, and snapshot MiB/frame counts guest-memory snapshot reads. They are not whole-frame CPU time, complete GPU time or total GPU bandwidth. The watts estimate uses battery current and voltage; while charging, it cannot establish game or whole-device power consumption. Compare repeated runs under matching scene, temperature and charging conditions.

## Trying a development build

Build a development APK from `main` or `dev`. This is a device-tested development port, not a general compatibility or stable-release claim.

1. Install the APK and open the launcher. To retain existing app data, use a compatible update with the same package name and signing key.
2. Choose **Import data package** and select your locally prepared Android game-data ZIP. Wait for the installed/ready state.
3. On the supported Qualcomm test phone, use **bundled Turnip**. A custom driver ZIP may be imported; incompatible drivers are rejected before game startup. Imported drivers can be deleted from the launcher. There is currently one custom-driver slot; a successful new import replaces it.
4. Start with **Fullscreen · 720 high**. The selector shows actual output pixels (1600×720 on a 20:9 phone), with lower-cost modes that keep the UI/output at 720 high and render 3D at 480 or 640 high, a 900-high fullscreen mode, and 1280×720/1600×900 16:9 alternatives. The 480 mode has been visually confirmed on the test phone; it trades 3D sharpness for fewer rendered pixels, but repeatable power savings remain unproven. Output no longer depends on a separate handheld/docked selector. Save import/export is on the immersive launcher, below Start; settings and game-package management scroll on the right in wide layouts.
5. Start the game. When finished, use the game's save/exit and the application's normal exit flow.

**What does importing the ZIP do?** The launcher extracts and validates its contents into the app's own data directories: resources use app-specific storage, while game modules and saves use internal app storage. The ZIP is an import source; the game does not read it on every launch. Once a successful import and launch are confirmed, the copy in Downloads can be removed. Keep a separate backup if you may need to reinstall. Clearing app data or uninstalling can remove imported data and saves; export saves first. Game data is not added to the already installed APK.

Only a prepared Android package is accepted by this import flow; an arbitrary Switch archive is not a ready-to-import Android package. Use your own legally owned Switch **1.0.34.0 with Reign of the Warlock** (repository data version **3.3.94012**). See [game-file preparation](../GAME_FILES.md) for source-file/version requirements; its Apple deployment steps differ from the Android launcher. No game files, keys or saves are supplied here.

## Known limitations and priorities

- The user has verified uninterrupted movement and removal of the brief skill question marks across camp transitions. Broader area/device coverage remains part of .
- The complete iOS touch presentation is still being adapted. Remaining map-goal/rift HUD art and haptic integration, shop icon appearance and full interaction/device coverage stay open; working combat and dragging do not close all touch tasks.
- Sustained performance is the current priority: profile the CPU translation path and improve measured hotspots while preserving GPU synchronization, resource ownership and validation. ADPF frame wiring is present, but successful API calls have not established a repeatable FPS or power benefit.
- CPU-only pages coalesce protected-page faults within a bounded 64 KiB window on Android; GPU-written pages remain exact and stop coalescing. Shader lookup reuses the existing byte-key hash table. Bounded guest-lock spinning is available for measurement (`debug.d2r.lock_spin=20` enables 20 microseconds on the next launch), but remains off by default because current device comparisons have not established a repeatable gain. Turning debug collection off also removes dirty-protection and main-yield timing. Sustained combat and battery savings need separate measurements.
- Background/resume, keyboard, controllers, audio, save recovery and a complete playthrough require broader acceptance. Historical diagnostic or contract tasks archived during cleanup are not claimed as proven by the real-game first run.

## Development

```sh
git clone --branch main https://github.com/d2r-mobile/d2r-mobile.git
cd d2r-mobile

# Game-free host tests and Android ARM64 cross-compilation.
export DM_ROOT=/path/to/external-disk/d2r-work
./dm/android/test.sh -DDM_ANDROID_VULKAN=OFF
./dm/android/build.sh -DDM_ANDROID_VULKAN=OFF

# Inspect the separate playable-APK build entry and its options.
python3 dm/android/tools/build.py --help
```

The game-free tools use CMake 3.22+, a C compiler and Capstone 5; native ARM64 cases need an ARM64 host. Android cross-compilation requires the NDK (r29 is used in current builds). First configuration may download pinned dependencies. See the [Android developer notes](../../dm/android/README.md) for tool setup and historical fixtures.

The playable-APK entry is `dm/android/tools/build.py` with `--game-run`. It needs the reference toolchain, SDK/build tools, JDK, shader tooling and locally prepared resources; its `--turnip` and `--touch-art` inputs must be supplied as appropriate. A generic APK can import game modules on the phone. Do not publish builds embedding game code or derived game artwork. `dm/android/app/build.sh` builds the older synthetic diagnostic APK; `setup.sh` is the upstream Apple preparation flow.

Start contributions from , agree on a bounded issue and submit a topic PR to `main`. Report the exact source commit, tests and actual device conditions. Keep unsupported paths explicit, preserve scanning/W^X/ownership/failure checks, and separate implementation from device acceptance. Keep game content, credentials, saves and raw device logs out of GitHub. See [CONTRIBUTING.md](../../CONTRIBUTING.md).

At each task closeout, the coordinator reviews the exact PR head and required evidence, closes only issues whose acceptance scope is satisfied, and merges only accepted changes. Remove topic branches only after confirming their work is preserved and no active worktree or worker needs them; keep pending candidates and unfinished PRs. Update both English and Chinese READMEs when behavior, limitations or verified progress changes, distinguishing merged source from local device candidates.

## Credits and license

This Android port is MIT-licensed. Third-party notices remain with their code.

Upstream [architecture](../../dm/README.md), [graphics](../../dm/gfx/README.md), [iOS](../../dm/ios/README.md) and [verification](../../dm/verify/README.md) guides remain useful platform-specific references. See the [dependency/license inventory](../DEPENDENCIES.md), [derived interface information](../DERIVED.md) and [MIT license](../../LICENSE), copyright (c) 2026 jhaizhou. Third-party notices remain with their code.

Diablo and Blizzard Entertainment are Blizzard Entertainment trademarks; Nintendo Switch is a Nintendo trademark. This independent, non-commercial project is unaffiliated with and not endorsed or sponsored by those companies.
