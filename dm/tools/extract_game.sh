#!/bin/bash
# 从你自己的游戏包里取出本项目要的两部分（更新后的程序文件与资源），放进工作区。
# 用法：extract_game.sh <游戏包 .xci 或 .nsp> <prod.keys>
#   游戏包：你自己导出的《暗黑破坏神 II：狱火重生》Switch 版，里面要同时有本体与 3.3.94012 的更新（.xci 常见的「1G+1U」）。
#   prod.keys：从你自己的主机导出的密钥文件。两样都只在本机读，不拷贝、不上传。
# 产物：$DM_ROOT/data/switch/3.3.94012/Update/{ExeFS,RomFS}（DM_ROOT 缺省 ~/dm-work）。已有这个目录时不覆盖。
# 需要：hactool（开源的 Switch 文件解包工具，https://github.com/SciresM/hactool ，按它的说明 make 出来）。
#   放进 PATH，或用 HACTOOL=<路径> 指给本脚本。磁盘要留出约 20 GB（中间文件 9 GB，做完自动删）。
# Extract the two parts this project needs (the updated program files and the assets) from your own copy of the game and put them into the workspace.
# Usage: extract_game.sh <game dump .xci or .nsp> <prod.keys>
#   Game dump: your own dump of the Switch version of Diablo II: Resurrected; it must contain both the base game and the 3.3.94012 update (the common "1G+1U" form for .xci).
#   prod.keys: the key file dumped from your own console. Both are only read on this machine; nothing is copied or uploaded.
# Output: $DM_ROOT/data/switch/3.3.94012/Update/{ExeFS,RomFS} (DM_ROOT defaults to ~/dm-work). An existing directory is not overwritten.
# Requires: hactool (an open-source tool for unpacking Switch files, https://github.com/SciresM/hactool ; build it with make following its instructions).
#   Put it on PATH, or point this script at it with HACTOOL=<path>. Keep about 20 GB of disk free (9 GB of intermediate files, deleted automatically when done).
set -u
[ $# -eq 2 ] || { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
PKG=$1; KEYS=$2; DM_ROOT=${DM_ROOT:-$HOME/dm-work}; OUT=$DM_ROOT/data/switch/3.3.94012/Update
die() { echo "✗ $*" >&2; exit 1; }
HT=${HACTOOL:-$(command -v hactool || true)}; [ -n "$HT" ] && [ -x "$HT" ] || die "没找到 hactool：装好后放进 PATH，或设 HACTOOL=<路径>（见本文件开头）"
[ -f "$PKG" ] || die "没有这个游戏包：$PKG"; [ -f "$KEYS" ] || die "没有这个密钥文件：$KEYS"
[ ! -e "$OUT" ] || die "已有 $OUT，不覆盖。要重做先自己把它移走"
ht() { "$HT" -k "$KEYS" --disablekeywarns --suppresskeys "$@"; }
T=$DM_ROOT/build/cache/dm/extract.$$; mkdir -p "$T/nca" || die "建不了中间目录 $T"; trap 'rm -rf "$T"' EXIT
say() { echo "[$(date +%T)] $*"; }

say "拆游戏包（约 9 GB）"
case "$PKG" in
  *.xci|*.XCI) ht -t xci --securedir="$T/nca" "$PKG" > "$T/unpack.log" 2>&1 || die "游戏包拆不开（密钥不对或文件损坏），日志 $T/unpack.log：$(tail -1 "$T/unpack.log")";;
  *.nsp|*.NSP) ht -t pfs0 --outdir="$T/nca" "$PKG" > "$T/unpack.log" 2>&1 || die "游戏包拆不开（密钥不对或文件损坏）：$(tail -1 "$T/unpack.log")";;
  *) die "只认 .xci 或 .nsp";;
esac

# 找出本体与更新的程序内容：按每个内容文件自己报的类型与程序号认，不靠文件名。
# 更新的程序内容报的也是本体的程序号；区别是它的资源段是「补丁」格式（BKTR），要叠在本体上才读得出
# Find the program content of the base game and of the update: identify them by the type and program id each content file reports, not by file name.
# The update's program content also reports the base game's program id; the difference is that its asset section is in "patch" format (BKTR) and can only be read when layered on top of the base game
BASE=; UPD=
for f in "$T"/nca/*.nca; do
  case "$f" in *.cnmt.nca) continue;; esac
  info=$(ht -t nca "$f" 2>&1)
  echo "$info" | grep -q "Content Type: *Program" || continue
  echo "$info" | grep -q "^Title ID: *0100726014352000" || continue
  if echo "$info" | grep -q "BKTR"; then UPD=$f; else BASE=$f; fi
done
[ -n "$BASE" ] || die "游戏包里没找到这个游戏的本体"
[ -n "$UPD" ] || die "游戏包里没有更新：本项目要 3.3.94012 的更新，只有本体不行"

say "取程序文件与资源（约 9 GB）"
mkdir -p "$OUT.part" || die "建不了 $OUT.part"
ht -t nca --basenca="$BASE" --exefsdir="$OUT.part/ExeFS" --romfsdir="$OUT.part/RomFS" "$UPD" > "$T/extract.log" 2>&1 || { rm -rf "$OUT.part"; die "更新内容取不出来：$(tail -1 "$T/extract.log")"; }

# 版本核对：三个程序文件的校验值必须是 3.3.94012 的
# Version check: the checksums of the three program files must be those of 3.3.94012
ok=1
while read -r want name; do
  got=$(shasum -a 256 "$OUT.part/ExeFS/$name" 2> /dev/null | cut -c1-64)
  if [ "$got" = "$want" ]; then echo "  ✓ $name"; else echo "  ✗ $name 不是 3.3.94012 的（${got:-文件不在}）"; ok=0; fi
done <<SUMS
55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e main
1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017 sdk
67a38fcf89cd13e040a021eb3d157072f38ebf6680a907874c35c7b231b5d4e2 subsdk0
SUMS
[ -f "$OUT.part/RomFS/data/build_manifest.psv" ] || { echo "  ✗ 资源里没有 data/build_manifest.psv"; ok=0; }
[ "$ok" = 1 ] || { rm -rf "$OUT.part"; die "版本不对：游戏包里的更新不是 3.3.94012"; }
mv "$OUT.part" "$OUT" || die "改名到 $OUT 没成"
say "好了：$OUT。接着在仓库根目录运行 ./setup.sh"
