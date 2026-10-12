#!/usr/bin/env python3
# 每帧附件带宽预算表（目标 120 帧、≤ 20 GB/s → 每帧 ≤ 170 MB）：从 DM_NVN_PASSDUMP 的一帧数据流按通道列读入 / 写回 MB，
# 按两者之和从大到小排。读入按 P 行 load 计（clear / dontcare 不计）；写回按 P 行的附件计、扣掉 Z 行判 dontcare 的（写回前瞻）。
# 合一深度模板格式（fmt 260 = Depth32Float_Stencil8）按深度 4 + 模板 1 字节拆：d 行记深度平面，s 行（只挂模板）记模板平面。
# 纹理采样读取（补列，粗估）：
#   全屏类通道（绘制顶点数 ≤ 6 且绘制 ≤ 3 次；绘制次数取 PASSDUMP_ALL 的 d 行，没有就取片元程序绑定行数）：每个被采样的绑定视图按 min(绑定视图整张字节, 目标像素数 × 每纹素字节)；
#   几何类通道：目标像素数 × 通道内片元着色器声明的贴图槽数（取最大）× 被采样贴图的平均每纹素字节（每像素每槽约采一次，材质贴图有 mip）。
#   贴图格式与尺寸取同帧同通道每个描述符的 B 行（同一序号可绑定不同级 / 层的视图），查不到按 4 字节；槽数取 B 库反射 json。
# 用法：tbdr_budget.py <含 PASSDUMP 的日志> [帧号（缺省取第一帧）] [预算 MB=170] [--tsv=全量表路径]
# TSV 不截前 25 名 / 着色器键；带逐次绘制来源、未知贴图分母与合并通道备注。纹理列始终是绑定模型粗估，不是外存实测。
# Per-frame attachment bandwidth budget table (target 120 fps, ≤ 20 GB/s → ≤ 170 MB per frame): from one frame of the DM_NVN_PASSDUMP data stream, list load / store MB per pass,
# sorted by their sum in descending order. Load is counted from P rows with load (clear / dontcare not counted); store is counted from the P rows' attachments, minus those the Z rows judge dontcare (store lookahead).
# Combined depth-stencil formats (fmt 260 = Depth32Float_Stencil8) are split as depth 4 + stencil 1 bytes: d rows record the depth plane, s rows (stencil-only attachment) record the stencil plane.
# Texture sampling reads (supplementary column, rough estimate):
#   fullscreen-class passes (draw vertex count ≤ 6 and ≤ 3 draws; draw count from the d rows of PASSDUMP_ALL, or else the number of fragment program binding rows): for each sampled bound view, min(whole bound view bytes, target pixel count × bytes per texel);
#   geometry-class passes: target pixel count × number of texture slots declared by the fragment shaders in the pass (take the max) × average bytes per texel of the sampled textures (about one sample per pixel per slot; material textures have mips).
#   Texture format and size come from the B row of each descriptor in the same frame and pass (the same serial can bind views of different levels / layers); 4 bytes if not found; slot counts come from the B library reflection json.
# Usage: tbdr_budget.py <log containing PASSDUMP> [frame number (default: first frame)] [budget MB=170] [--tsv=path of the full table]
# The TSV is not truncated to the top 25 / shader keys; it carries per-draw sources, the unknown-texture denominator and merged-pass notes. The texture column is always a rough binding-model estimate, not an external-memory measurement.
import re, sys, collections
BPP = {70: 4, 71: 4, 80: 4, 81: 4, 90: 4, 92: 4, 93: 4, 115: 8, 125: 16, 55: 4, 25: 2, 65: 4, 105: 4, 103: 4, 252: 4, 250: 2, 260: 5, 255: 4, 10: 1, 20: 2, 30: 2, 60: 4, 53: 4}
TSV = next((a[6:] for a in sys.argv[1:] if a.startswith('--tsv=')), None)
args = [a for a in sys.argv[1:] if not a.startswith('--tsv=')]
log = args[0]; FR = args[1] if len(args) > 1 else None; BUDGET = float(args[2]) if len(args) > 2 else 170
P = collections.OrderedDict(); dead = set(); shader = collections.defaultdict(collections.Counter)
TBPP = {10: 1, 20: 2, 25: 2, 30: 2, 53: 4, 55: 4, 60: 4, 65: 8, 70: 4, 71: 4, 80: 4, 81: 4, 90: 4, 92: 4, 93: 4, 103: 8, 105: 8, 115: 8, 125: 16,
        130: .5, 131: .5, 132: 1, 133: 1, 134: 1, 135: 1, 140: .5, 141: .5, 142: 1, 143: 1, 150: 1, 151: 1, 152: 1, 153: 1, 250: 2, 252: 4, 255: 4, 260: 4}
