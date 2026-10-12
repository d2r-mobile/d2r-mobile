#!/usr/bin/env python3
# Switch 着色器 → PC 着色器（DXBC）→ PC 链 MSL（metallib）映射表。
# 依据：两侧 data/hd/shaders 都是 162 个同名 SHAD 包，每个变体一条 BTCD 记录，记录数逐包相等（共 10847 条）；
#   记录头除尺寸外只差平台号（PC 3 / Switch 5），没有排列键 ⇒ 按包内序号一一对应。
#   顺序对齐的证据：计算着色器线程组大小逐条 22/22 相等；记录数 ≥16 的 22 个包代码量秩相关，对齐 0.81–1.00、错位一条全部更低。
# Switch 记录：头之后 = GPU 代码段（0x12345678 开头），其后 = 控制段（0x98761234 开头，长度见记录头）。
#   运行期 nvnProgramSetShaders 拿到的是控制段指针 ⇒ 键 = sha256(控制段)[:16]。
# PC 记录：内含一个 DXBC；PC 链缓存文件名 = sha256(DXBC)[:16].metallib（platform/shell/mtl12/shaders/build_cache.py）。
# 绑定映射（maxwell_refs.py 头注释）：NVN 槽号 = D3D 寄存器号，按 Switch 机器码实际引用逐条核「Switch 用到的 ⊆ PC 声明的」。
# 键冲突（Switch 二进制相同、PC DXBC 不同）时选一份：与 Switch 实际用到的输出数 / cb / t / s 差异最小者；
#   并列取最简（输出少、资源少），再并列取 DXBC 哈希最小（确定性）。选择与理由写进 <输出>.conflicts.tsv。
# 程序本体内嵌的着色器（不在包里）：Switch main 原映像里有 10 个控制段，前 6 个是图形着色器，与 PC D2R.exe 内嵌的 6 个 DXBC 同序一一对应
#   （两边同位置各有一对完全相同的重复项）；后 4 个是计算着色器，PC 无对应，另行处理。
# 用法：shader_map.py <PC 包目录> <Switch 包目录> <PC metallib 缓存目录> <输出 tsv> [<main 原映像> <PC D2R.exe>]
# Mapping table: Switch shader → PC shader (DXBC) → PC-chain MSL (metallib).
# Basis: data/hd/shaders on both sides holds 162 SHAD packages with the same names, one BTCD record per variant, and record counts are equal package by package (10847 in total);
#   apart from sizes, record headers differ only in the platform number (PC 3 / Switch 5), and there is no permutation key ⇒ map one-to-one by index within the package.
#   Evidence of order alignment: compute shader thread group sizes are equal record by record, 22/22; for the 22 packages with ≥16 records, code size is rank-correlated, 0.81–1.00 when aligned and lower in every case when shifted by one record.
# Switch record: after the header = GPU code segment (starts with 0x12345678), followed by = control segment (starts with 0x98761234, length in the record header).
#   At runtime nvnProgramSetShaders receives the control segment pointer ⇒ key = sha256(control segment)[:16].
# PC record: contains one DXBC; PC-chain cache file name = sha256(DXBC)[:16].metallib (platform/shell/mtl12/shaders/build_cache.py).
# Binding mapping (header comment of maxwell_refs.py): NVN slot number = D3D register number; verified record by record against what the Switch machine code actually references that "used by Switch ⊆ declared by PC".
# On a key conflict (same Switch binary, different PC DXBC) pick one: the one with the smallest difference from the output count / cb / t / s actually used by Switch;
#   on a tie take the simplest (fewer outputs, fewer resources), on a further tie take the smallest DXBC hash (deterministic). The choice and the reason are written to <output>.conflicts.tsv.
# Shaders embedded in the executable itself (not in packages): the Switch main raw image has 10 control segments; the first 6 are graphics shaders and map one-to-one, in the same order, to the 6 DXBC embedded in PC D2R.exe
#   (each side has a pair of identical duplicates at the same position); the last 4 are compute shaders with no PC counterpart, handled separately.
# Usage: shader_map.py <PC package dir> <Switch package dir> <PC metallib cache dir> <output tsv> [<main raw image> <PC D2R.exe>]
import hashlib, json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from maxwell_refs import bindings, vs_inputs, sph_stage

