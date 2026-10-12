#!/usr/bin/env python3
# xtrace.sh 导出表的读数汇总。用法：xtrace_report.py <导出目录（含 tables/）> [进程名=DmIOS]
# xctrace 导出的 XML 里同一个值第二次出现只写 ref="<id>"，这里先按 id 还原再取数。时间单位：纳秒。
# Summary of readings from the tables exported by xtrace.sh. Usage: xtrace_report.py <export dir (containing tables/)> [process name=DmIOS]
# In the XML exported by xctrace, the second occurrence of the same value is written only as ref="<id>"; here values are first restored by id and then read. Time unit: nanoseconds.
import sys, os, re, collections, statistics as st
import xml.etree.ElementTree as ET
D = sys.argv[1]
# 第二参数：进程号（推荐）或进程名。不给时读 <目录>/pid（xtrace.sh mac 写的）。按进程名过滤会把同名的别人那局混进来（mini 上三个任务同时量，读数作废）。
# Second argument: process id (recommended) or process name. If not given, read <dir>/pid (written by xtrace.sh mac). Filtering by process name mixes in someone else's same-named run (three jobs measuring at once on the second Mac, readings void).
PROC = sys.argv[2] if len(sys.argv) > 2 else (open(os.path.join(D, 'pid')).read().strip() if os.path.exists(os.path.join(D, 'pid')) else 'DmIOS')
def isme(s): return (f'({PROC})' in s or f'pid: {PROC}' in s) if PROC.isdigit() else PROC in s

def rows(schema):
    p = os.path.join(D, 'tables', schema + '.xml')
    if not os.path.exists(p) or os.path.getsize(p) == 0: return None, []
    root = ET.parse(p).getroot(); node = root.find('node'); cols = [c.findtext('mnemonic') for c in node.find('schema').findall('col')]
    ids = {}; out = []
    def val(e):
        if 'ref' in e.attrib: return ids.get(e.attrib['ref'])
        v = {'fmt': e.attrib.get('fmt', ''), 'text': (e.text or '').strip(), 'tag': e.tag, 'sub': {c.tag: val(c) for c in e}}
        if 'id' in e.attrib: ids[e.attrib['id']] = v
        return v
    for r in node.findall('row'):
        vs = [None if c.tag == 'sentinel' else val(c) for c in r]; out.append(dict(zip(cols, vs)))
    return cols, out
num = lambda v: float(v['text']) if v and v['text'] not in ('', None) else None
fmt = lambda v: v['fmt'] if v else ''
def pct(a, q): a = sorted(a); return a[min(len(a) - 1, int(len(a) * q))] if a else float('nan')
def dist(name, a, unit=1e6, u='ms'):
    a = [x / unit for x in a if x is not None]
    print(f'{name}：{len(a)} 个；' + (f'均值 {st.mean(a):.2f}、p50 {pct(a, .5):.2f}、p95 {pct(a, .95):.2f}、最大 {max(a):.2f} {u}' if a else '无数据'))
def mine(r):   # 这一行是否属于被测进程（有 process / thread 列的表） | Whether this row belongs to the process under test (for tables with process / thread columns)
    for k in ('process', 'thread'):
        if r.get(k) and isme(fmt(r[k])): return True
    return not any(r.get(k) for k in ('process', 'thread'))

print(f'# {D}')
for s, col, label in (('ca-client-buffer-wait-interval', 'duration', '等下一张 drawable'), ('display-vsyncs-interval', 'duration', '垂直同步间隔'),
                      ('displayed-surfaces-interval', 'duration', '每张上屏画面的显示时长'), ('display-compositor-interval', 'duration', '合成器区间')):
    c, R = rows(s)
    if c is None: print(f'{label}：没有表 {s}'); continue
    dist(f'{label}（{s}）', [num(r[col]) for r in R if mine(r)])
c, R = rows('metal-gpu-intervals')   # GPU 上每个编码器的执行区间：按「第几个编码器 × 顶点 / 片元 / 计算」归并，除以帧数得每帧毫秒 | Execution intervals of each encoder on the GPU: merged by 'encoder index × vertex / fragment / compute', divided by the frame count to get milliseconds per frame
if c:
    g = collections.defaultdict(float); n = collections.Counter(); frames = set(); tot = 0
    for r in R:
        lab = fmt(r.get('event-label'))
        if not isme(lab) or 'Command' not in lab: continue
        k = (re.sub(r'^Command Buffer \d+:|\s*\(.*$', '', lab).strip(), fmt(r.get('channel-name'))); d = num(r.get('duration')) or 0
        g[k] += d; n[k] += 1; tot += d; frames.add(fmt(r.get('frame-number')))
    nf = max(len(frames), 1); print(f'## metal-gpu-intervals：{len(R)} 行，{nf} 帧；本进程各编码器 GPU 区间合计 {tot / nf / 1e6:.2f} ms/帧（区间可并行重叠，是「活的份额」不是墙钟）')
    for k, v in sorted(g.items(), key=lambda kv: -kv[1])[:40]: print(f'  {v / nf / 1e6:7.3f} ms/帧  {v / tot * 100:5.1f}%  {n[k] / nf:4.1f} 段/帧  {k[1]:8s} {k[0]}')
    ch = collections.Counter()
    for k, v in g.items(): ch[k[1]] += v
    print('  按阶段：' + '，'.join(f'{k} {v / tot * 100:.1f}%' for k, v in ch.most_common()))
