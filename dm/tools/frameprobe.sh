#!/bin/bash
# 帧稳 Mac 对照（mini 上跑）：产品节奏（不开 DM_DETCLOCK，display 模式 60 Hz）回放营地 / 野外，逐帧读 nvn.tsv，
# 统计局内 CPU 帧（出帧工作 − 取图等待 − 出图队列等待）超 16.7 ms 的次数，并按 上传 / 建管线建库 / 其它 归因。
# 用法：frameprobe.sh <camp1|wild1> <局号>   环境：MINI_ROOT、DM_OUT（mini_sync.sh 的目录）
# Frame-stability Mac comparison (run on the second Mac): replays camp / wilderness at product pacing (DM_DETCLOCK off, display mode 60 Hz), reads nvn.tsv frame by frame,
# counts how many in-run CPU frames (present work − image acquire wait − present queue wait) exceed 16.7 ms, and attributes them to upload / pipeline and library creation / other.
# Usage: frameprobe.sh <camp1|wild1> <run number>   Environment: MINI_ROOT, DM_OUT (the directory of mini_sync.sh)
set -u
scene=$1; k=$2; mini=${MINI_ROOT:?}; out=${DM_OUT:-$mini/out/$(bash "$(dirname "$0")/product.sh" name keypage)}; bin=$mini/src/dm/host/dmhost
tr=$mini/data/switch/truth_runs/$scene; run=$mini/frameprobe/${scene}_$k; rm -rf "$run"; mkdir -p "$run"
cp -R "$mini/data/switch/save_ref_warlock/0000000000000001/0" "$run/save0"
if [ "$scene" = camp1 ]; then c2s=$tr/c2s_to_E.jsonl; n=122; keys='A@OpenPanel:MainMenuButtonRibbon>FrontEndNavigation:ToGame'
else c2s=$tr/c2s_to_arrive.jsonl; n=215; keys='A@OpenPanel:MainMenuButtonRibbon>FrontEndNavigation:ToGame,A@DM:Tick:133#>OpenPanel:WaypointsPanelExpansion,DOWN@DM:Tick:142#,A@DM:Tick:150#>ClosePanel:WaypointsPanelExpansion'; fi
(cd "$(dirname "$bin")" && timeout 110 env DM_SAVE="$run/save0" DM_GAME_SEED=0x12345678 DM_FIXED_TIME=1790000000 DM_TICKS_N=$((n+1)) \
  DM_C2S_REPLAY="$c2s" DM_TICKS="$run/ticks.jsonl" DM_KEYS="$keys" DM_PRESENT_HZ=60 DM_PRESENT_INTERVAL=display DM_UIMSG=1 \
  DM_NVN_TSV="$run/nvn.tsv" DM_NVN_FRAMES=0 ./dmhost "$out" main sdk > "$run/host.log" 2>&1); echo "rc=$?"
python3 - "$run" "$scene" "$k" <<'PY'
import re, sys, collections
run, scene, k = sys.argv[1:]
iv, o, first = [], None, None
for l in open(run + "/host.log", errors="replace"):
    m = re.match(r"\[dm\] UIMSG \[([\d.]+)s\] PanelManager (OpenPanel|UnloadPanel) LoadScreenPanel", l)
    if not m: continue
    t = float(m.group(1))
    if m.group(2) == "OpenPanel": o = t; first = t if first is None else first
    elif o is not None: iv.append((o, t)); o = None
if o is not None: iv.append((o, 1e9))
F = collections.defaultdict(dict)
for l in open(run + "/nvn.tsv"):
    p = l.rstrip("\n").split("\t")
    if len(p) == 3 and p[0].isdigit():
        try: F[int(p[0])][p[1]] = float(p[2])
        except ValueError: pass
game = [f for f, d in sorted(F.items()) if "@t_s" in d and first is not None and d["@t_s"] > first and not any(a <= d["@t_s"] <= b for a, b in iv)]
over = collections.Counter(); worst = []
for f in game:
    d = F[f]; cpu = d.get("@work_ms", 0) - d.get("@wait_ms", 0) - d.get("@pq_wait_ms", 0)
    if cpu > 16.7:
        src = "上传" if d.get("@upload_ms", 0) > 4 else "建管线建库" if d.get("@pso_new", 0) + d.get("@lib_new", 0) > 0 else "其它"
        over[src] += 1; worst.append((round(cpu, 1), f, src, round(d.get("@upload_ms", 0), 1), int(d.get("@pso_new", 0) + d.get("@lib_new", 0)), round(d.get("@sub_cpu_ms", 0), 1)))
cpus = sorted(F[f].get("@work_ms", 0) - F[f].get("@wait_ms", 0) - F[f].get("@pq_wait_ms", 0) for f in game)
p99 = cpus[int(0.99 * (len(cpus) - 1))] if cpus else 0
print(f"FRAMEPROBE {scene} 局{k} 局内帧 {len(game)} 超16.7ms {sum(over.values())} {dict(over)} CPU中位 {cpus[len(cpus)//2] if cpus else 0:.1f} p99 {p99:.1f} 读取撤下 {len(iv)}")
for w in sorted(worst, reverse=True)[:5]: print("  最慢", w, "（CPU ms, 帧, 归因, 上传ms, 新建管线库, 提交CPU ms）")
PY
