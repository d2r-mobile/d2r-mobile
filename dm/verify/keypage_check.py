#!/usr/bin/env python3
"""检查键页间接访问、旧槽访问计数和 text 差异。用法：keypage_check.py NEW OLD。 | Check key-page indirect accesses, the old-slot access count, and text differences. Usage: keypage_check.py NEW OLD."""
import collections
import re
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM


KEYS = ('K18', 'KTLS', 'KTLR', 'KGATE', 'KRES0', 'KRES1', 'KASPECT', 'KASPECTUI')
KEY_OFF = {504: 'KASPECTUI', 505: 'KASPECT', 506: 'KRES0', 507: 'KRES1', 508: 'K18', 509: 'KTLS', 510: 'KTLR', 511: 'KGATE'}
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True


def meta(path):
    out = {}
    for line in open(path):
        if line.startswith('# '):
            k, v = line[2:].rstrip().split('\t', 1)
            out[k] = int(v, 0) if v.startswith('0x') else v
    return out


def old_counts(directory):
    counts = collections.Counter()
    for line in open(f'{directory}/main.sites.tsv'):
        if line.startswith('#'):
            continue
        fields = line.rstrip().split('\t')
        if fields[1] == 'x18' or fields[1] == 'W':
            counts['K18'] += 1
        elif fields[1] == 'tpidr_r' or fields[1] == 'tpidr_w':
            counts['KTLS'] += 1
        elif fields[1] == 'tpidrro_r':
            counts['KTLR'] += 1
    for line in open(f'{directory}/main.gates.tsv'):
        if line.strip():
            counts['KGATE'] += 1
    for line in open(f'{directory}/main.params.tsv'):
        if line.startswith('#'):
            continue
        key = line.rstrip().split('\t')[3]
        counts[key] += 1
    return counts


def new_counts(directory, key_page_off, patch_off):
    counts = collections.Counter()
    data = open(f'{directory}/main.patched.bin', 'rb').read()
    ins = list(md.disasm(data[patch_off:], patch_off))
    for prev, cur in zip(ins, ins[1:]):
        if prev.mnemonic != 'adrp' or cur.mnemonic != 'ldr':
            continue
        if prev.operands[0].reg != cur.operands[1].mem.base or prev.operands[1].imm != key_page_off:
            continue
        index = cur.operands[1].mem.disp // 8
        if 0 <= index < len(KEYS) and cur.operands[1].mem.disp % 8 == 0:
            counts[KEYS[index]] += 1
    return counts


def differences(new_dir, old_dir, text_end):
    """原 text 区新旧逐字节比：改写站点处新旧都须是 b（补丁区后移，只允许跳转目标不同）；参数位 / 剪口 / 规则剪口放行；其余不许变。 | Byte-by-byte comparison of the original text area, new vs old: at rewrite sites both new and old must be b (the patch area moved back, only the jump target may differ); parameter sites / cuts / rule cuts are let through; nothing else may change."""
    new = open(f'{new_dir}/main.patched.bin', 'rb').read()[:text_end]
    old = open(f'{old_dir}/main.patched.bin', 'rb').read()[:text_end]
    allowed, sites = set(), set()
    for base in (new_dir, old_dir):
        for name in ('main.params.tsv', 'main.cuts.tsv', 'main.rules.tsv'):
            try: lines = open(f'{base}/{name}')
            except FileNotFoundError: continue
            for line in lines:
                if line.startswith('#') or not line.strip(): continue
                f = line.split('\t'); a = int(f[0], 0)
                if name == 'main.params.tsv': allowed.update(range(a, a + 8)); allowed.update(range(int(f[1], 0), int(f[1], 0) + 8))
                else: allowed.update(range(a, a + len(bytes.fromhex(f[1]))))
        for line in open(f'{base}/main.obs.tsv'):   # 观测门入口同样换成跳补丁区的 b | Observation gate entries are likewise replaced with a b into the patch area
            if line.startswith('#') or not line.strip(): continue
            sites.add(int(line.split('\t')[0], 0))
        for line in open(f'{base}/main.sites.tsv'):
            if line.startswith('#') or not line.strip(): continue
            f = line.split('\t'); a = int(f[0], 0); sites.add(a)
            if f[1] == 'W': allowed.update(range(a, int(f[2], 0)))
    isb = lambda w: (w & 0xFC000000) == 0x14000000
    bad = []
    for a in range(0, min(len(new), len(old)) & ~3, 4):
        if new[a:a + 4] == old[a:a + 4] or a in allowed: continue
        if a in sites and isb(struct.unpack('<I', new[a:a + 4])[0]) and isb(struct.unpack('<I', old[a:a + 4])[0]): continue
        bad.append(a)
    return bad, len(sites)

def main():
    if len(sys.argv) != 3:
        raise SystemExit('用法：keypage_check.py NEW_DIR OLD_DIR')
    new_dir, old_dir = sys.argv[1:]
    m = meta(f'{new_dir}/main.fixups.tsv')
    assert m['KEY_PAGE_SIZE'] == 0x4000
    assert m['KEY_PAGE_OFF'] % 0x4000 == 0
    assert m['PATCH_OFF'] % 0x10000 == 0 and m['KEY_PAGE_OFF'] < m['PATCH_OFF']
    raw = open(f'{new_dir}/main.patched.bin', 'rb').read()
    assert raw[m['KEY_PAGE_OFF']:m['KEY_PAGE_OFF'] + 0x4000] == bytes(0x4000)
    bad = []
    for ins in md.disasm(raw[m['PATCH_OFF']:], m['PATCH_OFF']):
        if ins.mnemonic == 'mrs' and ins.op_str.endswith('tpidrro_el0'):
            nxt = list(md.disasm(raw[ins.address + 4:ins.address + 12], ins.address + 4))
            if nxt and nxt[0].mnemonic in ('ldr', 'str') and re.search(r'\[x(\d+), #([0-9]+)\]', nxt[0].op_str):
                if int(nxt[0].op_str.rsplit('#', 1)[1].rstrip(']')) in tuple(x * 8 for x in KEY_OFF):
                    bad.append(ins.address)
    assert not bad, f'仍有直取槽序列: {[hex(x) for x in bad]}'
    new = new_counts(new_dir, m['KEY_PAGE_OFF'], m['PATCH_OFF'])
    old = old_counts(old_dir)
    assert all(new[k] == old[k] for k in KEYS), f'计数不等: new={new} old={old}'
    diff, nsites = differences(new_dir, old_dir, m['TEXT_END'])
    assert not diff, f'text 非站点跳转 / 参数 / 剪口差异: {[hex(x) for x in diff[:20]]}'
    print('键页布局与初值：期望 → 真结果：0x4000 对齐、补丁区前、全零 → 通过')
    print(f'各下标引用计数：期望 → 真结果：{dict(old)} → {dict(new)}')
    print(f'原 text 差异：期望 → 真结果：仅 {nsites} 个站点的 b 目标、参数位与既有剪口 → 通过')


if __name__ == '__main__':
    main()
