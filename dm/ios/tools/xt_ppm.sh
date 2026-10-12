#!/bin/bash
# 后处理链上界矩阵：每局一种环境，输出各趟（按首绘片元键）对深度预通道片元的归一比值。
# 用法：ppm.sh <序列…>，元素 = base / nofxaa / bloomq0 / skiptm / skipcp / skipfx / skipcopy / skiplin
# Upper-bound matrix for the post-processing chain: one environment per run; outputs each pass's (keyed by first-draw fragment key) normalized ratio to the depth pre-pass fragment.
# Usage: ppm.sh <sequence...>, elements = base / nofxaa / bloomq0 / skiptm / skipcp / skipfx / skipcopy / skiplin
PN=$(bash "$(dirname "$0")/../../tools/product.sh" name touch); D=${XT_MINI_DIR:-$(ls -dt "${DM_ROOT:-$HOME/dm-work}"/run/mini/*_"$PN" 2>/dev/null | head -1)}; [ -d "$D" ] || { echo "没有 mini 同步目录 ${DM_ROOT:-$HOME/dm-work}/run/mini/*_${PN}（先 mini_sync.sh，或用 XT_MINI_DIR 指定）"; exit 2; }; T=$D/src/dm/ios/tools; B=${DM_ROOT:-$HOME/dm-work}/run/xtrace/batch_$(date +%m%d_%H%M%S)_ppm; mkdir -p $B; O=$B/out.tsv; : > $O
source $T/mlock.sh; mlock_acquire "${XT_OWNER:?先设 XT_OWNER=线名} 后处理矩阵 $# 局 约 $(( $# * 95 / 60 )) 分钟"; trap mlock_release EXIT; i=0
for v in "$@"; do
  case $v in
    base) x=;; nofxaa) x="DM_NVN_NOFXAA=1";; bloomq0) x="DM_NVN_BLOOM_Q=0";;
    skiptm) x="DM_NVN_SKIP_FS=2e2d6700,f371fb3f,a1b1aab6";; skipcp) x="DM_NVN_SKIP_FS=52aac1f2,9a650561";; skipfx) x="DM_NVN_SKIP_FS=ebcfcd1f";;
    skipcopy) x="DM_NVN_SKIP_FS=d5fcb22f,45a5b1ff";; skiplin) x="DM_NVN_SKIP_FS=56c2fef6,5ddee022";; *) echo "未知 $v"; continue;;
  esac
  i=$((i + 1)); R=$B/$(printf %02d $i)_$v
  XT_DIR=$R XT_ENV="DM_NVN_ENC_LABEL=1 $x" XT_ONLY='^metal-gpu-intervals$' XT_TREE=$D/src XT_OUT=$D/out/$PN XT_SAVE=$D/data/switch/save_ref_warlock/0000000000000001 \
    bash $T/xtrace.sh mac camp 'Game Performance' 12 35 > $R.log 2>&1
  red=$(grep -c '【红】' $R/host.err 2>/dev/null)
  python3 $T/xtrace_report.py $R 2>/dev/null | python3 ${DM_ROOT:-$HOME/dm-work}/run/xtrace/ppm_row.py "$v" "$red" | tee -a $O
  [ -d $R/tables ] && find $R/tables -delete   # 读数已进 out.tsv，导出的表不留（单局约 100 MB） | The readings are already in out.tsv, so the exported tables are not kept (about 100 MB per run)
done
