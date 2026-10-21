#!/usr/bin/env bash
# 在开发机执行无游戏数据回归，缓存与临时文件沿用 Android 工作区。
# Run game-free host regressions, keeping caches and temporary files in the Android workspace.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/paths.sh"
mkdir -p "$TMPDIR"
build=${DM_ANDROID_HOST_BUILD_DIR:-$DM_ROOT/build/host-sanitized}
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' "$@"
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
