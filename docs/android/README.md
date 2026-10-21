# D2R Mobile — Android

中文：[README.zh-CN.md](README.zh-CN.md)

An experimental Android port of Android implementation, bringing offline **Diablo II: Resurrected** and its game-specific touch interface to Android ARM64. The current test device is a **Xiaomi 15 Ultra / Snapdragon 8 Elite / Adreno 830**.

**The development build now runs the game on the target phone.** Startup, character selection, entering camp and normal save/exit have been tested. The user has also verified combat, skills, pickup, inventory dragging and stash dragging. Full device compatibility, complete iOS touch parity and sustained 60 fps remain work in progress.

Android code is included on both `main` and `dev` in this repository.

## Current status

Updated **2026-10-21**, including device tests and user feedback through October 21. Follow the project board for current work and acceptance boundaries.

| Area | Verified progress | Remaining work |
|---|---|---|
| Game startup and saves | Real game startup, character selection, camp and normal save/exit | Full game progression, failure recovery and lifecycle matrix () |
| Rendering | Actual NVN-to-Vulkan game rendering; user reports normal character-selection and camp images with Turnip | Other scenes, resource lifetime and graphics conformance (, ) |
| Touch | Original D2R touch core connected; movement/release, attack, skills, pickup, inventory and stash dragging tested; MAP moved to the top-left with the original top-right map title still usable | UI/art finishing, complete shop-drag icon verification, remaining gestures, IME and physical-controller transitions (, ) |
| Launcher and drivers | Imported-data status, FPS/power display toggle, driver selection, custom ZIP import/delete and incompatible-driver rejection tested | Broader device/driver compatibility and release packaging () |
| Performance | Reduced repeated buffer allocations, upload commands, range queries and inactive diagnostic work | CPU translation/command encoding, binding and copy costs; sustained frame times and energy measurement () |

**Turnip is the rendering baseline.** The system Qualcomm driver remains available as a fallback, but its earlier black patches and lighting defects were not resolved by the experimental UBO workaround. This project does not currently prioritize that driver-specific repair.

Performance varies by scene and over time. Reaching about 60 fps in some camp samples is not evidence of stable 60 fps in combat or a long session. Energy savings have not been established. Upstream iOS performance results describe the Apple build, not this Android port.

**Installed local candidates can be ahead of the published integration branch.** The redesigned launcher, 30/60 fps limit selection and virtual-controller on/off switch have passed bounded phone tests in a local candidate; these changes have not yet been merged. The user also reports uninterrupted movement across camp boundaries with that candidate. This does not close the remaining touch or controller acceptance work.

Recent stationary-camp comparisons with the 60 fps limit averaged about 51–56 fps. Lazy query reset and skipping a full-cover presentation clear did not show a repeatable FPS improvement; both experiments remain off by default and unmerged. Research delivery  and the upstream progress discussion inform further work under .

Read diagnostic values within their scope: CPU time covers NVN command translation, GPU time covers measured command batches, and snapshot MiB/frame counts guest-memory snapshot reads. They are not whole-frame CPU time, complete GPU time or total GPU bandwidth. The watts estimate uses battery current and voltage; while charging, it cannot establish game or whole-device power consumption. Compare repeated runs under matching scene, temperature and charging conditions.

## Trying a development build

Use an engineering APK built from the integration branch. This is a device-tested development port, not a general compatibility or stable-release claim.

1. Install the APK and open the launcher. To retain existing app data, use a compatible update with the same package name and signing key.
2. Choose **Import data package** and select your locally prepared Android game-data ZIP. Wait for the installed/ready state.
3. On the supported Qualcomm test phone, use **bundled Turnip**. A custom driver ZIP may be imported; incompatible drivers are rejected before game startup. Imported drivers can be deleted from the launcher. There is currently one custom-driver slot; a successful new import replaces it.
4. Start with **Phone aspect / Auto**. This keeps the game at the phone's aspect ratio without requiring native-display rendering resolution. Compare quality and frame time before increasing resolution.
5. Start the game. When finished, use the game's save/exit and the application's normal exit flow.

**What does importing the ZIP do?** The launcher extracts and validates its contents into the app's own data directories: resources use app-specific storage, while game modules and saves use internal app storage. The ZIP is an import source; the game does not read it on every launch. Once a successful import and launch are confirmed, the copy in Downloads can be removed. Keep a separate backup if you may need to reinstall. Clearing app data or uninstalling can remove imported data and saves; export saves first. Game data is not added to the already installed APK.

Only a prepared Android package is accepted by this import flow; an arbitrary Switch archive is not a ready-to-import Android package. Use your own legally owned Switch **1.0.34.0 with Reign of the Warlock** (repository data version **3.3.94012**). See [game-file preparation](../../docs/GAME_FILES.md) for source-file/version requirements; its Apple deployment steps differ from the Android launcher. No game files, keys or saves are supplied here.

## Known limitations and priorities

- The user has verified the camp-transition movement fix in a local candidate; integration and broader area-transition checks remain open (). Skill buttons can still briefly flash question marks during transitions ().
- The complete iOS touch presentation is still being adapted. Remaining map-goal/rift HUD art and haptic integration, shop icon appearance and full interaction/device coverage stay open; working combat and dragging do not close all touch tasks.
- Sustained performance is the current priority: profile the CPU translation path and improve measured hotspots while preserving GPU synchronization, resource ownership and validation. A working ADPF API experiment did not demonstrate a repeatable FPS improvement and was not adopted.
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

Upstream [architecture](../../dm/README.md), [graphics](../../dm/gfx/README.md), [iOS](../../dm/ios/README.md) and [verification](../../dm/verify/README.md) guides remain useful platform-specific references. See the [dependency/license inventory](../../docs/DEPENDENCIES.md), [derived interface information](../../docs/DERIVED.md) and [MIT license](../../LICENSE), copyright (c) 2026 jhaizhou. Third-party notices remain with their code.

Diablo and Blizzard Entertainment are Blizzard Entertainment trademarks; Nintendo Switch is a Nintendo trademark. This independent, non-commercial project is unaffiliated with and not endorsed or sponsored by those companies.
