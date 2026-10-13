#!/bin/bash
# 把 Mac 着色器库按指定的最低系统版本重编一份：msl_retarget.sh <最低系统，如 15.0> <输出根目录>   产出 <输出根目录>/b-<翻译链版本>/
# 为什么要有：dmsh_build.py 编库时不带最低版本参数，库里记的目标是编译这台机器的 SDK（macOS 26）；老系统上加载这种库会直接失败
# （同程序本体的最低系统版本是一回事，换了个地方）。老接口版的包（tools/mac_app.sh DM_MAC_M3=1）带的是这里重编出来的库。
# 源码来源（共用缓存只读）：_b 库旁边的 .metal；_bf 库（可读 tile 版本）的源码在内容寻址缓存里，按库文件内容对上。
# 编译命令与 dmsh_build.py 相同，只多一个 -mmacosx-version-min。约 7000 个库 3 分钟。
# Recompile the Mac shader libraries for a given minimum system version: msl_retarget.sh <minimum system, e.g. 15.0> <output root>   produces <output root>/b-<translation chain version>/
# Why it exists: dmsh_build.py compiles the libraries without a minimum-version flag, so the target recorded in them is the SDK of the build machine (macOS 26); loading such a library on an older system simply fails
# (the same matter as the minimum system version of the executable, in a different place). The older-API package (tools/mac_app.sh DM_MAC_M3=1) carries the libraries recompiled here.
# Where the sources come from (the shared cache is read-only): the .metal next to each _b library; the sources of _bf libraries (the readable-tile variants) are in the content-addressed cache and are matched by the content of the library file.
# The compile command is the same as in dmsh_build.py with only -mmacosx-version-min added. About 7000 libraries in 3 minutes.
set -u; MIN=$1; OUTROOT=$2
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=${DM_ROOT:-$HOME/dm-work}
V=$(sed -n 's/.*DMSH_CHAIN "\(.*\)".*/\1/p' "$HERE/../nvn/dmsh_chain.h"); S=$ROOT/build/cache/metal_shader_cache/b-$V; CAS=$ROOT/build/cache/metal_shader_cache/cas/variant
[ -d "$S" ] || { echo "🔴 没有着色器库 ${S}（先跑 dmsh_build.py）"; exit 2; }
D=$OUTROOT/b-$V; rm -rf "$D"; mkdir -p "$D"; cp -c "$S"/*.json "$D"/
one() { src=$1; dst=$2; t=$(mktemp -d); if xcrun -sdk macosx metal -c -mmacosx-version-min="$MIN" "$src" -o "$t/a.air" 2>"$t/err" && xcrun -sdk macosx metallib "$t/a.air" -o "$t/a.metallib" 2>>"$t/err"; then mv "$t/a.metallib" "$dst"; else echo "失败 $(basename "$dst")：$(head -1 "$t/err" | cut -c1-120)"; fi; rm -rf "$t"; }
export -f one; export MIN D S
BAD=$(ls "$S" | grep '_b\.metal$' | sed 's/\.metal$//' | xargs -P 10 -I{} bash -c 'one "$S/{}.metal" "$D/{}.metallib"')
T=$(mktemp); for r in "$CAS"/*/; do [ -f "$r/mac.metallib" ] && [ -f "$r/metal" ] && echo "$(shasum -a 256 "$r/mac.metallib" | cut -c1-64) $r"; done > "$T"
miss=0
for f in "$S"/*_bf.metallib; do [ -f "$f" ] || continue; h=$(shasum -a 256 "$f" | cut -c1-64); r=$(grep -m1 "^$h " "$T" | cut -d' ' -f2-)
  if [ -n "$r" ]; then m=$D/$(basename "${f%.metallib}").metal; cp "$r/metal" "$m"; BAD="$BAD$(one "$m" "$D/$(basename "$f")")"; rm -f "$m"; else miss=$((miss+1)); fi; done
rm -f "$T"
have=$(ls "$D" | grep -c '\.metallib$'); want=$(ls "$S" | grep -c '\.metallib$')
[ -z "$BAD" ] && [ "$miss" = 0 ] && [ "$have" = "$want" ] || { echo "🔴 着色器库重编不全：产出 ${have} / ${want}，_bf 源码没对上 ${miss} 个；${BAD:0:300}"; exit 3; }
echo "MSL_RETARGET ${D}（${have} 个库，目标 macOS ${MIN}）"
