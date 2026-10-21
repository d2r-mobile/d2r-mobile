# Game-free CI and reproducible entry

This directory is the only CI entry surface for issue #17. It does not read
game files, save data, keys or credentials, and it does not run a game, SDK or
device.

## Entry points

```sh
# Host build + CTest on the current machine (x86_64 or arm64).
bash ci/scripts/ci.sh host

# Pinned Android NDK r29 Linux download for an x86_64 runner.
ndk=$(bash ci/scripts/ndk-r29.sh)
ANDROID_NDK_HOME="$ndk" bash ci/scripts/ci.sh android
```

Owned metadata/failure checks:

```sh
bash ci/tests/test-ndk-stub.sh
bash ci/tests/test-ci-failures.sh
bash ci/tests/test-elf-pages.sh
```

`DM_CI_WORK` selects an external work directory; `DM_ROOT`/personal disk paths
are not required. Builds use the repository's existing CMake files without
modifying runtime or root CMake.

The required Java job also runs the accepted #189 hotplug regressions from the
current checkout: the real `android.jar` Java listener control, the actual JNI
start/stop fixture with private mutants, and the actual NativeActivity callback
harness. Its private negative controls include a copied native source with the
wrong loader receiver and a malformed compile copy; either must fail the same
required command. `DM_HOTPLUG_NATIVE_SOURCE` is a test-only override used to
run that command against a private source copy; its default is the current
checkout's `dm/android/app/native.c`. This is host/cross-build evidence only,
not physical hotplug or device acceptance.

Action commits are recorded in `ci/actions.lock`.

## Runner and tool facts

- `host-x86_64`: `ubuntu-24.04` GitHub-hosted x86_64 runner, native host build
  and CTest.
- `host-arm64`: `ubuntu-24.04-arm`, an official GitHub-hosted public-repository
  arm64 runner (GitHub changelog, 2025-08-07). It runs the same host build and
  tests natively on ARM64; it is not an Android device.
- `android-arm64-cross`: `ubuntu-24.04` x86_64 runner. It downloads the pinned
  Android NDK r29 Linux package (`29.0.14206865`, SHA-1
  `87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b`) and cross-builds
  `arm64-v8a`/API29. ARM64 binaries are **not executed** on the x86 runner.
  The job status is `cross_build_only`, never a device pass.
  Local ARM hosts may set `DM_CI_ALLOW_ARM_CROSS=1` only to cross-build; that is
  still not execution or CI evidence.

Runner labels are from GitHub's public-repository arm64 runner announcement
(2025-08-07) and the Ubuntu 24.04 runner image/toolset metadata:
<https://github.blog/changelog/2025-08-07-arm64-hosted-runners-for-public-repositories-are-now-generally-available/>.

## Pinned dependencies and cache keys

`dm/android/dependencies.cmake` pins LZ4 v1.10.0 and Mbed TLS v3.6.4 by SHA-256.
The manifest records those URLs and digests. Host FetchContent cache keys include
the runner architecture, commit SHA, dependency CMake and Android CMake hash.
The NDK cache key includes the NDK archive SHA-1 and runner OS. Build output and
test logs are not cached.

## Evidence allowlist

Only these files are uploaded:

- `manifest.json` — commit, dirty flag, tool versions, dependency digests,
  parameters, test status and limitations;
- `host-ctest.log` or `android-ctest.log`;
- `elf-pages.json` for the Android cross-build.

The manifest records whether the tree was dirty. CI starts from a fresh checkout,
so its dirty flag should be false. Historical local results are never copied into
a current CI artifact.

## 4/16 KiB checks

`elf_pages.py` statically inspects every executable ELF in the cross-build:
each `PT_LOAD` `p_align` must be a positive power of two at least 16 KiB, and
`p_offset`/`p_vaddr` must be congruent modulo `p_align`. A zero or non-power-of-
two alignment, a misaligned segment, a parse failure or an empty set is rejected.
This is a file layout check only. `runtime_page_size` remains `not_executed`;
current device page-size behavior is unknown and is not claimed.

## Current limits

This is a bounded CI partial: host tests are native host tests, Android is a
cross-build only, and there is no real game/SDK/rtld/phone, HID, HUD or playable
acceptance. Issue #17 and parent #19 stay open until actual CI evidence and the
remaining acceptance conditions are reviewed.