pc_dir, dm_dir, cache_dir, out = sys.argv[1:5]

def records(d):
    r = [m.start() for m in re.finditer(b'BTCDBTCD', d)]
    return [d[x:(r[k + 1] if k + 1 < len(r) else len(d))] for k, x in enumerate(r)]

def pc_dxbc(rec):
    i = rec.find(b'DXBC')
    return rec[i:i + struct.unpack_from('<I', rec, i + 24)[0]]

def dx_outputs(dx):
    j = dx.find(b'OSGN')
    if j < 0: return -1
    n = struct.unpack_from('<I', dx, j + 8)[0]
    return sum(1 for q in range(n) if struct.unpack_from('<I', dx, j + 16 + q * 24 + 8)[0] == 0)

def dm_parts(rec):
    i = rec.find(bytes.fromhex('34127698'))            # 控制段魔数（小端 0x98761234） | control segment magic (little-endian 0x98761234)
    csz = struct.unpack_from('<I', rec, 0x28)[0]        # 记录头：平台号 5、控制段长度… | record header: platform number 5, control segment length...
    return rec[i:i + csz], i

STAGE = {'vs': '顶点', 'ps': '像素', 'cs': '计算', 'hs': '外壳', 'ds': '域'}
cache = set(os.listdir(cache_dir))
rows, keys, cand, dxs, locs = [], {}, {}, {}, {}
for name in sorted(os.listdir(dm_dir)):
    A = records(open(os.path.join(pc_dir, name), 'rb').read())
    B = records(open(os.path.join(dm_dir, name), 'rb').read())
    if len(A) != len(B): sys.exit(f'【红】{name} 记录数不等 PC {len(A)} / Switch {len(B)}')
    for k, (a, b) in enumerate(zip(A, B)):
        dx = pc_dxbc(a); ctl, _ = dm_parts(b)
        if len(ctl) < 0x100: sys.exit(f'【红】{name}#{k} 控制段异常')
        swk = hashlib.sha256(ctl).hexdigest()[:16]; pck = hashlib.sha256(dx).hexdigest()[:16]
        ml = pck + '.metallib' in cache
        dm_out = struct.unpack_from('<I', ctl, 0x734)[0] if name.startswith('ps_') else -1
        pc_out = dx_outputs(dx) if name.startswith('ps_') else -1
        refl = json.load(open(os.path.join(cache_dir, pck + '.json')))['bindings'] if ml else []
        pcb = {x['reg'] for x in refl if x['type'] == 'cbuffer'}
        pt = {x['reg'] for x in refl if x['type'].startswith('texture') or x['type'].startswith('buffer')}
        ps_ = {x['reg'] for x in refl if x['type'] == 'sampler'}
        _, ci = dm_parts(b); sb = bindings(b[0x38:ci], name[:2])
        pbuf = {x['reg'] for x in refl if x['type'] in ('buffer_raw', 'buffer_structured', 'atomic_alias')}
        ok = int(sb['ubo'] <= pcb) | int(sb['tex'] <= pt) << 1 | int(sb['samp'] <= ps_) << 2 | int(sb['ssbo'] == pbuf) << 3
        fmt = lambda x: ','.join(map(str, sorted(x))) or '-'
        rows.append((name, k, STAGE.get(name[:2], name[:2]), swk, pck, int(ml), pc_out, dm_out,
                     fmt(sb['ubo']), fmt(sb['tex']), fmt(sb['samp']), fmt(sb['ssbo']), ok))
        keys.setdefault(swk, set()).add(pck)
        cand.setdefault(swk, {})[pck] = (pc_out, dm_out, pcb, pt, ps_, sb)
        dxs[pck] = dx; locs.setdefault(swk, []).append(f'{name}#{k}')

