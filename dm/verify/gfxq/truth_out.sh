#!/bin/bash
# 画质真值专用产物（只给离线画真值帧用，产品构建绝不带）：复制产物目录到 <源>-truth，
# 把游戏显存分配器预算常数 main+0x8524c4 `mov x8,#0x10400000`（260 MB）改成 `mov x8,#0x20000000`（512 MB）。
# 依据：内部分辨率 > 约 345 万像素时该分配器放不下渲染目标、游戏启动即崩；真值要按屏幕原生 2868x1320 画。
# 该常数只是显存池预算，不进游戏逻辑——由 V1 逐拍对拍核（见 image_quality.md）。
# 用法：truth_out.sh [源产物目录=$DM_ROOT/build/cache/dm/out/<当前代号>-keypage]   输出：<源>-truth，打印其路径
# Build output dedicated to image-quality ground truth (only for rendering ground-truth frames offline; product builds never carry it): copy the build output directory to <source>-truth,
# and change the game's video memory allocator budget constant at main+0x8524c4 from `mov x8,#0x10400000` (260 MB) to `mov x8,#0x20000000` (512 MB).
# Rationale: when the internal resolution is > about 3.45 million pixels, this allocator cannot fit the render targets and the game crashes at startup; ground truth must be rendered at the screen's native 2868x1320.
# This constant is only the video memory pool budget and does not enter game logic - checked by the V1 per-tick comparison (see image_quality.md).
# Usage: truth_out.sh [source build output dir=$DM_ROOT/build/cache/dm/out/<current codename>-keypage]   Output: <source>-truth, prints its path
set -eu
SRC=${1:-$(bash "$(dirname "$0")/../../tools/product.sh" path keypage)}; DST=$SRC-truth
OFF=$((0x8524c4)); OLD=d2a20808; NEW=d2a40008   # MOVZ X8,#0x1040,LSL#16 → MOVZ X8,#0x2000,LSL#16（小端存放） | MOVZ X8,#0x1040,LSL#16 → MOVZ X8,#0x2000,LSL#16 (stored little-endian)
le() { echo "$1" | sed -E 's/(..)(..)(..)(..)/\4\3\2\1/'; }
cur=$(xxd -s $OFF -l 4 -p "$SRC/main.patched.bin")
[ "$cur" = "$(le $OLD)" ] || { echo "【红】源产物 main+0x8524c4 不是预期指令 $OLD（读到小端 $cur），构建号变了？不改" >&2; exit 2; }
rm -rf "$DST"; cp -R "$SRC" "$DST"
printf "$(le $NEW | sed 's/../\\x&/g')" | dd of="$DST/main.patched.bin" bs=1 seek=$OFF conv=notrunc 2>/dev/null
[ "$(xxd -s $OFF -l 4 -p "$DST/main.patched.bin")" = "$(le $NEW)" ] || { echo "【红】写入核对失败" >&2; exit 1; }
cmp -l "$SRC/main.patched.bin" "$DST/main.patched.bin" | wc -l | awk '{ if ($1 > 4) { print "【红】改动字节数 " $1 " > 4" > "/dev/stderr"; exit 1 } }'
echo "$DST"