texinfo = collections.defaultdict(dict); sampled = collections.defaultdict(set); tgt = {}; maxvc = collections.defaultdict(int); ndraw = collections.defaultdict(int); fskeys = collections.defaultdict(set); fused = set(); att_desc = collections.defaultdict(list)
import os, json; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import dmsh_chain
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
LIB = os.path.join(DM_ROOT, 'build/cache/metal_shader_cache/b-{dmsh_chain.version()}')
def tex_slots(key):
    try: return sum(1 for b in json.load(open(f'{LIB}/{key}_b.json'))['bindings'] if b.get('kind') in ('tex', 'samp_tex'))
    except Exception: return 4
nm = {}
for ln in open(__file__.rsplit('/', 1)[0] + '/../nvn/shader_map.tsv'):
    c = ln.rstrip('\n').split('\t')
    if len(c) > 4 and c[0].endswith('.bin'): nm.setdefault(c[3], c[0][:-4])
for l in open(log, errors='replace'):
    m = re.search(r'\[pass\] F(\d+) (\w) (\d+) (.*)', l)
    if not m: continue
    f, k, p, r = m.groups()
    if FR is None: FR = f
    if f != FR: continue
    p = int(p); e = P.setdefault(p, {'att': [], 'load': 0, 'store': 0})
    if k == 'P':
        a = re.match(r'(c\d+|d|s) ser=(\d+) fmt=(\d+) (\d+)x(\d+)(?:.*? lv(\d+) sl(\d+))?.*?\b(clear|load|dontcare)', r)
        if not a: continue
        slot, ser, fmt, w, h, lv, sl, act = a.groups(); fmt = int(fmt); lv = int(lv or 0)
        b = max(int(w) >> lv, 1) * max(int(h) >> lv, 1) * BPP.get(fmt, 4)
        if fmt == 260: b = b // 5 * (4 if slot == 'd' else 1)
        e['att'].append((slot, int(ser), lv, int(sl or 0), b, act))
        att_desc[p].append(r)
        tgt.setdefault(p, max(int(w) >> lv, 1) * max(int(h) >> lv, 1))
    elif k == 'B':
        b = re.match(r'st\d+ (?:tex|tbuf) reg\d+ tid=(\d+) 实 ser=(\d+) fmt=(\d+) (\d+)x(\d+)x(\d+)', r)
        if b:
            tid, ser, fmt, w, h, dd = map(int, b.groups())
            texinfo[p].setdefault(ser, set()).add((tid, fmt, w, h, dd))
    elif k == 'R':
        rr = re.match(r'st\d+ ser=(\d+)', r)
        if rr: sampled[p].add(int(rr.group(1)))
    elif k == 'Z':
        z = re.match(r'(c|d|s) ser=(\d+) lv(\d+) sl(\d+) (dontcare|store)', r)
        if z and z.group(5) == 'dontcare': dead.add((p, z.group(1), int(z.group(2)), int(z.group(3)), int(z.group(4))))
    elif k == 'F' and '合并下一个通道' in r: fused.add(p)
    elif k == 'K' and ' st1 ' in f' {r} ':
        key = r.split('key=')[1].split()[0].replace('_b', ''); shader[p][nm.get(key, key[:8])] += 1; fskeys[p].add(key)
    elif k in ('D', 'd'):   # d 行（PASSDUMP_ALL）每次绘制一行，可数绘制次数；D 行只有每通道第一次 | d rows (PASSDUMP_ALL) are one per draw, so draws can be counted; D rows only have the first of each pass
        v = re.search(r'vc=(\d+)', r)
        if v: maxvc[p] = max(maxvc[p], int(v.group(1)))
        if k == 'd': ndraw[p] += 1
