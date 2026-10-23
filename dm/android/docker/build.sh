#!/usr/bin/env bash
# 在容器里构建安卓 App（以宿主用户 UID 跑）。先建镜像：docker build -t dm-android-build:1 dm/android/docker
#   dm/android/docker/build.sh                    assembleDebug + lintDebug
#   dm/android/docker/build.sh gradle <参数...>   在 /src 里跑任意命令；build.sh bash 进 shell
# 挂载：
#   /src           本仓库（脚本所在的工作树，或 DM_SRC）
#   /gen           由游戏文件生成的宿主表头，只读     DM_GEN_DIR（必需；生成方法见 dm/host/Makefile，不入库）
#   /android-user  调试签名钥匙（ANDROID_USER_HOME）  DM_ANDROID_USER（缺省 ~/.android-dm）
#   /cache         Gradle 缓存                        具名卷 dm-android-build-cache
# 钥匙要留着：已装的 App 只接受同一钥匙签的升级包，卸载会连存档一起删。第一次用时如有 Windows 版 Android Studio 的
# %USERPROFILE%\.android\debug.keystore 就拷来，这里出的包与 Android Studio 出的包可以互相覆盖安装。
#
# Build the Android App in the container (runs as the host user's UID). Build the image first: docker build -t dm-android-build:1 dm/android/docker
#   dm/android/docker/build.sh                    assembleDebug + lintDebug
#   dm/android/docker/build.sh gradle <args...>   run any command in /src; build.sh bash for a shell
# Mounts:
#   /src           this repository (the worktree the script is in, or DM_SRC)
#   /gen           host headers generated from the game files, read-only  DM_GEN_DIR (required; generated as in dm/host/Makefile, not committed)
#   /android-user  debug signing key (ANDROID_USER_HOME)                  DM_ANDROID_USER (default ~/.android-dm)
#   /cache         Gradle caches                                          named volume dm-android-build-cache
# Keep the key: an installed App only accepts upgrades signed with the same key, and uninstalling deletes the saves with it. On first use, Windows
# Android Studio's %USERPROFILE%\.android\debug.keystore is copied when present, so packages from here and from Android Studio overlay-install each other.
set -euo pipefail

image=${DM_ANDROID_BUILD_IMAGE:-dm-android-build:1}
src=$(readlink -f "${DM_SRC:-$(dirname "$0")/../../..}")   # DM_SRC：改编另一工作树（如 git worktree）| DM_SRC: build another worktree (e.g. a git worktree)
gen=$(readlink -f "${DM_GEN_DIR:?要 DM_GEN_DIR=<宿主表头目录> | needs DM_GEN_DIR=<host header directory>}")
user_home=${DM_ANDROID_USER:-$HOME/.android-dm}

for f in dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc; do
    [ -f "$gen/$f" ] || { echo "缺 $gen/$f | missing $gen/$f (dm/host/Makefile)" >&2; exit 1; }
done
mkdir -p "$user_home"
if [ ! -f "$user_home/debug.keystore" ] && command -v cmd.exe > /dev/null; then
    win_key=$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2> /dev/null | tr -d '\r')")/.android/debug.keystore
    [ -f "$win_key" ] && cp "$win_key" "$user_home/debug.keystore" && echo "debug key ← $win_key"
fi

if [ $# -eq 0 ]; then
    set -- gradle --no-daemon -p dm/android :app:assembleDebug :app:lintDebug -PdmGenDir=/gen -PdmPython=/usr/bin/python3
fi

args=(--rm --user "$(id -u):$(id -g)"
      -v "$src:/src"
      -v "$gen:/gen:ro"
      -v "$user_home:/android-user"
      -v dm-android-build-cache:/cache)
if [ -t 0 ] && [ -t 1 ]; then
    args+=(-it)
fi
exec docker run "${args[@]}" "$image" "$@"
