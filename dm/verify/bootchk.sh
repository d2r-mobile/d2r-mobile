#!/bin/bash
# 产物目录启动块清单核对（r11 原样跑时缺清单不开预取 = 出版漏项）：bootlist.tsv 必须存在、头行 BUILD_ID 与 main.cuts.tsv 一致、bootlist.sha256 与内容一致、块数 > 0
# 清单入库为版本化数据，产物 bootlist.tsv 须与仓库 dm/data/bootlist_<main 构建号>.tsv 逐字节相同（记录有竞态、不能重录比对）
# 用法：bootchk.sh <产物目录>   末行 BOOTLIST OK|RED …；退出码 0/1
# Boot block list check of the build output directory (when r11 was run as is, the missing list meant no prefetch = an omission in the release): bootlist.tsv must exist, the BUILD_ID in its header line must match main.cuts.tsv, bootlist.sha256 must match the content, block count > 0
# The list is checked into the repository as versioned data, and the build output bootlist.tsv must be byte-for-byte identical to the repository's dm/data/bootlist_<main build ID>.tsv (recording is racy, so it cannot be re-recorded for comparison)
# Usage: bootchk.sh <build output directory>   last line BOOTLIST OK|RED ...; exit code 0/1
D=${1%/}; B=$D/bootlist.tsv
red() { echo "BOOTLIST RED $*"; exit 1; }
[ -s "$B" ] || red "缺 $B"
bid=$(sed -n '1s/^# BUILD_ID[[:space:]]*//p' "$B"); cid=$(grep -m1 '^# BUILD_ID' "$D/main.cuts.tsv" | awk '{print $NF}')
[ -n "$bid" ] && [ "$bid" = "$cid" ] || red "构建号不符：清单 ${bid:-无} / main.cuts.tsv ${cid:-无}"
RB=$(cd "$(dirname "$0")/../data" 2>/dev/null && pwd)/bootlist_$bid.tsv
[ -f "$RB" ] || red "仓库缺清单 dm/data/bootlist_$bid.tsv"
cmp -s "$B" "$RB" || red "与仓库 dm/data/bootlist_${bid:0:12}….tsv 不同（产物 $(grep -vc '^#' "$B") 块 / 仓库 $(grep -vc '^#' "$RB") 块）"
[ -f "$D/bootlist.sha256" ] && (cd "$D" && shasum -a 256 -c bootlist.sha256 >/dev/null 2>&1) || red "bootlist.sha256 缺或不符"
n=$(grep -vc '^#' "$B"); [ "$n" -gt 0 ] || red "清单 0 块"
# 清单对不对是静态性质——逐块核数据文件存在、不越界、不重复、开头 16 字节与资源包一致（运行时预取命中只报不判）
# Whether the list is correct is a static property -- check block by block that the data file exists, is not out of bounds, is not duplicated, and that the first 16 bytes match the resource package (runtime prefetch hits are only reported, not judged)
st=$(python3 "$(dirname "$0")/bootlist_static.py" "$RB" ${DM_ROMFS:+"$DM_ROMFS/data"}) || red "$(echo "$st" | tail -1)；$(echo "$st" | head -1)"
echo "BOOTLIST OK $n 块 构建号 ${bid:0:12}… 与仓库清单逐字节相同，逐块与资源包一致"