ALLD = any(ndraw.values())   # 有逐次绘制行时按绘制次数分类，否则按片元程序绑定行数（只记首次时会把多次小方块误判成全屏类） | classify by draw count when per-draw rows exist, otherwise by the number of fragment program binding rows (when only the first is recorded, many small quads are misclassified as fullscreen class)
rows = []
for p, e in P.items():
    ld = sum(b for *_, b, act in e['att'] if act == 'load')
    st = sum(b for slot, ser, lv, sl, b, act in e['att'] if (p, slot[0], ser, lv, sl) not in dead)
    px = tgt.get(p, 0); tx = 0
    nd = ndraw[p] if ALLD else sum(shader[p].values())
    if maxvc[p] <= 6 and nd <= 3:   # 全屏类（绘制顶点 ≤ 6 且绘制 ≤ 3 次；UI 等多次小方块按几何类算） | fullscreen class (draw vertices ≤ 6 and ≤ 3 draws; many small quads such as UI count as geometry class)
        for ser in sampled[p]:
            for tid, f, w, h, dd in texinfo[p].get(ser, {(-1, None, 0, 0, 1)}):
                tx += min(w * h * dd * TBPP.get(f, 4), px * TBPP.get(f, 4)) if f is not None else px * 4
    elif sampled[p]:    # 几何类 | geometry class
        bp = [TBPP.get(v[1], 4) for s_ in sampled[p] for v in texinfo[p].get(s_, ())] or [4]
        tx = px * max([tex_slots(k_) for k_ in fskeys[p]] or [4]) * (sum(bp) / len(bp))
    rows.append((ld + st + tx, ld, st, tx, p, ' '.join(f'{slot}:{ser}' + (f'/lv{lv}' if lv else '') for slot, ser, lv, sl, b, act in e['att']), ', '.join(shader[p])[:40]))
tl = sum(r[1] for r in rows) / 1e6; ts = sum(r[2] for r in rows) / 1e6; tt = sum(r[3] for r in rows) / 1e6
print(f'帧 {FR}：通道 {len(rows)} 个；附件 读入 {tl:.1f} + 写回 {ts:.1f} = {tl + ts:.1f} MB（预算 {BUDGET:.0f}，超 {tl + ts - BUDGET:+.1f}）；纹理采样（粗估{"" if ALLD else "，无逐次绘制行，分类偏粗"}）{tt:.1f} MB；总带宽 {tl + ts + tt:.1f} MB')
print('总 MB | 附件读入 | 附件写回 | 纹理采样 | 通道 | 附件 | 主要片元着色器')
acc = 0
for t, ld, st, tx, p, att, sh in sorted(rows, reverse=True)[:25]:
    acc += t; print(f'{t / 1e6:6.1f} | {ld / 1e6:5.1f} | {st / 1e6:5.1f} | {tx / 1e6:5.1f} | {p} | {att[:52]} | {sh}   （累计 {acc / 1e6:.0f}）')
if TSV:
    import csv
    with open(TSV, 'w', encoding='utf-8', newline='') as out:
        wr = csv.writer(out, delimiter='\t')
        wr.writerow(['frame', 'render_order', 'pass', 'draws', 'draw_source', 'sampling_model', 'target_pixels', 'load_bytes', 'store_bytes', 'texture_est_bytes', 'total_est_bytes', 'unknown_textures', 'unknown_formats', 'texture_views', 'sampled_serials', 'fragment_keys', 'fragment_names', 'attachments', 'attachment_details', 'note'])
        for order, (total, ld, st, tx, p, att, _) in enumerate(rows):
            nd = ndraw[p] if ALLD else sum(shader[p].values())
            source = 'PASSDUMP_ALL' if ndraw[p] else 'clear_only' if not maxvc[p] else 'shader_bindings'
            model = 'small_draw' if maxvc[p] <= 6 and nd <= 3 else 'geometry'
            views = [v for s_ in sampled[p] for v in texinfo[p].get(s_, ())]
            wr.writerow([FR, order, p, nd, source, model, tgt.get(p, 0), int(ld), int(st), int(tx), int(total),
                         len(sampled[p] - texinfo[p].keys()), sum(v[1] not in TBPP for v in views), len(views), ','.join(map(str, sorted(sampled[p]))), ','.join(sorted(fskeys[p])), ','.join(shader[p]), att, '; '.join(att_desc[p]),
                         '绑定模型粗估；含tile绑定，不是外存实测' if p in fused else '绑定模型粗估，不是外存实测'])