if len(sys.argv) > 6:
    mi = open(sys.argv[5], 'rb').read(); pe = open(sys.argv[6], 'rb').read()
    dm_ctl = [m.start() for m in re.finditer(bytes.fromhex('34127698'), mi)]
    pc_dx = []
    for m in re.finditer(b'DXBC', pe):
        o = m.start(); sz = struct.unpack_from('<I', pe, o + 24)[0] if o + 32 <= len(pe) else 0
        if 64 < sz < 1 << 20 and o + sz <= len(pe) and struct.unpack_from('<I', pe, o + 20)[0] == 1: pc_dx.append(pe[o:o + sz])
    gfx = [o for o in dm_ctl if mi.rfind(bytes.fromhex('78563412'), o - 0x1000, o) > 0]
    # 按特征配对（不靠顺序：反汇编发现两边内嵌顺序不同）：阶段取 SPH；集合差异 = cb/t/s 与（顶点）输入槽的对称差，取唯一最小
    # Pair by features (not by order: disassembly showed the embedded order differs between the two sides): stage is taken from SPH; set difference = symmetric difference of cb/t/s and (vertex) input slots, take the unique minimum
    def pc_feat(dx):
        h = hashlib.sha256(dx).hexdigest()[:16]
        j = json.load(open(os.path.join(cache_dir, h + '.json')))
        bs = j['bindings']
        return h, j['stage'], ({x['reg'] for x in bs if x['type'] == 'cbuffer'}, {x['reg'] for x in bs if x['type'].startswith('texture')},
                               {x['reg'] for x in bs if x['type'] == 'sampler'}, {x['location'] for x in j['vertex_inputs']})
    pcs = [pc_feat(dx) for dx in pc_dx]
    for k, o in enumerate(gfx):
        g = mi.rfind(bytes.fromhex('78563412'), o - 0x1000, o)
        code = mi[g:o]; stg = sph_stage(code); sb = bindings(code, stg)
        dm = (sb['ubo'], sb['tex'], sb['samp'], vs_inputs(code) if stg == 'vs' else set())
        cands = sorted((sum(len(a ^ b) for a, b in zip(dm, f)), h) for h, st, f in pcs if st == stg)
        if not cands or (len(cands) > 1 and cands[0][0] == cands[1][0] and cands[0][1] != cands[1][1]):
            sys.exit(f'【红】内嵌着色器 @{o:#x}（{stg}）配不出唯一的 PC 对应：{cands}')
        pck = cands[0][1]; swk = hashlib.sha256(mi[o:o + 0x880]).hexdigest()[:16]
        ml = pck + '.metallib' in cache
        refl = json.load(open(os.path.join(cache_dir, pck + '.json')))['bindings'] if ml else []
        pbuf = {x['reg'] for x in refl if x['type'] in ('buffer_raw', 'buffer_structured', 'atomic_alias')}
        f = pcs[[h for h, _, _ in pcs].index(pck)][2]
        ok = int(dm[0] <= f[0]) | int(dm[1] <= f[1]) << 1 | int(dm[2] <= f[2]) << 2 | int(sb['ssbo'] == pbuf) << 3
        fmt = lambda x: ','.join(map(str, sorted(x))) or '-'
        rows.append(('main.rodata@%x' % o, k, STAGE.get(stg, stg), swk, pck, int(ml), -1, -1, fmt(sb['ubo']), fmt(sb['tex']), fmt(sb['samp']), fmt(sb['ssbo']), ok))
        keys.setdefault(swk, set()).add(pck)
        print(f'  内嵌 @{o:#x} {stg} 用 cb{fmt(dm[0])} t{fmt(dm[1])} s{fmt(dm[2])} 输入{fmt(dm[3])} → PC {pck}（差异 {cands[0][0]}，次优 {cands[1][0] if len(cands) > 1 else "-"}）')
    print(f'内嵌图形着色器 {len(gfx)} 个（metallib {sum(1 for r in rows[-len(gfx):] if r[5])} 个），内嵌计算着色器 {len(dm_ctl) - len(gfx)} 个未映射')