for s in ('metal-application-encoders-list', 'metal-driver-event-intervals', 'metal-application-intervals'):
    c, R = rows(s)
    if c is None: print(f'没有表 {s}'); continue
    R = [r for r in R if mine(r)]; key = next((k for k in ('encoder-label', 'event-label', 'label', 'state', 'event-type', 'channel-name') if k in c), None)
    g = collections.defaultdict(list)
    for r in R: g[fmt(r.get(key)) if key else '全部'].append(num(r.get('duration')) or 0)
    tot = sum(sum(v) for v in g.values()); print(f'## {s}：{len(R)} 行，按「{key}」分组，总时长 {tot / 1e6:.1f} ms，列 {c}')
    for k, v in sorted(g.items(), key=lambda kv: -sum(kv[1]))[:25]: print(f'  {sum(v) / 1e6:9.2f} ms  {len(v):6d} 次  均 {st.mean(v) / 1e3:8.1f} µs  {k[:70]}')
for s, key in (('gpu-performance-state-intervals', 'gpu-performance-state'), ('device-thermal-state-intervals', 'thermal-state'), ('metal-gpu-state-intervals', 'state')):
    c, R = rows(s)
    if c is None: print(f'没有表 {s}'); continue
    g = collections.Counter()
    for r in R: g[(fmt(r.get(key)), fmt(r.get('track-label')) if 'track-label' in c else '')] += num(r.get('duration')) or 0
    tot = sum(g.values()) or 1; print(f'## {s}：{len(R)} 段'); [print(f'  {v / tot * 100:5.1f}%  {v / 1e9:7.2f} s  {k}') for k, v in g.most_common(12)]
c, R = rows('gpu-shader-profiler-interval') if os.path.exists(os.path.join(D, 'tables', 'gpu-shader-profiler-interval.xml')) else (None, [])
if c:
    print(f'## gpu-shader-profiler-interval：{len(R)} 行，列 {c}'); key = next((k for k in ('shader-name', 'label', 'name', 'function', 'shader') if k in c), c[2] if len(c) > 2 else None)
    g = collections.Counter()
    for r in R: g[fmt(r.get(key))] += num(r.get('duration')) or 0
    tot = sum(g.values()) or 1; [print(f'  {v / tot * 100:5.1f}%  {v / 1e6:9.2f} ms  {k[:80]}') for k, v in g.most_common(30)]
c, R = rows('context-switch')   # 各线程在哪类核上跑、被抢占多少次 | Which kind of core each thread runs on and how many times it is preempted
if c:
    run = collections.defaultdict(collections.Counter); ev = collections.defaultdict(collections.Counter); last = {}
    for r in R:
        if not r.get('thread') or not isme(fmt(r['thread'])): continue
        th = fmt(r['thread']).split(' (')[0]; t = num(r['time']); e = fmt(r.get('event')); ev[th][e] += 1
        if th in last and last[th][1]: run[th][last[th][1]] += t - last[th][0]
        core = fmt(r.get('cpu')); last[th] = (t, ('P' if 'P Core' in core else 'E' if 'E Core' in core else '?') if e == 'Running' else None)
    print(f'## context-switch：{len(R)} 行；各线程在性能核 / 能效核上的运行时间与调度事件')
    for th, cnt in sorted(run.items(), key=lambda kv: -sum(kv[1].values()))[:14]:
        tot = sum(cnt.values()); print(f'  {tot / 1e6:9.1f} ms  P {cnt["P"] / tot * 100:5.1f}%  E {cnt["E"] / tot * 100:5.1f}%  事件 {dict(ev[th].most_common(4))}  {th[:40]}')
for s in ('graphics-compiler-activity-intervals', 'metal-command-buffer-error', 'hang-risks', 'graphics-compiler-spill-events'):
    c, R = rows(s); print(f'{s}：' + ('没有这张表' if c is None else f'{len(R)} 行' + (f'，总时长 {sum(num(r.get("duration")) or 0 for r in R) / 1e6:.1f} ms' if c and 'duration' in c else '')))
