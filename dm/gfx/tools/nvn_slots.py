#!/usr/bin/env python3
# main 的 NVN 取址表与静态调用面（3.3.94012）。
# 1) 解析 main 里的 NVN 加载函数（按名字取址后逐个存进全局指针槽）→ 名字 → 槽；
# 2) 全 .text 扫「ldr xT,[xN,#imm]」读槽：从读取点往回找 xN 的上一次写入，是 adrp 到槽页才算一次调用点。
#    （前向寄存器跟踪会把陈旧值当槽地址、或漏掉循环外提的 adrp，两种都抽查出过错，故用回溯。）
# 用法：nvn_slots.py <main 原映像 .bin（dm/tools/nso.py 解出）> <输出 tsv> [名字表输出]
# 输出列：名字 槽地址 调用点数 首个调用点；名字表按槽地址（= 加载顺序）。
# main's NVN address-lookup table and static call surface (3.3.94012).
# 1) Parse the NVN loader function in main (looks up addresses by name and stores them one by one into global pointer slots) → name → slot;
# 2) Scan all of .text for "ldr xT,[xN,#imm]" slot reads: search backward from the read for the previous write to xN; it only counts as a call site if that write is an adrp to the slot's page.
#    (Forward register tracking treats stale values as slot addresses, or misses an adrp hoisted out of a loop; spot checks found both kinds of error, hence the backward trace.)
# Usage: nvn_slots.py <main raw image .bin (extracted by dm/tools/nso.py)> <output tsv> [name table output]
# Output columns: name, slot address, call site count, first call site; the name table is ordered by slot address (= load order).
import struct, sys
LOADER = 0x1739290                    # main 内 NVN 加载函数（3.3.94012；换版本先重新定位） | NVN loader function inside main (3.3.94012; re-locate it first when changing versions)
img, out = sys.argv[1], sys.argv[2]
d = open(img, 'rb').read()
TEXT_END = 0x1b786a0                  # main .text 尾（NSO 段头） | end of main .text (NSO segment header)
N = TEXT_END // 4
w = struct.unpack_from('<%dI' % N, d, 0)

def adrp(x, pc):
    imm = ((x >> 29) & 3) | (((x >> 5) & 0x7ffff) << 2)
    if imm & (1 << 20): imm -= 1 << 21
    return (pc & ~0xfff) + (imm << 12)
def cstr(a): return d[a:d.index(b'\0', a)].decode()

# 1) 加载函数：adrp/add x1 = 名字，str x0,[xn,#imm] = 槽
# 1) Loader function: adrp/add x1 = name, str x0,[xn,#imm] = slot
i, reg, cur, slot = LOADER // 4, {}, None, {}
while w[i] != 0xd65f03c0:
    x, pc = w[i], i * 4
    if x & 0x9f000000 == 0x90000000: reg[x & 31] = adrp(x, pc)
    elif x & 0xffc00000 == 0x91000000 and (x & 31) == 1: cur = cstr(reg[(x >> 5) & 31] + ((x >> 10) & 0xfff))
    elif x & 0xffc00000 == 0xf9000000 and (x & 31) == 0: slot[reg[(x >> 5) & 31] + ((x >> 10) & 0xfff) * 8] = cur
    i += 1
offs = {a & 0xfff for a in slot}

def writes(x, r):
    """x 是否写通用寄存器 r（近似、偏保守） | Whether x writes general-purpose register r (approximate, conservative)"""
    op0 = (x >> 25) & 0xf
    if x & 0xfc000000 == 0x94000000 or x & 0xfffffc1f == 0xd63f0000: return r <= 18   # bl / blr
    if op0 in (0b1000, 0b1001, 0b0101, 0b1101): return (x & 31) == r                    # 数据处理 | data processing
    if op0 & 0b0101 == 0b0100:                                                          # 读写内存 | memory load/store
        L = (x >> 22) & 1
        if (x >> 27) & 7 == 0b101:
            wb = (x >> 23) & 3 in (1, 3)
            return (L and ((x & 31) == r or ((x >> 10) & 31) == r)) or (wb and ((x >> 5) & 31) == r)
        if (x >> 27) & 7 == 0b111 and (x >> 24) & 3 == 0:
            wb = (x >> 21) & 1 == 0 and (x >> 10) & 3 in (1, 3)
            return (L and (x & 31) == r) or (wb and ((x >> 5) & 31) == r)
        if (x >> 27) & 7 == 0b111: return bool(L) and (x & 31) == r
        if (x >> 27) & 7 == 0b011: return (x & 31) == r
    return False

# 2) 回溯归属每次读槽
# 2) Attribute each slot read by tracing backward
uses = {a: [] for a in slot}
for i in range(N):
    x = w[i]
    if x & 0xffc00000 != 0xf9400000: continue
    off = ((x >> 10) & 0xfff) * 8
    rn = (x >> 5) & 31
    if off not in offs or rn == 31: continue
    for j in range(i - 1, max(i - 4000, -1), -1):
        y = w[j]
        if y == 0xd65f03c0: break
        if writes(y, rn):
            if y & 0x9f000000 == 0x90000000 and (y & 31) == rn and adrp(y, j * 4) + off in uses:
                uses[adrp(y, j * 4) + off].append(i * 4)
            break
with open(out, 'w') as f:
    f.write('# kind: evidence  main NVN 取址槽与静态调用点（nvn_slots.py）\nname\tslot\tnuses\tfirst_use\n')
    for a in sorted(slot):
        u = uses[a]; f.write('%s\t%x\t%d\t%s\n' % (slot[a], a, len(u), '%x' % u[0] if u else '-'))
if len(sys.argv) > 3: open(sys.argv[3], 'w').write(''.join(slot[a] + '\n' for a in sorted(slot)))
used = sum(1 for a in slot if uses[a])
print('槽 %d，有调用点 %d，调用点 %d' % (len(slot), used, sum(len(v) for v in uses.values())))
