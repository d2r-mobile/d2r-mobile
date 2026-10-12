#!/bin/bash
# 真机启动时间线（DM_BOOTPROF ⓪–⑥ 分段）：冷启动 1 局（重装后首启：代码页冷；RomFS 可能仍在系统文件缓存里——真冷要重启手机，不做）+ 热启动 N 局。
# 用法：[DM_OUT=产物] dm/ios/tools/bootprof.sh [热局数=3] 或 --metal-cold [局数=3]
# Real-device boot timeline (DM_BOOTPROF segments ⓪–⑥): 1 cold-start run (first launch after reinstall: code pages cold; the RomFS may still be in the system file cache - truly cold needs a phone reboot, not done) + N warm-start runs.
# Usage: [DM_OUT=build output] dm/ios/tools/bootprof.sh [warm runs=3] or --metal-cold [runs=3]
set -u
cd "$(dirname "$0")/.."
METAL_COLD=0
if [ "${1:-}" = "--metal-cold" ]; then METAL_COLD=1; N=${2:-3}; else N=${1:-3}; fi
source tools/local_env.sh && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}; RUN_ROOT=${DM_ROOT:-$HOME/dm-work}/run/ios; R=$RUN_ROOT/bootprof; mkdir -p $R; rm -f $R/*.log
t(){ echo "[$(date +%T)] $*"; }
INGAME_KEYS='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame'   # 同 smoke.sh | Same as smoke.sh
if [ "$METAL_COLD" -eq 1 ]; then
  case "$N" in ''|*[!0-9]*|0) t "🔴 局数必须是正整数"; exit 1;; esac
fi
source tools/devlock.sh
devlock_acquire "bootprof"
trap devlock_release EXIT
./run.sh build > $R/build.log 2>&1 || { t "🔴 构建失败"; tail -5 $R/build.log; exit 1; }
./run.sh install >> $R/build.log 2>&1 || { t "🔴 装机失败"; tail -5 $R/build.log; exit 1; }
if [ "$METAL_COLD" -eq 1 ]; then
  for kind in cold hot; do
    for k in $(seq 1 $N); do
      name=${kind}${k}; cold_env=()
      [ "$kind" = cold ] && cold_env=(DEVICECTL_CHILD_DM_METAL_COLD=1)
      # 冷 / 热都按冒烟同一键序进局（标准存档新副本），量「进局读取」= ToGame → 读取画面撤下（冷缓存下预热编管线会不会拉长读取）
      # Cold / warm both enter the run with the same key sequence as the smoke test (fresh copy of the standard save), measuring "run-entry loading" = ToGame → loading screen removed (whether warm-up pipeline compilation under a cold cache lengthens loading)
      env -u DEVICECTL_CHILD_DM_METAL_COLD "${cold_env[@]}" DEVICECTL_CHILD_DM_SAVE_FRESH=warlock DEVICECTL_CHILD_DM_KEYS="$INGAME_KEYS" DEVICECTL_CHILD_DM_UIMSG=1 DEVICECTL_CHILD_DM_BOOTPROF=1 DEVICECTL_CHILD_DM_WATCH=40 ./run.sh boot 60 > $R/$name.run 2>&1 || { t "🔴 $name 启动失败"; exit 1; }
      cp $RUN_ROOT/out/boot.txt $R/$name.log || { t "🔴 $name 日志读取失败"; exit 1; }
      grep -q "⑥" $R/$name.log || t "🔴 $name 没出分段表（看 $R/$name.log）"
    done
  done
else
  for k in $(seq 0 $N); do
    name=hot$k; [ $k = 0 ] && name=cold
    DEVICECTL_CHILD_DM_SAVE_FRESH=1 DEVICECTL_CHILD_DM_UIMSG=1 DEVICECTL_CHILD_DM_BOOTPROF=1 DEVICECTL_CHILD_DM_WATCH=8 ./run.sh boot 40 > $R/$name.run 2>&1
    cp $RUN_ROOT/out/boot.txt $R/$name.log
    grep -q "⑥" $R/$name.log || t "🔴 $name 没出分段表（看 $R/$name.log）"
  done
fi
python3 - $R $N $METAL_COLD <<'PY'
import sys, re, statistics
R, N, metal_flag = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
metal_cold = metal_flag == 1
names = ([f'cold{k}' for k in range(1, N + 1)] + [f'hot{k}' for k in range(1, N + 1)]) if metal_cold else ['cold'] + [f'hot{k}' for k in range(1, N + 1)]
seg = {}
metal = {}
cleared = set()
errors = []
togame = {}; ingame = {}
for n in names:
    for l in open(f'{R}/{n}.log', errors='replace'):
        m = re.match(r'\[dm\] ([⓪①②③④⑤⑥]b?) (.+?)\s{2,}([\d.]+)\s+([\d.]+)', l)
        if m: seg.setdefault((m[1], m[2].strip()), {})[n] = (float(m[3]), float(m[4]))
        m = re.search(r'METALCACHE 现存 (\d+) 个 (\d+) 字节', l)   # 每个目标目录一行，按局加总（只取最后一行会把旧位置的 0 当成全量，热局误报 0）
        if m and n not in cleared: f0, b0 = metal.get(n, (0, 0)); metal[n] = (f0 + int(m[1]), b0 + int(m[2]))
        m = re.search(r'METALCACHE 已清 (\d+) 个 (\d+) 字节 → 现存 (\d+) 个', l)
        if m:
            metal[n] = (int(m[1]), int(m[2]))
            if int(m[3]) == 0: cleared.add(n)
            else: errors.append(f'{n} 清后文件数 {m[3]} 越线（要求 0）')
        if metal_cold and 'METALCACHE 【红】' in l: errors.append(f'{n}: {l.strip()}')
        m = re.search(r'UIMSG \[([\d.]+)s\] FrontEndNavigation ToGame', l)
        if m and n not in togame: togame[n] = float(m[1])
        m = re.search(r'UIMSG \[([\d.]+)s\] PanelManager (?:UnloadPanel|ClosePanel) LoadScreenPanel', l)
        if m and n in togame and n not in ingame: ingame[n] = float(m[1]) - togame[n]
print('段 | ' + ' | '.join(names) + '（各段用时 s，括号内累计）')
for (k, name), v in seg.items():
    print(f'{k} {name} | ' + ' | '.join(f'{v[n][0]:.3f}（{v[n][1]:.3f}）' if n in v else '—' for n in names))
for n in names:
    tot = max((v[n][1] for v in seg.values() if n in v), default=0)
    d4 = next((v[n][0] for (k, _), v in seg.items() if k == '④' and n in v), 0); d5 = next((v[n][0] for (k, _), v in seg.items() if k == '⑤' and n in v), 0)
    print(f'{n}：到 Ribbon {tot:.3f} s；④ {d4:.3f} s（{d4 / tot * 100 if tot else 0:.0f}%）⑤ {d5:.3f} s（{d5 / tot * 100 if tot else 0:.0f}%）')
if metal_cold:
    for n in names:
        present = {k for (k, _), values in seg.items() if n in values}
        missing = set('⓪①②③④⑤⑥') - present
        if missing: errors.append(f'{n} 缺分段：{"".join(sorted(missing))}')
        if n not in metal: errors.append(f'{n} 缺 METALCACHE 读数')
        else: print(f'{n} METALCACHE 删前/启动前：{metal[n][0]} 个 {metal[n][1]} 字节')
        if n.startswith('cold') and n not in cleared: errors.append(f'{n} 未确认清零')
        if n not in ingame: errors.append(f'{n} 缺进局读取读数（ToGame / 读取画面撤下）')
        else: print(f'{n} 进局读取（ToGame → 读取画面撤下）{ingame[n]:.2f} s')
    if errors:
        for error in errors: print(f'🔴 {error}', file=sys.stderr)
        sys.exit(1)
    for kind in ('cold', 'hot'):
        group = [n for n in names if n.startswith(kind)]
        totals = [max((v[n][1] for v in seg.values() if n in v), default=0) for n in group]
        print(f'{kind} 进局读取中位数：{statistics.median([ingame[n] for n in group]):.2f} s')
        print(f'{kind} 中位数（{len(group)}/{N} 局）：总用时 {statistics.median(totals):.3f} s；METALCACHE 删前/启动前 {statistics.median([metal[n][0] for n in group]):g} 个 {statistics.median([metal[n][1] for n in group]):g} 字节')
        for (key, label), values in seg.items():
            duration = [values[n][0] for n in group if n in values]
            cumulative = [values[n][1] for n in group if n in values]
            if len(duration) != N:
                print(f'🔴 {kind} {key} 分段覆盖 {len(duration)}/{N}', file=sys.stderr); sys.exit(1)
            print(f'{kind} {key} {label} 中位数：{statistics.median(duration):.3f}（{statistics.median(cumulative):.3f}）')
PY
