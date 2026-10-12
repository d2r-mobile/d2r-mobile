# Switch 版游戏状态 oracle 提取器（真值侧：Ryujinx GDB）。口径对齐 PC M0 插件 v7（src/tools/m0/oracle_plugin.c）：
#   每个服务端拍开始前（拍入口 Game.nss+0x5d3bc0，x0 = pGame；= PC 0x1402c8860，rcx = pGame）取一次：
#   5 类单位表 pGame+{0x2230,0x2630,0x2a30,0x3230,0x2e30}，每类 128 桶、桶内链 next=+0x158；
#   单位 type/cls/id/mode/act(u8 +0x18)/坐标（玩家/怪/飞弹 path+0..+6 含小数）/种子 +0x28/+0x2c；
#   statlist（玩家/怪/物品）unit+0x88 → +0x30 数组、+0x38 项数；物品 ItemData(+0x10) 品质/种子/初始种子/flags；
#   全局种子 pGame+0x1a8/+0x1ac。按（类型, id）排序，各类 FNV-1a 64，输出与插件同格式的 JSONL。
# 偏移出自 PC v7；Switch 94012 静态证据：拍入口结构逐项同、单位表 5 个偏移各 ~260 处立即数、LCG 附近 +0x28 最多。活体复核见 --check。
# 用法：swtick.py [--port 55571] [--ticks N] [--out 文件] [--full]
# Game-state oracle extractor for the Switch version (ground-truth side: Ryujinx GDB). Conventions aligned with the PC M0 plugin v7 (src/tools/m0/oracle_plugin.c):
#   sampled once before each server tick begins (tick entry Game.nss+0x5d3bc0, x0 = pGame; = PC 0x1402c8860, rcx = pGame):
#   5 unit-class tables pGame+{0x2230,0x2630,0x2a30,0x3230,0x2e30}, 128 buckets per class, in-bucket chain next=+0x158;
#   unit type/cls/id/mode/act(u8 +0x18)/coordinates (player/monster/missile path+0..+6 including the fraction)/seeds +0x28/+0x2c;
#   statlist (player/monster/item) unit+0x88 → array at +0x30, entry count at +0x38; item ItemData(+0x10) quality/seed/initial seed/flags;
#   global seeds pGame+0x1a8/+0x1ac. Sorted by (type, id), FNV-1a 64 per class, output as JSONL in the same format as the plugin.
# Offsets come from PC v7; Switch 94012 static evidence: the tick entry structure matches item by item, each of the 5 unit-table offsets appears as an immediate ~260 times, and +0x28 is the most frequent near the LCG. For the live re-check see --check.
# Usage: swtick.py [--port 55571] [--ticks N] [--out file] [--full]
import sys, os, struct, json, argparse, time
sys.path.insert(0, os.path.dirname(__file__))
from swgdb import Gdb

TICK_ENTRY = 0x5d3bc0
TBASE = (0x2230, 0x2630, 0x2a30, 0x3230, 0x2e30)
NB, NEXT, MAXS = 128, 0x158, 96

def fnv(h, b):
    for c in b: h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

class Reader:
    def __init__(self, g): self.g = g; self.n = 0
    def rd(self, a, n):
        if not a: return None
        self.n += 1
        try: return self.g.read(a, n)
        except IOError: return None

