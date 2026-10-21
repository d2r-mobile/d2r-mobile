#!/usr/bin/env bash
# 用 NDK 交叉编译 ARM64 设备探针，不读取游戏文件；依据官方 CMake 工具链。
# Cross-compile the ARM64 device probe with the NDK without game files; based on the official CMake toolchain.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/paths.sh"
ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-$DM_ROOT/tools/android/android-ndk-r29}}
if [[ ! -f "$ndk/build/cmake/android.toolchain.cmake" ]]; then
    echo 'Set ANDROID_NDK_HOME to an installed Android NDK (r28 or newer recommended).' >&2
    exit 1
fi
build=${DM_ANDROID_BUILD_DIR:-$DM_ROOT/build/android/arm64-v8a}
mkdir -p "$TMPDIR"
cmake -S "$here" -B "$build" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build" --parallel
printf 'Built native tools in %s\n' "$build"
