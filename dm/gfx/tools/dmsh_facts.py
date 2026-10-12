#!/usr/bin/env python3
# 翻译查询的作答表 nvn/shader_facts.tsv：每个 Switch 着色器键一行，记翻译器问得到、但 Switch 机器码里读不出的那几项事实——
# 贴图 / 图像的维度类型、图像格式、顶点属性与片元输出的分量类型、计算着色器的线程组大小与共享内存字节数。
# 这些事实取自与它一一配对的 PC 版着色器的反射（NVN 槽号 = D3D 寄存器号，见 dm/gfx/README.md）。表随仓库，出着色器库只读这张表，
# 不需要 PC 版游戏文件；表里没有的键，翻译查询按缺省作答（出包汇总里单列）。
# 用法：dmsh_facts.py --freeze   从 PC 反射重出整张表（维护者用；要 PCCACHE 反射目录、PC DXBC 目录与 vkd3d-compiler）
#       dmsh_facts.py --check    重算一遍与仓内的表逐键比对，不一致报红退出 1（同上，要 PC 侧输入）
#       dmsh_facts.py            打印表的行数与各项事实的覆盖数
# 行格式：键 <TAB> JSON（键按字典序、紧凑写法；同样的输入逐字节同样的表）
# Answer table for translation queries, nvn/shader_facts.tsv: one row per Switch shader key, recording the few facts the translator asks for but that cannot be read from the Switch machine code -
# dimension type of textures / images, image format, component types of vertex attributes and fragment outputs, thread group size and shared memory bytes of compute shaders.
# These facts come from the reflection of the PC shader paired one-to-one with it (NVN slot number = D3D register number, see dm/gfx/README.md). The table ships with the repo; building the shader library reads only this table
# and needs no PC game files; for keys not in the table, translation queries are answered with defaults (listed separately in the build summary).
# Usage: dmsh_facts.py --freeze   regenerate the whole table from PC reflection (for maintainers; needs the PCCACHE reflection dir, the PC DXBC dir and vkd3d-compiler)
#       dmsh_facts.py --check    recompute and compare key by key against the in-repo table; on mismatch report a failure and exit 1 (as above, needs the PC-side inputs)
#       dmsh_facts.py            print the table's row count and the coverage count of each fact
# Row format: key <TAB> JSON (keys in lexicographic order, compact form; the same input gives a byte-identical table)
import json, os, re, subprocess, sys
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
HERE = os.path.dirname(os.path.abspath(__file__)); HOME = os.path.expanduser('~')
TABLE = os.path.join(HERE, '../nvn/shader_facts.tsv'); MAP = os.path.join(HERE, '../nvn/shader_map.tsv')
TT = {'texture1d': '1d', 'texture1darray': '1darray', 'texture2d': '2d', 'texture2darray': '2darray', 'texture2dms': '2dms',
      'texture2dmsarray': '2dmsarray', 'texture3d': '3d', 'texturecube': 'cube', 'texturecubearray': 'cubearray', 'buffer_typed': 'buffer'}
CT = {'float': 'float', 'int': 'sint', 'uint': 'uint'}

def load():
    """键 → 事实字典；表不存在返回空字典 | key → fact dict; returns an empty dict if the table does not exist"""
    if not os.path.exists(TABLE): return {}
    return {k: json.loads(v) for k, v in (l.rstrip('\n').split('\t', 1) for l in open(TABLE) if l.strip() and not l.startswith('#'))}

def row(key, facts): return key + '\t' + json.dumps(facts, sort_keys=True, separators=(',', ':'), ensure_ascii=False) + '\n'

def from_pc(pj, stage, dxbc_asm):
    """PC 反射 json（pj）+ 计算着色器的 DXBC 反汇编文本（没有给 None）→ 事实字典 | PC reflection json (pj) + DXBC disassembly text of the compute shader (None if absent) → fact dict"""
    f = {'tex': {}, 'img': {}, 'attr': {}, 'fout': {}}
    for b in pj['bindings']:
        if b['kind'] == 't' and b['type'] in TT: f['tex'][str(b['reg'])] = TT[b['type']]
        if b['kind'] == 'u' and b['type'] in TT:
            f['img'][str(b['reg'])] = {'type': TT[b['type']], 'fmt': 'R32Uint' if 'uint' in (b.get('msl_type') or '') else 'R16G16B16A16Float'}
    for v in pj.get('vertex_inputs') or []: f['attr'][str(v.get('location', v.get('index')))] = CT.get(v.get('component_type'), 'float')
    for o in pj.get('fragment_outputs') or []: f['fout'][str(o.get('index', 0))] = CT.get(o.get('component_type'), 'float')
    if pj.get('threadgroup_size'): f['local'] = pj['threadgroup_size']
    if stage == 'cs':   # 共享内存：PC DXBC 的 groupshared 声明求和（翻译器缺省 48 KB，超 Metal 上限 32 KB） | shared memory: sum of the groupshared declarations in the PC DXBC (the translator default is 48 KB, over Metal's 32 KB limit)
        asm = dxbc_asm or ''
        n = sum(int(m.group(1)) for m in re.finditer(r'dcl_tgsm_raw\s+g\d+,\s*(\d+)', asm)) + \
            sum(int(m.group(1)) * int(m.group(2)) for m in re.finditer(r'dcl_tgsm_structured\s+g\d+,\s*(\d+),\s*(\d+)', asm))
        f['shared'] = (n + 15) // 16 * 16
        if dxbc_asm is None: f['pc_missing'] = True
    return f

