# Linux build host and first findings (2026-10-16)

中文：[2026-10-16-linux-build.zh-CN.md](2026-10-16-linux-build.zh-CN.md)

Notes from a second contributor setting up from `main` at `recorded build` on Windows 11 + WSL2 (Ubuntu 18.04) with Docker. Branch experimental build. Times are NZDT (UTC+13). No device has run this build yet. Everything below is a build or reading result, not a playability result.

## Building on Linux / WSL with Docker

`dm/android/docker/` holds a pinned build image and a runner script. With them the APK builds without Windows-side Python, NDK or CMake:

```sh
docker build -t dm-android-build:1 dm/android/docker
DM_GEN_DIR=/abs/path/to/generated/headers dm/android/docker/build.sh
```

- The image pins Ubuntu 24.04 by digest, apt packages by the 2026-10-10 snapshot, JDK 17, command-line tools 16111833 (SHA-1 from Google's repository listing), `platforms;android-36`, `build-tools;36.0.0`, `ndk;29.0.14206865`, `cmake;3.31.6`, and Gradle 9.8.1 (SHA-256). It is about 3.6 GB.
- The container runs as the host user. Gradle caches go to a named volume. The debug signing key stays in a host directory (`~/.android-dm`). On first use the script copies Windows Android Studio's `debug.keystore` when it exists, so APKs from both paths upgrade each other.
- sdkmanager unpacks executables as mode 744 (root only), so the image adds `chmod -R a+rX` in the same layer. Without it, the first build failed with "Cannot run program …/cmake: Permission denied".

**Inputs prepared on Linux** (in a container with Python 3.12, Pillow 12.1.1, lz4 and capstone 5.0.x), from a `git archive` of `recorded build` so the checkout stays clean:

| Step | Time | Result |
|---|---|---|
| `make -C dm/host dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc` | 2 min 54 s | 46,551 / 1,250 / 8,010 bytes |
| `DM_TARGET=android DM_RULESET=touch-run-v4 patch_nso.py` on `main`, `sdk`, `subsdk0` | 2 min 21 s | codename `3.3.94012-r44`, every `fixups.tsv` has `# TARGET 0x1` |

Rewriter counts, so other hosts can compare runs from the same original ExeFS:

| Module | slot_refs | gates | cuts | obs | patched.bin SHA-256 |
|---|---:|---:|---:|---:|---|
| main | 25,127 | 36 | 48 | 21 | `27c5c83d29096d5eb365f4067e7c0e702bacf201fc42f04ec48166330949c49a` |
| sdk | 12,085 | 110 | 0 | 0 | `9b33cf9850ce5bc648cd0932ddb79103ecc50b98350bf42f286a63a242d99196` |
| subsdk0 | 10,486 | 0 | 0 | 0 | `ecc9d697f568ce16500c1b41bdabfce621e70610fad49ab0084bd0b4185d5f3a` |

`main` logged that the startup prefetch list is absent (expected for the public tree).

**Build result (15:07):** `:app:assembleDebug :app:lintDebug` with those tables: BUILD SUCCESSFUL. A cold build took about 3.5 min, after a 2 min 47 s image build.
- APK 20,468,833 bytes, SHA-256 `f359339fa2efd92133b74f50b472c39ce4144bf259ab3d2b489e7a20d11954e3`, signed with this host's debug key. `lib/arm64-v8a/libdm.so` is 16,659,904 bytes.
- Lint: 0 errors, 38 warnings, mostly widget layouts (`SmallSp` 16, `RtlSymmetry` 4).
- C/C++: 128 warnings. 111 are `%llx`/`%llu` for `uint64_t`, which has the same width on arm64.
- One warning is a real log-only defect: `loader.c:240` prints `pthread_key_t` with `%lu`. That type is `int` on bionic, so the upper 32 bits of the four printed keys are undefined.

**Device-free checks:**
- `dm/android/tests/lifecycle_dispatch_test.c` passes with gcc 7.5 on x86-64 Linux.
- `TextInputRulesTest` and `SaveSummaryReaderTest` (27 synthetic checks) pass with the image's JDK 17.

## Findings

1. **This repository cannot produce the `.xgs` shader bundles.** The engine loads `<key>.xgs` from the shader directory (`xbg_game.cpp:1110-1125`). The format is an `xbg_shader_header` (magic `XGS1`, version 1, stage, bindings, inputs, c1 bytes, SPIR-V; `xbg_game.h:81-87`). No script, tool or document in the tree writes it, and `dm/android/README.md` only says "external `.xgs` bundles matching the game and Vulkan backend". A new contributor can build the APK but cannot provision a device from this repository alone. Question for the team: where is the producer (the HarmonyOS tooling?), and can it, or its format specification, be committed here?
2. **The rewrite verifier does not cover `DM_TARGET=android`.** `dm/verify/structck.py` (checks ⑥ and ⑧) and `equiv.py:84` expect the Apple slot sequence (`mrs tpidrro_el0` plus the key page). Android output uses `mrs tpidr_el0; ldur [x,#-8]` (`patch_nso.py:286`). So `regress.sh` cannot validate the Android modules; the hashes above are the only cross-check for now.
3. **The engine needs Vulkan 1.1, but the manifest and README ask for 1.3.** The instance requests 1.1 (`vulkan_present.cpp:139`) and the engine requires only 1.1 (`xbg_vulkan.cpp:154`, "SPIR-V 1.3"). `AndroidManifest.xml:6` declares `android.hardware.vulkan.version` `0x403000`, which is 1.3. Earlier probe runs on a Redmi K50 Ultra (Snapdragon 8+ Gen 1, Adreno 730, HyperOS 3, Android 15) reported Vulkan **1.1.128**, driver 512.615, 64-lane subgroups, no `shaderFloat64` and no fragment shader interlock. That phone is the next test target. Bundles translated for Adreno 830 may declare capabilities that `validate_capabilities` rejects there.
4. **Start-up timeline zero is wrong in the App under USAP.** `main.c` reads the process creation time from `/proc/self/stat`. Android can pre-fork app processes (USAP pool), and then that field is the pre-fork time. On the Redmi it read **8,422 s** against Java's `Process.getStartUptimeMillis()` **3.3 s**. Every `[x.xxs]` log time and the boot profile's ⓪ segment are offset by that amount. Fix: PR (branch experimental build).
5. **The App does not link `.holes` tables, so upstream's trimmed tester pack breaks.** `link_romfs` in `jni_android.c` links only `*.data` and `build.config`. `hle_fs.c` looks for `<archive>.holes` in `romfs/data/`, so it reads the four zero-stripped archives at physical offsets. Fix: PR (branch experimental build), with the device evidence below.

## Device bring-up on the Redmi K50 Ultra (15:15–15:35)

Redmi K50 Ultra, Snapdragon 8+ Gen 1, Adreno 730, HyperOS 3 / Android 15 (API 35), USAP pool enabled. APK built from this branch (with the changes merged in), signed with this host's debug key.

| Step | Result |
|---|---|
| `adb install` (first install) | Success. The manifest's `android.hardware.vulkan.version` 1.3 does not block sideloading on this Vulkan 1.1 device |
| All-files access | `appops set --uid com.xiaoboluo.d2r MANAGE_EXTERNAL_STORAGE allow` |
| Game files | Upstream's trimmed pack (18 archives, 4 `.holes`, `build.config`) pushed to `Download/data` in 2 min |
| Modules | r44 Android `out/` (24 files, 79 MB) copied into `files/dm/out/` with `run-as` |
| Launch, no shaders | 23 files linked, then stops at `boot: no shader bundles` (error screen, as designed) |

To check the host before real shaders arrive, two launches used a single **empty placeholder** `.xgs`, deleted afterwards. That only gets past the launcher's directory check; the first real shader load must fail.
- **With the changes:**
  - The Vulkan engine initialised on the Adreno 730: `NVN caps api=1.1.128`, swapchain 2712×1220, `mirror-clamp-to-edge` enabled.
  - Modules loaded with the bionic key-table slots (`bionic 键表偏移 0`), relocations ran, all 264 forced take-overs hit.
  - SDK init and `nnMain` ran, with a 3,728 MB guest heap.
  - RomFS, temp and transactional save mounted, and `hle_fs.c` registered all four `.holes` tables.
  - The NVN window was 2000×900, and the run stopped at the game's first shader request: `Missing genuine shader bundle …/a1217a07d94c1184.xgs`.
- **Without the changes** (same data, stale links removed): 19 files linked, three `tact：zlib 解码失败`, then `CRASH sig=11` at `main+0x1b692dc`, before any shader request.
- **Start time (the changes):** this App's own process was dated 21,668 s (about 6 h) before its launch by `/proc/<pid>/stat`, because the USAP pool pre-forked it.

The phone now has the the changes build, no placeholder, and a default `Settings` file in its saves from these runs.

## Next

the project is generating `.xgs` bundles for the Redmi K50 Ultra (finding 1). With them: real shaders in `files/dm/shaders/`, then the device acceptance list in `dm/android/README.md` (rendering, character creation, entering a world, NPCs, inventory/skills, sound, background/resume, save/reload) and an FPS reading on Adreno 730.
