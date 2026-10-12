#!/bin/bash
# 存档内容指纹：对一个用户存档目录（含 0/ 1/ ExtraData*）按 MANIFEST.sha256 同一格式（「sha  0000000000000001/相对路径」逐行、按路径排序）出清单，
# 打印清单本身的 sha256。与 data/switch/<存档>/MANIFEST.sha256 的 sha 可直接比（同内容 ⇒ 同值）。.lock 不计。
# 用法：savesha.sh <用户存档目录>   → 打印 64 位十六进制
# Save content fingerprint: for a user save directory (containing 0/ 1/ ExtraData*), produce a manifest in the same format as MANIFEST.sha256 ("sha  0000000000000001/<relative path>" per line, sorted by path),
# and print the sha256 of the manifest itself. Directly comparable with the sha of data/switch/<save>/MANIFEST.sha256 (same content ⇒ same value). .lock is excluded.
# Usage: savesha.sh <user save dir>   → prints 64 hex digits
set -eu
D=${1%/}
( cd "$D" && find . -type f ! -name .lock | sed 's|^\./||' | LC_ALL=C sort | while read -r f; do printf '%s  0000000000000001/%s\n' "$(shasum -a 256 "$f" | cut -c1-64)" "$f"; done ) | shasum -a 256 | cut -c1-64