def units(r, game):
    tab = r.rd(game + TBASE[0], 0x1400)            # 0x2230..0x3630 连续：5 类 × 128 桶 × 8 字节 | 0x2230..0x3630 contiguous: 5 classes × 128 buckets × 8 bytes
    out = []; bad = 0
    for t, base in enumerate(TBASE):
        for b in range(NB):
            u = struct.unpack_from('<Q', tab, base - TBASE[0] + 8 * b)[0]
            k = 0
            while u and k < 4096:
                hd = r.rd(u, NEXT + 8)
                if hd is None: bad += 1; break
                ty, cls, uid, mode = struct.unpack_from('<IIII', hd, 0)
                x = {'type': ty, 'cls': cls, 'id': uid, 'mode': mode, 'act': hd[0x18], 'seed': struct.unpack_from('<I', hd, 0x30)[0],
                     'rlo': struct.unpack_from('<I', hd, 0x28)[0], 'rhi': struct.unpack_from('<I', hd, 0x2c)[0],
                     'x': 0, 'y': 0, 'xf': 0, 'yf': 0, 'it': None, 'st': [], 'ptr': u, 'path': struct.unpack_from('<Q', hd, 0x38)[0]}
                if t == 4:
                    d = struct.unpack_from('<Q', hd, 0x10)[0]; idt = r.rd(d, 0x1c)
                    if idt: x['it'] = struct.unpack_from('<IIIII', idt, 0)[0:3] + (struct.unpack_from('<I', idt, 0x18)[0], struct.unpack_from('<I', idt, 0x10)[0])
                if t in (0, 1, 3):
                    p = r.rd(struct.unpack_from('<Q', hd, 0x38)[0], 8)
                    if p: x['xf'], x['x'], x['yf'], x['y'] = struct.unpack('<HHHH', p)
                sl = struct.unpack_from('<Q', hd, 0x88)[0]
                slh = r.rd(sl, 0x40)
                if slh:
                    sb, sc = struct.unpack_from('<Q', slh, 0x30)[0], struct.unpack_from('<I', slh, 0x38)[0]
                    arr = r.rd(sb, 8 * min(sc, MAXS)) if sb and sc else None
                    if arr: x['st'] = [struct.unpack_from('<II', arr, 8 * j) for j in range(len(arr) // 8)]
                if ty != t: bad += 1
                out.append(x); k += 1
                u = struct.unpack_from('<Q', hd, NEXT)[0]
    return out, bad

def tick_line(r, game):
    tick = struct.unpack('<I', r.rd(game + 0x170, 4))[0]
    us, bad = units(r, game)
    us.sort(key=lambda u: (u['type'], u['id']))
    h = [0xcbf29ce484222325] * 6
    gs = r.rd(game + 0x1a8, 8) or bytes(8); h[5] = fnv(h[5], gs)
    cnt = [0] * 5
    for u in us:
        t = u['type']
        if t >= 5: continue
        cnt[t] += 1
        hh = fnv(h[t], struct.pack('<9I', t, u['cls'], u['id'], u['mode'], u['act'], u['x'], u['y'], u['xf'], u['yf']))
        hh = fnv(hh, struct.pack('<II', u['rlo'], u['rhi']))
        if t == 4: hh = fnv(hh, struct.pack('<5I', *(u['it'] or (0,) * 5)))
        if t <= 1 or t == 4:
            hh = fnv(hh, struct.pack('<I', len(u['st'])))
            for a, b in u['st']: hh = fnv(hh, struct.pack('<II', a, b))
        h[t] = hh
    return {'tick': tick, 'n': cnt, 'h': ['%016x' % x for x in h], 'bad': bad, 'reads': r.n}, us

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=55571); ap.add_argument('--ticks', type=int, default=25)
    ap.add_argument('--out', default='-'); ap.add_argument('--full', action='store_true')
    A = ap.parse_args()
    g = Gdb(port=A.port)
    mods, info = g.modules()
    game_mod = next((v for k, v in mods.items() if k.startswith('Game')), None)
    if not game_mod: sys.exit('模块表里没有 Game.nss：\n' + info)
    bp = game_mod[0] + TICK_ENTRY
    g.interrupt()
    if not g.set_bp(bp): sys.exit(f'下断失败 {bp:#x}')
    f = sys.stdout if A.out == '-' else open(A.out, 'w')
    t0 = time.time()
    try:
        last = None; dup = 0; gaps = []
        while True:
            g.cont(); stop = g.wait_stop(timeout=60)
            k = 0
            while g.reg(32) != bp:                    # 不在拍入口的停止：继续等真命中 | A stop that is not at the tick entry: keep waiting for a real hit
                k += 1
                if k > 50: sys.exit(f'连续 50 次停在非拍入口 {g.reg(32):#x}')
                g.cont(); stop = g.wait_stop(timeout=60)
            game = g.reg(0)
            tick = int.from_bytes(g.read(game + 0x170, 4), 'little')
            if tick == last:                          # 同一次命中被重复报告（Ryujinx）：不重复取 | The same hit reported twice (Ryujinx): do not sample it again
                dup += 1; g.step_over(bp, stop); continue
            if last is not None and tick != last + 1: gaps.append((last, tick))
            r = Reader(g)
            line, us = tick_line(r, game)
            line['game'] = hex(game); line['dt'] = round(time.time() - t0, 3)
            f.write(json.dumps(line) + '\n'); f.flush()
            if A.full:
                for u in us: f.write(json.dumps({'t': line['tick'], 'u': u}) + '\n')
            last = tick
            g.step_over(bp, stop)                     # 越过入口断点，否则继续后又停在同一拍 | Step over the entry breakpoint, otherwise after continuing it stops at the same tick again
            A.ticks -= 1
            if A.ticks <= 0: break
        print(json.dumps({'重复报告': dup, '断档': gaps}), file=sys.stderr)
        if gaps: sys.exit(f'拍号不连续：{gaps[:5]}')
    finally:
        g.clear_bp(bp); g.cont(); g.close()