amb = {k: v for k, v in keys.items() if len(v) > 1}
with open(out, 'w') as f:
    f.write('# kind: evidence  Switch 着色器变体 → PC DXBC → PC 链 metallib（dm/gfx/tools/shader_map.py）\n')
    f.write('# 键冲突（同一 Switch 控制段对应多份 PC DXBC）：%d\n' % len(amb))
    f.write('package\tindex\tstage\tdm_ctl_sha16\tpc_dxbc_sha16\tmetallib\tpc_outputs\tdm_outputs\tdm_ubo\tdm_tex\tdm_samp\tdm_ssbo\tsubset_ok(cb|t<<1|s<<2|ssbo全等<<3)\n')
    for r in rows: f.write('\t'.join(map(str, r)) + '\n')

# 键冲突裁定
# Key conflict resolution
def dx_chunks(d):
    out = {}
    for i in range(struct.unpack_from('<I', d, 28)[0]):
        o = struct.unpack_from('<I', d, 32 + 4 * i)[0]
        out[d[o:o + 4]] = d[o + 8:o + 8 + struct.unpack_from('<I', d, o + 4)[0]]
    return out
def klass(pcs):
    ch = [dx_chunks(dxs[p]) for p in pcs]
    code = [c.get(b'SHEX', c.get(b'SHDR')) for c in ch]
    if len(set(code)) == 1: return '指令块相同·仅输入签名不同（建管线时按顶点着色器输出配对选）'
    if len({len(x) for x in code}) == 1: return '指令块等长·输入签名不同（疑为输入寄存器重编号，同按配对选）'
    return '指令块不同（真实代码差异，Switch 编成同一二进制；需运行期定位包与序号或画面确认）'
def score(c):
    po, so, pcb, pt, ps_, sb = c
    diff = (po != so and po >= 0) + len(pcb ^ sb['ubo']) + len(pt ^ sb['tex']) + len(ps_ ^ sb['samp'])
    return (diff, max(po, 0) + len(pcb) + len(pt) + len(ps_))
with open(out + '.conflicts.tsv', 'w') as f:
    f.write('# kind: evidence  Switch 控制段键冲突的 PC 候选裁定（差异最小 → 最简 → 哈希最小）\n')
    f.write('dm_ctl_sha16\tchosen_pc\tclass\tcandidates(pc:差异:规模)\trecords\n')
    for k in sorted(amb):
        cs = sorted(cand[k].items(), key=lambda kv: (score(kv[1]), kv[0]))
        f.write('%s\t%s\t%s\t%s\t%s\n' % (k, cs[0][0], klass([p for p, _ in cs]), ' '.join('%s:%d:%d' % (p, *score(c)) for p, c in cs), ' '.join(locs[k])))

n = len(rows); ml = sum(r[5] for r in rows)
okc = [sum(1 for r in rows if r[12] >> b & 1) for b in range(4)]
print(f'绑定子集成立：cb {okc[0]}/{len(rows)}，t {okc[1]}/{len(rows)}，s {okc[2]}/{len(rows)}；存储缓冲全等 {okc[3]}/{len(rows)}')
uniq_dm = len(keys); uniq_pc = len({r[4] for r in rows}); uniq_pc_ml = len({r[4] for r in rows if r[5]})
outd = [r for r in rows if r[6] >= 0 and r[6] != r[7]]
print(f'记录 {n}；Switch 控制段去重 {uniq_dm}；PC DXBC 去重 {uniq_pc}；键冲突 {len(amb)}')
print(f'有 PC metallib 的记录 {ml}/{n}；PC DXBC 去重后有 metallib {uniq_pc_ml}/{uniq_pc}')
print(f'像素着色器输出数不同（PC 多目标、Switch 少写）{len(outd)} 条')
by = {}
for r in rows: by.setdefault(r[2], [0, 0]); by[r[2]][0] += 1; by[r[2]][1] += r[5]
for s, (t, m) in by.items(): print(f'  {s}：记录 {t}，有 metallib {m}')
