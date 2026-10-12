# 全线程采样（DM_SAMPLE=毫秒:*）汇总：按时间窗、可选线程，统计各线程在跑占比，及游戏函数（eh_frame 起点）自身 / 含子调用热点。
# 用法：sampagg.py <日志> <起s> <止s> [线程句柄 0x…]   需要 build/cache/dm/out/3.3.94012-r2/{main,sdk}.orig.bin（原映像，按偏移解函数）
#   不给线程：列各线程样本数、在跑占比、自身热点前 3
#   给线程：该线程在跑样本的自身热点、宿主帧、含子调用热点（等待类样本 = 最内层在系统等待函数，不计入在跑）
# All-thread sampling (DM_SAMPLE=ms:*) summary: for a time window and an optional thread, reports each thread's running share and the self / inclusive hotspots of game functions (eh_frame starts).
# Usage: sampagg.py <log> <start s> <end s> [thread handle 0x...]   Requires build/cache/dm/out/3.3.94012-r2/{main,sdk}.orig.bin (original images, functions resolved by offset)
#   No thread given: lists each thread's sample count, running share and top 3 self hotspots
#   Thread given: that thread's self hotspots among running samples, host frames, and inclusive hotspots (waiting samples = innermost frame is in a system wait function, not counted as running)
import sys, re, bisect, subprocess, collections, os
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
log, lo, hi = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]); thr = int(sys.argv[4], 16) if len(sys.argv) > 4 else None
ORIG = os.path.join(DM_ROOT, 'build/cache/dm/out/3.3.94012-r2')
SDIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sdis.py')
st = {}
for m in ('main', 'sdk'):
    sys.argv = ['x', f'{ORIG}/{m}.orig.bin', '--find', 'x']; g = {}
    exec(open(SDIS).read().split("if sys.argv[2] == '--sym'")[0], g); st[m] = (g['fn_starts'](), g['name'])
WAIT = ('mach_wait_until', '__psynch_cvwait', '__semwait_signal', 'nanosleep', 'mach_msg2_trap', 'swtch_pri', '__psynch_mutexwait', 'block_inner', '__ulock_wait')
lines = open(log, encoding='utf-8', errors='replace').read().split('\n')
def fnof(f):
    mm = re.match(r'(main|sdk)\+0x([0-9a-f]+)', f)
    if not mm: return None
    s, _ = st[mm.group(1)]; off = int(mm.group(2), 16); return f'{mm.group(1)}+{s[bisect.bisect_right(s, off) - 1]:#x}'
def dm(fn):
    mod, a = fn.split('+'); r = st[mod][1](int(a, 16))
    return subprocess.run(['xcrun', 'c++filt', r], capture_output=True, text=True).stdout.strip()[:70]
per = collections.defaultdict(lambda: [0, 0, collections.Counter()]); selfc = collections.Counter(); incl = collections.Counter(); host = collections.Counter(); waitat = collections.Counter(); n = 0
for i, l in enumerate(lines):
    m = re.match(r'\[dm\] SAMPLE #\d+ \[([\d.]+)s\] thr=(0x[0-9a-f]+) pc=(\S+)', l)
    if not m or not (lo <= float(m.group(1)) <= hi): continue
    h = int(m.group(2), 16); pc = m.group(3); p = per[h]
    bt = lines[i + 1].split('bt:')[1].split() if i + 1 < len(lines) and 'bt:' in lines[i + 1] else []
    if any(pc.startswith(w) for w in WAIT):
        p[1] += 1
        if thr is not None and h == thr:                 # 等待点：回溯里第一个游戏函数（含 sdk 同步原语时再往外找一层 main 函数） | Wait point: the first game function in the backtrace (when it contains an sdk synchronization primitive, look one more level out for a main function)
            fs = [fnof(f) for f in bt if fnof(f)]; mf = next((f for f in fs if f.startswith('main')), None)
            waitat[(fs[0] if fs else '?') + ' ← ' + (mf or '?')] += 1
        continue
    p[0] += 1; frames = [pc] + bt
    first = next((fnof(f) for f in frames if fnof(f)), None)
    p[2][first or '(宿主) ' + pc.split('+')[0]] += 1
    if thr is None or h != thr: continue
    n += 1
    if not fnof(pc): host[pc.split('+')[0]] += 1
    if first: selfc[first] += 1
    seen = set()
    for f in frames:
        fn = fnof(f)
        if fn and fn not in seen: incl[fn] += 1; seen.add(fn)
if thr is None:
    for h, (run, wait, c) in sorted(per.items(), key=lambda x: -x[1][0]):
        tot = run + wait
        if tot: print(f'{h:#x} 样本 {tot:5d} 在跑 {100 * run / tot:5.1f}%  热点: ' + '; '.join(f'{fn} {k}' for fn, k in c.most_common(3)))
    sys.exit(0)
run, wait, _ = per[thr]
print(f'线程 {thr:#x} 窗口 [{lo}, {hi}]：样本 {run + wait}，在跑 {run}（{100 * run / max(1, run + wait):.1f}%）')
print('== 自身（最内层游戏函数，占在跑样本）'); [print(f'{c:4d} {100 * c / max(1, n):5.1f}% {fn} {dm(fn)}') for fn, c in selfc.most_common(12)]
print('== 宿主帧（最内层非游戏）'); [print(f'{c:4d} {h}') for h, c in host.most_common(6)]
print('== 含子调用'); [print(f'{c:4d} {100 * c / max(1, n):5.1f}% {fn} {dm(fn)}') for fn, c in incl.most_common(20)]
print('== 等待点（等待样本：最内层游戏函数 ← 最内层 main 函数）'); [print(f'{c:4d} {k}') for k, c in waitat.most_common(8)]
