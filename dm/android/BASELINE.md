# Android development baseline

Target: Xiaomi 15 Ultra / Snapdragon 8 Elite, Android ARM64.
Task board: .

**This is a development baseline, not a playable game or a completed SDK port.**
The production scanner still rejects unsupported SDK synchronization paths.
The diagnostic quarantine is separate from production admission. Do not relax
either boundary to turn an unsupported input into an apparent success.

## What was checked for this snapshot

On 2026-10-18, an independent source copy based on upstream commit
`source snapshot` plus the Android changes passed:

| Check | Result | Scope |
|---|---|---|
| Fresh host build, ASAN/UBSAN | Passed | Apple silicon macOS, Clang, Capstone 5, Vulkan/glslc |
| CTest | 44/44 passed | Owned CPU, runtime, IPC, decoding and Vulkan samples |
| Fresh Android build | Passed | NDK r29, arm64-v8a, API 29, flexible page-size support |
| Android shared-library checks | Passed | Local bridge veneers and unresolved-import traps |

The builds used the dependency versions and digests in `dependencies.cmake`.
Their source caches were reused; build outputs were outside the source tree.
No game, SDK or original rtld instructions were executed for these checks.
No APK was installed and no current-device acceptance was performed.

## Reproduce

Host requirements include CMake, a C compiler, Capstone 5, a Vulkan loader and
headers, and `glslc`. Use `CMAKE_PREFIX_PATH` if dependencies need a search prefix.
The host execution results above apply to Apple silicon macOS; other hosts need
their own verification. From the repository root:

```sh
./dm/android/test.sh
./dm/android/build.sh
```

Set `DM_ROOT` to an external workspace and `ANDROID_NDK_HOME` to an installed NDK
when necessary. The scripts default to a sibling `d2r-work` directory. Tests use
owned inputs and do not require game files. `-DDM_ANDROID_VULKAN=OFF` disables GPU
checks when Vulkan tooling is unavailable; such a run does not verify graphics.

## Remaining gates

- SDK synchronization, TLR/preemption and SVC36 adaptation are not admitted.
  See ,
   and
  . The proposed contract and
  counterexamples (design only, unverified on Android) are in
  [`docs/android/runtime-contract.md`](../../docs/android/runtime-contract.md).
- The `lm` backend retains at most four copied packets per host. It is not a
  complete logging sink; unsupported commands still fail.
- Complete SDK initialization, required services, normal SDK teardown and
  Android acceptance remain open. Historical diagnostic prefixes and older
  synthetic phone results in the README do not cover the current full port.
- Vulkan samples, RGBA8 textures and input snapshots are foundations, not the
  game's renderer, audio, save system or playable app.

Game files, keys, signing credentials, generated artifacts and private diagnostic
logs are excluded from this snapshot. The task board remains open until each
task's own review and acceptance criteria are met.

## 中文摘要

这是可复现的 Android 开发基线，不是可玩版本，第一块尚未完成。此次从独立源码副本
完成宿主 ASAN/UBSAN 44/44 自有测试和 Android ARM64 全目标编译；没有执行真实
游戏／SDK／原始 rtld，也没有安装或验收手机 APK。旧版诊断和合成图案不能代表当前
完整移植通过。同步／抢占准入、完整初始化与退出、日志消费及图形／音频／存档等
仍按 GitHub 清单逐项推进和验收。