def pairs():
    """映射表（含冲突表的改判）→ {键: (配对的 PC 名, 阶段)} | Mapping table (with the conflict table's overrides) → {key: (paired PC name, stage)}"""
    pc_of, stage_of = {}, {}
    for ln in open(MAP):
        c = ln.rstrip('\n').split('\t')
        if len(c) > 4 and c[0].endswith('.bin'): pc_of.setdefault(c[3], c[4]); stage_of.setdefault(c[3], c[0][:2])
        if len(c) > 4 and c[0].startswith('main.rodata@'): pc_of.setdefault(c[3], c[4]); stage_of[c[3]] = {'顶点': 'vs', '像素': 'ps'}[c[2]]
    for ln in open(MAP + '.conflicts.tsv'):
        c = ln.rstrip('\n').split('\t')
        if len(c) > 1 and not ln.startswith('#') and not ln.startswith('dm_') and c[0] in pc_of: pc_of[c[0]] = c[1]
    return {k: (pc_of[k], stage_of[k]) for k in pc_of}

def recompute():
    pcc = os.environ.get('PCCACHE', f'{DM_ROOT}/build/cache/metal_shader_cache')
    dxdirs = os.environ.get('PCDXBC', f'{DM_ROOT}/build/ios/dxbc_all_0930:{DM_ROOT}/data/d3d12_study/shaders').split(':')
    vk = os.environ.get('VKD3D', f'{DM_ROOT}/build/vkd3d/inst/bin/vkd3d-compiler')
    if not os.path.isdir(pcc): sys.exit(f'【红】没有 PC 反射目录 {pcc}（PCCACHE=）')
    out, missing = {}, 0
    for key, (pc, stage) in sorted(pairs().items()):
        pp = os.path.join(pcc, pc + '.json')
        if not os.path.exists(pp): missing += 1; continue   # 无配对 PC 反射：不进表，查询按缺省 | no paired PC reflection: not entered in the table, queries use defaults
        asm = None
        if stage == 'cs':
            dx = next((p for d in dxdirs for p in (os.path.join(d, pc + '.dxbc'), os.path.join(d, pc[:12] + '.dxbc')) if os.path.exists(p)), None)
            if dx: asm = subprocess.run([vk, '-x', 'dxbc-tpf', '-b', 'd3d-asm', dx], capture_output=True, text=True).stdout
        out[key] = from_pc(json.load(open(pp)), stage, asm)
    return out, missing

if __name__ == '__main__':
    a = sys.argv[1:]
    if a == ['--freeze']:
        t, missing = recompute()
        open(TABLE, 'w').write('# 翻译查询的作答表（dmsh_facts.py --freeze 生成，勿手改）：键 <TAB> 事实 JSON\n' + ''.join(row(k, t[k]) for k in sorted(t)))
        print(f'已写 {TABLE}：{len(t)} 键；无配对 PC 反射、未进表 {missing}')
    elif a == ['--check']:
        t, missing = recompute(); have = load()
        bad = sorted(k for k in set(t) | set(have) if t.get(k) != have.get(k))
        for k in bad[:10]: print(f'【红】{k}：表 {have.get(k)} / 重算 {t.get(k)}')
        print(f'逐键比对 {len(set(t) | set(have))}：不一致 {len(bad)}；无配对 PC 反射 {missing}'); sys.exit(1 if bad else 0)
    elif not a:
        t = load(); print(f'{len(t)} 键：' + '，'.join(f'{n} {sum(bool(f.get(n)) for f in t.values())}' for n in ('tex', 'img', 'attr', 'fout', 'local', 'shared', 'pc_missing')))
    else: sys.exit('用法见文件头')
