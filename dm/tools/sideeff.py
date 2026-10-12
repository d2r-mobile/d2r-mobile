# 剪口证据：列被剪调用树的全部同步原语与对外副作用（把「剪口证据只证结果相同」这条教训工具化）。
# 用法：sideeff.py <原映像 .orig.bin> <根偏移>[,<根偏移>...] [--depth N]
# 从根沿直接 bl/b（尾调用）走遍调用树（函数边界取 eh_frame），输出：
#   ① 每个导入（PLT）调用：名字 + 调用点 + 所在函数（同步原语 / 文件 / 线程类单独标出）
#   ② 每个间接调用点 blr / br xN（非跳表）：需人工逐条解析目标
#   ③ 遍历到的函数数
# 只做静态枚举，不判断「成对 / 无人等待」——那一步须按清单逐条人工证。
# Cut evidence: lists all synchronization primitives and external side effects of the cut call tree (turning the lesson "cut evidence only proves the result is the same" into a tool).
# Usage: sideeff.py <original image .orig.bin> <root offset>[,<root offset>...] [--depth N]
# Walks the whole call tree from the roots along direct bl/b (tail calls) (function boundaries from eh_frame) and outputs:
#   ① every import (PLT) call: name + call site + containing function (synchronization primitive / file / thread classes are marked separately)
#   ② every indirect call site blr / br xN (not a jump table): the target must be resolved manually one by one
#   ③ the number of functions traversed
# Static enumeration only, with no judgment of "paired / nobody waiting" -- that step must be proven manually item by item from the list.
import struct, sys, bisect, re
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

img = open(sys.argv[1], 'rb').read()
roots = [int(x, 16) for x in sys.argv[2].split(',')]
maxd = int(sys.argv[sys.argv.index('--depth') + 1]) if '--depth' in sys.argv else 64
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

# ---- 动态段：符号表 / PLT 槽名（同 sdis.py）
# ---- Dynamic section: symbol table / PLT slot names (same as sdis.py)
m = struct.unpack_from('<I', img, 4)[0]
dyn, _, _, ehs, ehe, _ = [m + x for x in struct.unpack_from('<6i', img, m + 4)]
t = {}; o = dyn
while True:
    tag, v = struct.unpack_from('<qQ', img, o); o += 16
    if tag == 0: break
    t.setdefault(tag, v)
SYM, STR = t[6], t[5]
def cstr(off): return img[off:img.index(b'\0', off)].decode()
got = {}
for i in range(0, t.get(2, 0), 24):
    off, info, _ = struct.unpack_from('<QQq', img, t[23] + i)
    got[off] = cstr(STR + struct.unpack_from('<I', img, SYM + (info >> 32) * 24)[0])

# ---- eh_frame_hdr 函数起点
# ---- eh_frame_hdr function starts
def rd(o, enc, base):
    fmt, app = enc & 0xf, enc & 0x70
    v, sz = {0x3: (struct.unpack_from('<I', img, o)[0], 4), 0xb: (struct.unpack_from('<i', img, o)[0], 4), 0x4: (struct.unpack_from('<Q', img, o)[0], 8)}[fmt]
    if app == 0x10: v += o
    if app == 0x30: v += base
    return v, sz
_, ptrenc, cntenc, tblenc = img[ehs:ehs + 4]
o = ehs + 4; _, s = rd(o, ptrenc, ehs); o += s; n, s = rd(o, cntenc, ehs); o += s
starts = set()
for _ in range(n):
    a, s = rd(o, tblenc, ehs); o += s; _, s = rd(o, tblenc, ehs); o += s; starts.add(a)
starts = sorted(starts)
def fbounds(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i], starts[i + 1] if i + 1 < len(starts) else a + 4

# ---- PLT 桩：adrp x16 / ldr x17,[x16,#o] / add / br x17
# ---- PLT stubs: adrp x16 / ldr x17,[x16,#o] / add / br x17
plt = {}
def plt_name(a):
    if a in plt: return plt[a]
    w = list(md.disasm(img[a:a + 12], a)); name = None
    if len(w) >= 2 and w[0].mnemonic == 'adrp' and w[0].op_str.startswith('x16') and w[1].mnemonic == 'ldr' and 'x17' in w[1].op_str:
        page = int(w[0].op_str.split('#')[1], 16); off = int(re.search(r'#(0x[0-9a-f]+|\d+)\]', w[1].op_str).group(1), 0)
        name = got.get(page + off)
    plt[a] = name; return name

SYNC = re.compile(r'Mutex|Condition|Semaphore|Event|Barrier|ReaderWriter|Lock|Signal|Wait|Thread|Sleep|Yield|Fiber|Message|Queue|Atomic|futex|cond|sem_', re.I)
IO = re.compile(r'File|fopen|fwrite|fclose|Write|Flush|Mount|Save|Commit|Directory|Create|Delete|Rename|socket|send|connect|Notify|Event', re.I)

seen = {}; imports = []; indirect = []; parent = {}
stack = [(r, 0, None) for r in roots]
while stack:
    a, d, par = stack.pop()
    lo, hi = fbounds(a)
    if lo in seen: continue
    seen[lo] = d; parent[lo] = par
    code = img[lo:hi]
    for ins in md.disasm(code, lo):
        mn = ins.mnemonic
        if mn in ('bl', 'b') and ins.op_str.startswith('#'):
            tgt = int(ins.op_str[1:], 16)
            if mn == 'b' and lo <= tgt < hi: continue          # 函数内跳转 | Jump within the function
            nm = plt_name(tgt)
            if nm: imports.append((nm, ins.address, lo)); continue
            if d + 1 <= maxd: stack.append((tgt, d + 1, (lo, ins.address)))
        elif mn in ('blr', 'br') and not ins.op_str.startswith('x16') and not ins.op_str.startswith('x17'):
            indirect.append((ins.address, lo, mn + ' ' + ins.op_str))

print(f'根 {",".join(hex(r) for r in roots)}：遍历函数 {len(seen)} 个（深度上限 {maxd}）')
def cat(nm): return '同步' if SYNC.search(nm) else ('外部I/O' if IO.search(nm) else '其它')
from collections import defaultdict
by = defaultdict(list)
for nm, at, fn in imports: by[(cat(nm), nm)].append((at, fn))
for (c, nm), lst in sorted(by.items()):
    print(f'[{c}] {nm} ×{len(lst)}：' + ' '.join(f'{hex(at)}@{hex(fn)}' for at, fn in lst[:12]) + (' …' if len(lst) > 12 else ''))
print(f'间接调用 {len(indirect)} 处（br 含跳表，需人工区分）：')
for at, fn, s in indirect[:200]: print(f'  {hex(at)} 在 {hex(fn)}：{s}')

def path(fn):
    out = []
    while fn is not None and parent.get(fn) is not None:
        pf, at = parent[fn]; out.append(f'{hex(pf)}@{hex(at)}'); fn = pf
    return ' ← '.join(out)
if '--paths' in sys.argv:
    print('同步 / 外部 I/O 导入的调用链（被调函数 ← 调用者@调用点 …，深度优先首次发现的那条）：')
    done = set()
    for nm, at, fn in imports:
        if cat(nm) == '其它' or (nm, fn) in done: continue
        done.add((nm, fn)); print(f'  {nm} 在 {hex(fn)} ← {path(fn)}')
