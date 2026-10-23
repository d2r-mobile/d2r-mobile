# Android team collaboration

中文：[ANDROID_COLLABORATION.zh-CN.md](ANDROID_COLLABORATION.zh-CN.md)

## Repository responsibilities

| Repository | Ownership and purpose |
| --- | --- |
| Android implementation | Our private Android development repository and daily collaboration entry point |
| Android implementation | Our private HarmonyOS development repository, with separate ownership and access |
| [d2r-mobile/d2r-mobile](https://github.com/d2r-mobile/d2r-mobile) | The upstream project; we contribute Android and HarmonyOS changes through PRs reviewed and merged by its maintainers |

This repository preserves d2r-mobile history and its MIT license. The integrated upstream baseline is `source snapshot`. The older HarmonyOS repository derives from xbl-ios; its production sources remain separate and are not overwritten by automatic synchronization.

The existing public fork `the project/d2r-mobile` is an upstream PR transport, not another product development line. This private repository is not a GitHub fork. Put reviewed commits on a topic branch in that fork when contributing upstream; do not mirror the private repository or push directly to upstream main. HarmonyOS draft PR is open and unmerged. There is no Android upstream PR yet.

## Getting started

1. An administrator invites your GitHub account under Settings → Collaborators. Accept the invitation and clone this repository. Use your own clone or worktree.
2. Read this page, the [Android build guide](../dm/android/README.md), and [upstream contribution rules](../CONTRIBUTING.md). Prepare game programs, resources, shaders, and generated game data privately on your own machine.
3. Use `main` as the integration baseline and `android/<topic>` branches. Before editing, identify the owner and file scope in a repository Issue. Avoid concurrent edits to the same files; coordinate native builds and device access serially.
4. Open a PR in this repository with the source commit, build version, device, actual test time, results, and unverified scope. Development commits may contain explicitly identified unfinished features; they do not establish playability acceptance.
5. Prepare independently validated fixes as upstream PRs, following bilingual comments, localization, resource exclusions, and regression requirements. Compare upstream updates before merging; do not reset/clean or force-push over development work.

Recommended remotes: `origin` for this repository, read-only `upstream` for d2r-mobile/d2r-mobile, and `fork` for the public contribution fork. Set `remote.pushDefault=origin` and an unusable upstream push URL to prevent accidental upstream pushes.

## Source layout

- `dm/android/app/`: Java Activity, fullscreen Surface, floating touch controls, system input dialogs, widgets, and Gradle.
- `dm/android/jni_android.c`, `text_input_jni.c`, `lifecycle_dispatch.h`: startup, dedicated host input thread, and lifecycle dispatch.
- `dm/android/vulkan_present.*`: Android window and swapchain presentation.
- `dm/gfx/vulkan/`: NVN adaptation and Vulkan replay; `engine/xbg_game.cpp` is the main entry point for current graphics performance and cache investigation.
- `dm/host/`, `dm/i18n/`: shared host and bilingual UI table; preserve upstream platform compatibility.
- `dm/android/tests/`, `dm/gfx/vulkan/engine/tests/`: independent Android and Vulkan regression checks.

## Measured status (2026-10-16, China Standard Time)

Development build `0.1.7-dev / code8` was installed at 08:56 on vivo X200 Ultra (V2454A, Android 16/API36, Adreno 830). APK SHA256: `381514cce18027320cb6b0a5a72a83beb2cfefcbedb9bc3a9d0fb7f089cf62d0`. Corresponding source source snapshot`. This is a development baseline, not a finished release.

| Area | Actual result |
| --- | --- |
| Build | Current-source assembleDebug and lintDebug succeeded; localization, literal, native-widget protection, and mode registration scans passed. Apple/Metal/iOS regressions were not run on Windows |
| Install and saves | code7 and code8 were overlay-installed using automated clicks on the matching vivo app/version confirmation UI. All 42 save/save.work file hashes matched before and after code8 installation |
| Input and lifecycle | Two accepted name prompts passed in code5. Name cancellation, background cancellation during naming, and same-process resume passed in code6. Full input and long-duration stability acceptance remains incomplete |
| Flicker and black blocks | code7 cross-submit descriptor memo on→off→on reproduced the fault. code8 excludes texel-buffer bindings from memo, preventing reuse of VkBufferView objects destroyed at slot retirement, while retaining other cross-submit memo entries. Character selection, class preview, camp, and two short movement captures showed no recurrence of the large black blocks. Longer outdoor/combat validation is pending |
| FPS | Current code8 camp measurements remain 9.14–10.48 FPS (960 frames at the same workload). Recent fixed-scene GPU time was 45.66–58.85 ms/frame and presentation 1.27–1.57 ms. No stable FPS improvement is established; the system overlay's 60 is not game FPS |
| Widgets | 2×2/4×2 sources compile, providers are registered, and 27 synthetic parsing tests passed. Real launcher display/refresh and vivo platform integration remain unverified |

Remaining priorities: GPU main-pass cost, outdoor FPS and sustained graphics regression; complete NPC/combat/skills/inventory interaction; repeated lifecycle and cold-start save recovery; iOS touch details and a general import UI. Two shader SSA candidates did not establish stable gains in an original→candidate→original comparison; original shaders remain on the device.

Update 10-22: general import is done -- the release APK contains no game content; players make an import pack on a computer with `dm/tools/d2rm_android.py` from their own game files, and the App imports it with a progress screen when opened. The shader export step (`dm/gfx/tools/dmsh_vk.py`) makes all 6,927 game shader bundles byte-for-byte identical to the tested set. Touch drawing and panel states now match iOS (skill icons, button sockets, joystick, no virtual pad buttons over panels). Remaining iOS touch details: the empty-skill-slot menu, the stat-point overlay and the item image that follows the finger in a shop.

Raw logs, screenshots, private resources, save backups, and APKs remain in the maintainer's local `artifacts/android-20261016` evidence directory and are not uploaded with source. Reviewed evidence summaries can be committed; access to raw material is managed separately. Automated follow-up remains paused, and old agents are not automatically resumed. Preserve device data and saves, and retain signing identity for upgrades.
