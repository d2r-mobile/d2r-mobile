#!/usr/bin/env bash
# Game-free reproducible build/test entry used by GitHub Actions and locally.
# Modes:
#   host    native host build + CTest on this machine (x86_64 or arm64)
#   android ARM64 Android NDK r29/API29 cross-build; binaries are not executed
set -uo pipefail
mode=${1:-}
case "$mode" in host|android) ;; *) echo "usage: ci.sh host|android" >&2; exit 2;; esac
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
arch=$(uname -m)
if [[ "$mode" == android && "$arch" != "x86_64" &&
      "${DM_CI_ALLOW_ARM_CROSS:-0}" != "1" ]]; then
    echo "pinned NDK r29 Linux package targets x86_64; refusing $arch runner" >&2
    exit 2
fi
work=${DM_CI_WORK:-${RUNNER_TEMP:-/tmp}/dm-ci}
build=${DM_CI_BUILD_DIR:-$work/$mode-$(uname -m)}
logs=$work/logs
artifacts=$work/artifacts
mkdir -p "$build" "$logs" "$artifacts" "$work/tmp"
rm -f "$artifacts/manifest.json" "$artifacts/elf-pages.json"
export TMPDIR=$work/tmp
export ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
params=(CMAKE_BUILD_TYPE=Debug DM_ANDROID_REWRITE_SCAN=OFF DM_ANDROID_VULKAN=OFF)
if [[ "$mode" == host ]]; then
    log=$artifacts/host-ctest.log
else
    log=$artifacts/android-build.log
fi
pages=$artifacts/elf-pages.json
status=pass
configure_args=(-S "$root/dm/android" -B "$build"
    -DCMAKE_BUILD_TYPE=Debug -DDM_ANDROID_REWRITE_SCAN=OFF
    -DDM_ANDROID_VULKAN=OFF
    -DFETCHCONTENT_BASE_DIR="$work/fetchcontent-$(uname -m)")

if [[ "$mode" == host ]]; then
    if command -v clang >/dev/null 2>&1; then
        export CC=clang
        params+=(COMPILER=clang)
    else
        params+=(COMPILER="${CC:-cc}")
    fi
    configure_args+=(-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
                     -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined')
    params+=(SANITIZERS=address,undefined PAGE_CHECK=none)
    cmake "${configure_args[@]}" >"$logs/host-configure.log" 2>&1 || {
        status=configure_failed; cp "$logs/host-configure.log" "$log" 2>/dev/null || true; }
    if [[ "$status" == pass ]]; then
        cmake --build "$build" --parallel >"$logs/host-build.log" 2>&1 || {
            status=build_failed; cp "$logs/host-build.log" "$log" 2>/dev/null || true; }
    fi
    if [[ "$status" == pass ]]; then
        ctest --test-dir "$build" --output-on-failure >"$log" 2>&1 || status=test_failed
    fi
    test_status=$status
else
    ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
    if [[ -z "$ndk" || ! -f "$ndk/build/cmake/android.toolchain.cmake" ]]; then
        echo "Set ANDROID_NDK_HOME to the pinned NDK r29 (run ci/scripts/ndk-r29.sh)." >&2
        exit 2
    fi
    rev=$(awk -F' = ' '/^Pkg.Revision/{print $2}' "$ndk/source.properties")
    params+=(ANDROID_ABI=arm64-v8a ANDROID_PLATFORM=android-29 NDK_REV="$rev"
             PAGE_CHECK=static_load_align_only RUNTIME_PAGE_SIZE=not_executed)
    configure_args+=(-DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake"
                     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29
                     -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON)
    cmake "${configure_args[@]}" >"$logs/android-configure.log" 2>&1 || {
        status=configure_failed; cp "$logs/android-configure.log" "$log" 2>/dev/null || true; }
    if [[ "$status" == pass ]]; then
        cmake --build "$build" --parallel >"$logs/android-build.log" 2>&1 || {
            status=build_failed; cp "$logs/android-build.log" "$log" 2>/dev/null || true; }
    fi
    if [[ "$status" == pass ]]; then
        readelf=$(find "$ndk/toolchains/llvm/prebuilt" -type f -name llvm-readelf | head -1)
        [[ -n "$readelf" ]] || readelf=$(command -v readelf || true)
        [[ -n "$readelf" ]] || readelf=$(command -v llvm-readelf || true)
        [[ -n "$readelf" ]] || { echo "readelf unavailable" >&2; exit 2; }
        python3 "$root/ci/scripts/elf_pages.py" "$readelf" "$build" >"$pages" 2>"$logs/elf-pages.log" || status=page_check_failed
    fi
    test_status=cross_build_only
    [[ "$status" == pass ]] || test_status=$status
    cp "$logs/android-build.log" "$log" 2>/dev/null || true
fi

# The log is published: scrub checkout/work paths before manifest generation.
python3 "$root/ci/scripts/sanitize_log.py" "$log" "$root" "$work" >/dev/null 2>&1 || status=sanitize_failed
[[ -s "$log" ]] || { echo "missing diagnostic log: $log" >&2; status=missing_log; }

manifest_args=(--param "WORKSPACE_LABEL=external")
for p in "${params[@]}"; do manifest_args+=(--param "$p"); done
if ! python3 "$root/ci/scripts/manifest.py" --mode "$mode" --repo "$root" \
    --build-dir "$build" --artifact-dir "$artifacts" --log "$log" \
    --test-status "$test_status" --pages "$pages" \
    "${manifest_args[@]}" >/dev/null 2>"$logs/manifest.log"; then
    status=manifest_failed
fi
[[ -s "$artifacts/manifest.json" ]] || { echo "manifest not produced" >&2; status=manifest_failed; }
printf 'mode=%s status=%s build=%s log=%s\n' "$mode" "$status" "$build" "$log"
[[ "$status" == pass ]]
