# Android development notes

中文：[ANDROID_COLLABORATION.zh-CN.md](ANDROID_COLLABORATION.zh-CN.md)

## Contributing

Read the [build guide](../dm/android/README.md) and [contribution rules](../CONTRIBUTING.md). Prepare resources from your own game files; keep game content and generated files out of the repository.

Create a development branch from `dev` and open a PR when ready. Describe the change, build version, device, actual test results and unverified behavior. Run the affected platform checks when changing the shared host or graphics code.

## Source layout

- `dm/android/app/`: Java Activity, fullscreen Surface, floating touch controls, system input dialogs, widgets, and Gradle.
- `dm/android/jni_android.c`, `text_input_jni.c`, `lifecycle_dispatch.h`: startup, dedicated host input thread, and lifecycle dispatch.
- `dm/android/vulkan_present.*`: Android window and swapchain presentation.
- `dm/gfx/vulkan/`: NVN adaptation and Vulkan replay; `engine/xbg_game.cpp` is the main entry point for current graphics performance and cache investigation.
- `dm/host/`, `dm/i18n/`: shared host and bilingual UI table; preserve compatibility with other platforms.
- `dm/android/tests/`, `dm/gfx/vulkan/engine/tests/`: independent Android and Vulkan regression checks.

## Measured status (2026-10-16, China Standard Time)

Development build `0.1.7-dev / code8` was installed at 08:56 on vivo X200 Ultra (V2454A, Android 16/API36, Adreno 830). APK SHA256: `381514cce18027320cb6b0a5a72a83beb2cfefcbedb9bc3a9d0fb7f089cf62d0`. This is a development baseline, not a finished release.

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
