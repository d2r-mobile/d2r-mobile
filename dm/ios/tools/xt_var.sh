#!/bin/bash
# 单个着色器变体的上界量法：XT_OWNER=<持有者名> var.sh <序列…>，序列元素 = base 或变体名
# 变体库在 $DM_ROOT/run/xtrace/terr/<名>/b-89f2d5b38edb/（整套克隆后只换被改着色器的 _b.metallib；metallib 在本机编，mini 没有编译工具链）
# 每局：带窗口营地 3D 0.75，预热 35 s 录 12 s；一行：帧数、区间合计、主通道(p11)片元、深度预通道(p08)片元、归一比值、红字数
# 判法：看「对p08」相对基线；基线至少三局、每变体至少两局，两局对不上不报，<3% 算噪声；帧数明显少或合计异常小的那局作废
# 整批持 mini 测量锁（同一时刻只跑一份测量）；每局用自己的目录、按进程号出表
# Upper-bound measurement for a single shader variant: XT_OWNER=<holder name> var.sh <sequence...>, sequence elements = base or a variant name
# The variant library is in $DM_ROOT/run/xtrace/terr/<name>/b-89f2d5b38edb/ (a full clone with only the modified shader's _b.metallib replaced; the metallib is compiled on the local machine, the second Mac has no compiler toolchain)
# Each run: windowed, camp, 3D 0.75, warm up 35 s and record 12 s; one line: frame count, interval total, main pass (p11) fragment, depth pre-pass (p08) fragment, normalized ratio, number of failure lines
# How to judge: look at 「对p08」(vs p08) relative to the baseline; at least three baseline runs and at least two runs per variant; if the two runs disagree do not report it, and <3% counts as noise; a run with clearly fewer frames or an abnormally small total is void
# The whole batch holds the second Mac's measurement lock (only one measurement runs at a time); each run uses its own directory and produces tables by process id
PN=$(bash "$(dirname "$0")/../../tools/product.sh" name touch); D=${XT_MINI_DIR:-$(ls -dt "${DM_ROOT:-$HOME/dm-work}"/run/mini/*_"$PN" 2>/dev/null | head -1)}; [ -d "$D" ] || { echo "没有 mini 同步目录 ${DM_ROOT:-$HOME/dm-work}/run/mini/*_$PN（先 mini_sync.sh，或用 XT_MINI_DIR 指定）"; exit 2; }; T=$D/src/dm/ios/tools; W=${DM_ROOT:-$HOME/dm-work}/run/xtrace/terr; B=${DM_ROOT:-$HOME/dm-work}/run/xtrace/batch_$(date +%m%d_%H%M%S)_var; mkdir -p $B; O=$B/out.tsv; : > $O
source $T/mlock.sh; mlock_acquire "${XT_OWNER:?先设 XT_OWNER=线名} 着色器变体 $# 局 约 $(( $# * 95 / 60 )) 分钟"; trap mlock_release EXIT
i=0; for v in "$@"; do i=$((i + 1)); R=$B/$(printf %02d $i)_$v
  e="DM_NVN_ENC_LABEL=1"; [ $v = base ] || { [ -d $W/$v/b-89f2d5b38edb ] || { echo "没有变体库 $W/$v"; continue; }; e="$e DM_MSL_DIR=$W/$v"; }
  XT_DIR=$R XT_ENV="$e" XT_ONLY='^metal-gpu-intervals$' XT_TREE=$D/src XT_OUT=$D/out/$PN XT_SAVE=$D/data/switch/save_ref_warlock/0000000000000001 bash $T/xtrace.sh mac camp 'Game Performance' 12 35 > $R.log 2>&1
  red=$(grep -c '【红】' $R/host.err 2>/dev/null)
  python3 $T/xtrace_report.py $R 2>/dev/null | awk -v k=$v -v red=$red '/metal-gpu-intervals/{match($0,/[0-9]+ 帧/); nf=substr($0,RSTART,RLENGTH); match($0,/合计 [0-9.]+/); tot=substr($0,RSTART+7,RLENGTH-7)} / p11 /{if($6=="Fragment")f=$1} / p08 /{if($6=="Fragment")df=$1} END{w=(nf+0>=350 && tot+0>=5 && tot+0<=13)?"":"\t⚠作废（帧数 / 采样量异常：正常约 440 帧、合计 7–10）"; printf "%s\t%s 合计 %s\tp11 片元 %s\tp08 片元 %s\t对p08 %.3f\t红 %s%s\n",k,nf,tot,f,df,(df>0?f/df:0),red,w}' | tee -a $O
  [ -d $R/tables ] && find $R/tables -delete   # 读数已进 out.tsv，导出的表不留（单局约 100 MB） | The readings are already in out.tsv, so the exported tables are not kept (about 100 MB per run)
done; echo "结果 $O"
