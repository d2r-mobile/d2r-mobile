#!/usr/bin/env bash
# 把本机生成的运行包（out / shaders / overlay / ui-art）压成 dm-pack.zip 放进调试版的内置资源目录，再打调试版 HAP；应用首次启动时解压到 files/dm。
# 这些都是从游戏文件生成的：只进调试版（私下安装给有游戏的测试者），不入库，发布版（AGC）不带——打完发布版用 check_release_clean.py 检查。
# 资源目录在 HAP 里不压缩（692 MB 原样进包），所以先压缩（约 202 MB）。
# 用法：scripts/stage_debug_pack.sh <运行包目录，里面有 out shaders overlay ui-art>
# Zip the locally generated runtime pack (out / shaders / overlay / ui-art) into dm-pack.zip in the debug build's bundled resources, then build the debug HAP; the app unpacks
# it into files/dm on first launch. All of it is generated from game files: debug builds only (private installs for testers who own the game), never committed, never in the
# release (AGC) build — check the release package with check_release_clean.py. Resource files are stored uncompressed in a HAP (692 MB as is), hence the zip (~202 MB).
# Usage: scripts/stage_debug_pack.sh <runtime pack dir containing out shaders overlay ui-art>
# 管线种子清单（可选）放在 shaders/vk-pipeline-list.seed，用 dm/android/tools/merge_warm_lists.py 合并各设备的清单 | the optional pipeline seed goes in shaders/vk-pipeline-list.seed, merged from devices' lists with dm/android/tools/merge_warm_lists.py
set -euo pipefail
src=${1:?usage: stage_debug_pack.sh <pack dir>}
here=$(cd "$(dirname "$0")/.." && pwd)
dst="$here/entry/src/debugpack/resources/resfile/dm"
for d in out shaders overlay ui-art; do [ -d "$src/$d" ] || { echo "missing $src/$d" >&2; exit 1; }; done
rm -rf "$dst"; mkdir -p "$dst"
# 包版本 = 全部文件内容的哈希；应用启动时与 files/dm 里记的版本比较，不同才重新解压
# Pack version = hash of every file's contents; at launch the app compares it with the version recorded in files/dm and unpacks again only when it differs
(cd "$src" && find out shaders overlay ui-art -type f | LC_ALL=C sort | xargs -d '\n' sha256sum | sha256sum | cut -c1-16) > "$dst/pack-version.txt"
python -I - "$src" "$dst/dm-pack.zip" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for d in ('out', 'shaders', 'overlay', 'ui-art'):
        for root, _, files in os.walk(os.path.join(src, d)):
            for f in sorted(files):
                p = os.path.join(root, f)
                z.write(p, '/'.join(os.path.relpath(p, src).split(os.sep)))
PY
echo "staged $(du -h "$dst/dm-pack.zip" | cut -f1) dm-pack.zip, version $(cat "$dst/pack-version.txt")"
