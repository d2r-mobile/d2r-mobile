# 逐处等价验证：原指令（原映像，x18 是真寄存器）vs 补丁块（改写映像，x18 在 TSD 槽）。
# 每处随机状态多组；比较 x0-x30（x18 对比槽值）、sp、nzcv、v0-v31、全部内存写入、结束 PC、访存异常。
# 用法：equiv.py <改写产物目录> <模块> <NSO> [--seed 1] [--jobs N] [--min-eff 2] [--max-extra 16] [--mutate 300] [--tsv 文件]
# 射程：两边都访存异常的试验只比异常地址，不算「有效」；每处至少 min-eff 次有效试验，不够就补抽（≤max-extra 次），仍不够的列名。
# 变异自检：抽 mutate 个普通改写块，一半把替换后的那条指令改成 nop、一半把跳板出口 b 的目标 +4，都必须被判出分歧（判据能区分的负对照）。
# 宿主门（svc / cntpct / cntvct）语义由宿主定，这里不跑，由 structck.py 做结构核对；已登记剪口（cuts.tsv）是有意的语义改动，同样只由 structck 核字节。
# Per-site equivalence verification: original instruction (original image, x18 is a real register) vs patch block (rewritten image, x18 lives in a TSD slot).
# Several random states per site; compares x0-x30 (x18 compared against the slot value), sp, nzcv, v0-v31, all memory writes, final PC, memory access faults.
# Usage: equiv.py <rewrite build output dir> <module> <NSO> [--seed 1] [--jobs N] [--min-eff 2] [--max-extra 16] [--mutate 300] [--tsv file]
# Coverage: a trial where both sides take a memory access fault only compares the fault address and does not count as "effective"; each site needs at least min-eff effective trials, extra draws are made if short (≤max-extra), and sites still short are listed by name.
# Mutation self-check: draw mutate ordinary rewrite blocks; in half the replaced instruction is changed to nop, in half the target of the trampoline exit b gets +4; all must be judged divergent (a negative control showing the criterion can discriminate).
# Host gate (svc / cntpct / cntvct) semantics are defined by the host and are not run here, structck.py does the structural check; registered cuts (cuts.tsv) are intentional semantic changes and likewise only have their bytes checked by structck.
import sys, os, struct, random, argparse, multiprocessing as mp
from unicorn import *
from unicorn.arm64_const import *
from capstone import *
sys.path.insert(0, os.path.dirname(__file__))
import nsoimg

ap = argparse.ArgumentParser()
ap.add_argument('dir'); ap.add_argument('mod'); ap.add_argument('nso')
ap.add_argument('--seed', type=int, default=1); ap.add_argument('--jobs', type=int, default=os.cpu_count())
ap.add_argument('--min-eff', type=int, default=2); ap.add_argument('--max-extra', type=int, default=16)
ap.add_argument('--mutate', type=int, default=300); ap.add_argument('--tsv')
A = ap.parse_args()

IB = 0x100000000
ARENA = 0x0; ARENA_SZ = 0x1000000          # 低 16 MB 可读写随机区 | Low 16 MB readable/writable random area
TSD = 0x2000000
SP0 = 0x800000
DIR, MOD = A.dir, A.mod
hdr = nsoimg.header(DIR, MOD)                  # 表头：段边界与写死进指令的线程槽号 | Header: segment boundaries and the thread slot numbers hard-coded into instructions
PATCH_OFF = hdr['PATCH_OFF']
KEY_IDX = {k[4:-4]: v for k, v in hdr.items() if k.startswith('KEY_') and k.endswith('_IDX')}
KEY_SLOT = {k: 300 + i for i, k in enumerate(sorted(KEY_IDX, key=KEY_IDX.get))}
SLOT18, SLOTTLS, SLOTTLR = (KEY_SLOT[k] for k in ('K18', 'KTLS', 'KTLR'))
KEY_PAGE_OFF = hdr['KEY_PAGE_OFF']
pat = bytearray(open(f'{DIR}/{MOD}.patched.bin', 'rb').read())
orig = nsoimg.load(A.nso)
ncut = sum(1 for l in open(f'{DIR}/{MOD}.cuts.tsv') if not l.startswith('#'))
nobs = sum(1 for l in open(f'{DIR}/{MOD}.obs.tsv') if not l.startswith('#')) if os.path.exists(f'{DIR}/{MOD}.obs.tsv') else 0
def deexcl(b):
    # 仅测试用：全部独占读写（各位宽、单 / 成对）→ 普通 ldr/str/ldp/stp；store 后紧跟的状态 cbz/cbnz → nop。两份映像同样处理。
    # Test only: all exclusive loads/stores (every width, single / pair) → plain ldr/str/ldp/stp; the status cbz/cbnz right after a store → nop. Both images are processed the same way.
    LD = {0: 0x39400000, 1: 0x79400000, 2: 0xB9400000, 3: 0xF9400000}
    ST = {0: 0x39000000, 1: 0x79000000, 2: 0xB9000000, 3: 0xF9000000}
    b = bytearray(b); n = 0
    for o in range(0, len(b) - 4, 4):
        w = struct.unpack_from('<I', b, o)[0]
        if w & 0x3F800000 != 0x08000000: continue
        size, L, pair = w >> 30, (w >> 22) & 1, (w >> 21) & 1
        rs, rt2, rn, rt = (w >> 16) & 31, (w >> 10) & 31, (w >> 5) & 31, w & 31
        if pair:
            if size < 2: continue
            nw = ((0xA9400000 if L else 0xA9000000) if size == 3 else (0x29400000 if L else 0x29000000)) | (rt2 << 10) | (rn << 5) | rt
        else:
            nw = (LD if L else ST)[size] | (rn << 5) | rt
        struct.pack_into('<I', b, o, nw); n += 1
        if not L:
            nx = struct.unpack_from('<I', b, o + 4)[0]
            if nx & 0x7E000000 == 0x34000000 and nx & 31 == rs: struct.pack_into('<I', b, o + 4, 0xD503201F)
    return bytes(b), n
orig, _ = deexcl(orig); pat, _ = deexcl(pat)

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
sites = []; winend = {}; brinfo = {}; ngate = 0
for l in open(f'{DIR}/{MOD}.sites.tsv'):
    f = l.split(); a = int(f[0], 16)
    if f[1] == 'W': winend[a] = int(f[2], 16); sites.append(a)
    elif f[1] == 'J': brinfo[a] = (int(f[2], 16), int(f[3]))
    elif f[1] in ('svc', 'cntpct', 'cntvct'): ngate += 1      # 宿主门：语义由宿主定，只做结构核对 | Host gate: semantics are defined by the host, only a structural check is done
    else: sites.append(a)

def btarget(b, o):
    w = struct.unpack_from('<I', b, o)[0]; imm = w & 0x3FFFFFF
    if imm & 0x2000000: imm -= 0x4000000
    return o + 4 * imm
def plain_block(a):
    """普通改写块（stp; 键页间接读槽; 替换指令; 键页间接写槽; ...）→ 替换指令偏移。 | Ordinary rewrite block (stp; indirect slot read via key page; replaced instruction; indirect slot write via key page; ...) → offset of the replaced instruction."""
    s = btarget(pat, a); w0 = struct.unpack_from('<I', pat, s)[0]
    if w0 & 0xFFC003E0 != 0xA90003E0: return None          # stp xS, xT, [sp, #off]
    ins = list(md.disasm(bytes(pat[s:s + 36]), s))
    if len(ins) < 8 or ins[1].mnemonic != 'mrs' or 'tpidrro_el0' not in ins[1].op_str:
        return None
    if ins[2].mnemonic != 'adrp' or ins[2].operands[1].imm != KEY_PAGE_OFF: return None
    if ins[3].mnemonic != 'ldr' or ins[3].operands[1].mem.base != ins[2].operands[0].reg: return None
    T = ins[1].operands[0].reg
    if ins[4].mnemonic != 'add' or ins[4].operands[0].reg != T or ins[4].operands[1].reg != T: return None
    if ins[5].mnemonic != 'ldr' or ins[5].operands[1].mem.base != T: return None              # ldr xS,[xT]：读槽 | ldr xS,[xT]: read the slot
    if ins[7].mnemonic != 'str' or ins[7].operands[1].mem.base != T: return None              # str xS,[xT]：写回槽——只有普通块有，条件块没有（与旧版口径相同） | str xS,[xT]: write back to the slot - only ordinary blocks have it, conditional blocks do not (same convention as the old version)
    return ins[6].address
# 独占窗口内的条件分支（状态 cbz/cbnz 已被 deexcl 改成 nop，不在此列）：随机状态几乎抽不到「比较相等」那一向（2^-32），
# 所以每条分支两个方向各强制走一遍——两份映像同一条分支同时改成 nop（不跳）/ 无条件 b（必跳），其余照常比。
# 由来：r40 及以前改写器把窗口副本的「顺序落出」出口排在外部出口之后，比较交换窗口写成功后落进「比较不等」的出口；随机试验全绿（暗黑 6 个窗口：main 0x1848eb8 / 0x1848f28 / 0x1848f84，sdk 0x2ea0fc / 0x2ea4cc / 0x2ea644）。
# Conditional branches inside an exclusive window (the status cbz/cbnz has already been turned into nop by deexcl and is not included): a random state almost never draws the "compare equal" direction (2^-32),
# so each branch is forced through both directions once -- in both images the same branch is changed to nop (not taken) / an unconditional b (taken) at the same time, and the rest is compared as usual.
# Background: up to r40 the rewriter placed the "sequential fall-out" exit of the window copy after the external exits, so a compare-and-swap window fell into the "compare not equal" exit after a successful write; the random trials were all green (6 windows in the game: main 0x1848eb8 / 0x1848f28 / 0x1848f84, sdk 0x2ea0fc / 0x2ea4cc / 0x2ea644).
def cond_target(w, pc):
    if w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000: imm = (w >> 5) & 0x7FFFF; imm -= (imm & 0x40000) << 1; return pc + 4 * imm
    if w & 0x7E000000 == 0x36000000: imm = (w >> 5) & 0x3FFF; imm -= (imm & 0x2000) << 1; return pc + 4 * imm
    return None
win_br = {}
for a0, hi0 in winend.items():
    gate = btarget(pat, a0); pre = list(md.disasm(bytes(pat[gate:gate + 24]), gate))
    assert [x.mnemonic for x in pre] == ['stp', 'mrs', 'adrp', 'ldr', 'add', 'ldr'], f'窗口块前导不是预期形状 {a0:#x}'
    copy0 = gate + 24; lst = []
    for c in range(a0, hi0, 4):
        wo = struct.unpack_from('<I', orig, c)[0]; to = cond_target(wo, c)
        if to is None: continue
        cp = copy0 + (c - a0); wp = struct.unpack_from('<I', pat, cp)[0]; tp_ = cond_target(wp, cp)
        assert tp_ is not None, f'窗口副本里找不到对应的条件分支 {c:#x}'
        lst.append((c, wo, to, cp, wp, tp_))
    if lst: win_br[a0] = lst
def enc_b(pc, tgt): return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)
mut_sites = []
if A.mutate:
    cand = [a for a in sites if a not in winend and a not in brinfo and plain_block(a)]
    mut_sites = sorted(random.Random(A.seed).sample(cand, min(A.mutate, len(cand))))
    pat_mut = bytearray(pat)
    def exit_b(a):   # 改写块回 text 的那条 b（块内第一条目标落回 text 的无条件 b） | The b by which a rewrite block returns to text (the first unconditional b in the block whose target lands back in text)
        w = struct.unpack_from('<I', pat, a)[0]; o = a + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000)
        for k in range(o, o + 4 * 24, 4):
            w = struct.unpack_from('<I', pat, k)[0]
            if w & 0xFC000000 == 0x14000000 and k + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) < PATCH_OFF: return k, w
        return None
    # 两类变异各一半：偶数位 = 替换后的那条指令改 nop；奇数位 = 跳板出口 b 的目标 +4（参数门跳板回跳越界那一类：越过一条原指令）
    # Two kinds of mutation, half each: even index = the replaced instruction is changed to nop; odd index = target of the trampoline exit b +4 (the class where a parameter-gate trampoline jumps back out of bounds: skips one original instruction)
    mut_kind = {}
    for i, a in enumerate(mut_sites):
        eb = exit_b(a) if i % 2 else None
        if eb: k, w = eb; struct.pack_into('<I', pat_mut, k, (w & 0xFC000000) | ((w + 1) & 0x3FFFFFF)); mut_kind[a] = 'exit+4'
        else: struct.pack_into('<I', pat_mut, plain_block(a), 0xD503201F); mut_kind[a] = 'nop'
    pat_mut = bytes(pat_mut)

GP = [getattr(sys.modules['unicorn.arm64_const'], f'UC_ARM64_REG_X{i}') for i in range(29)] + [UC_ARM64_REG_X29, UC_ARM64_REG_X30]
VR = [getattr(sys.modules['unicorn.arm64_const'], f'UC_ARM64_REG_Q{i}') for i in range(32)]

def mk(image, arena0):
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    sz = (len(image) + 0xFFFF) & ~0xFFFF
    u.mem_map(IB, sz, UC_PROT_ALL); u.mem_write(IB, bytes(image))
    u.mem_map(ARENA, ARENA_SZ); u.mem_write(ARENA, arena0)
    u.mem_map(TSD, 0x10000)
    u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, TSD)
    st = {'w': [], 'fault': None, 'arena': arena0}
    def wr(uc, acc, addr, size, val, d): st['w'].append((addr, size, val & ((1 << (8 * size)) - 1)))
    def bad(uc, acc, addr, size, val, d): st['fault'] = (acc, addr); return False
    u.hook_add(UC_HOOK_MEM_WRITE, wr)
    u.hook_add(UC_HOOK_MEM_INVALID, bad)
    return u, st

def restore(u, st):
    for a, s, v in st['w']:
        if ARENA <= a < ARENA + ARENA_SZ: u.mem_write(a, st['arena'][a:a + s])
    st['w'].clear(); st['fault'] = None

def state(u, st, patched):
    g = [u.reg_read(r) for r in GP]
    tp = u.reg_read(UC_ARM64_REG_TPIDR_EL0)
    if patched: g[18] = struct.unpack('<Q', u.mem_read(TSD + 8 * SLOT18, 8))[0]; tp = struct.unpack('<Q', u.mem_read(TSD + 8 * SLOTTLS, 8))[0]
    g = g + [tp]
    w = [x for x in st['w'] if not (patched and TSD <= x[0] < TSD + 0x10000)]
    return g, u.reg_read(UC_ARM64_REG_SP), u.reg_read(UC_ARM64_REG_NZCV), [u.reg_read(r) for r in VR], w, st['fault']

def run(u, st, start, patched, init):
    restore(u, st)
    for r, v in zip(GP, init['g']): u.reg_write(r, v)
    for r, v in zip(VR, init['v']): u.reg_write(r, v)
    u.reg_write(UC_ARM64_REG_SP, SP0); u.reg_write(UC_ARM64_REG_NZCV, init['nzcv'])
    u.reg_write(UC_ARM64_REG_TPIDR_EL0, init['tls'])
    if not patched: u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, init['tlr'])
    if patched:
        for key, idx in KEY_IDX.items():
            u.mem_write(IB + KEY_PAGE_OFF + 8 * idx, struct.pack('<Q', KEY_SLOT[key] * 8))
        u.mem_write(TSD + 8 * SLOT18, struct.pack('<Q', init['g'][18]))
        u.mem_write(TSD + 8 * SLOTTLS, struct.pack('<Q', init['tls']))
        u.mem_write(TSD + 8 * SLOTTLR, struct.pack('<Q', init['tlr']))
        u.reg_write(GP[18], 0xDEADBEEFDEADBEEF)
    end = {'pc': None}
    if patched:
        seen = {'patch': False}
        def hk(uc, addr, size, d):
            if addr >= IB + PATCH_OFF: seen['patch'] = True
            elif seen['patch']: end['pc'] = addr; uc.emu_stop()
        h = u.hook_add(UC_HOOK_CODE, hk)
        try: u.emu_start(start, IB + 0x7FFFFFFF, count=400)
        except UcError: pass
        u.hook_del(h)
        if end['pc'] is None and st['fault'] is None: end['pc'] = 'NOEXIT'
    elif start - IB in winend:
        lo, hi = start, IB + winend[start - IB]
        def hk2(uc, addr, size, d):
            if not (lo <= addr < hi): end['pc'] = addr; uc.emu_stop()
        h = u.hook_add(UC_HOOK_CODE, hk2)
        try: u.emu_start(start, IB + 0x7FFFFFFF, count=400)
        except UcError: pass
        u.hook_del(h)
        if end['pc'] is None and st['fault'] is None: end['pc'] = 'NOEXIT'
    else:
        try: u.emu_start(start, IB + 0x7FFFFFFF, count=1)
        except UcError: pass
        end['pc'] = u.reg_read(UC_ARM64_REG_PC) if st['fault'] is None else None
    s = state(u, st, patched)
    return s + (end['pc'],)

def init_state(rng, kind, site_br=None):
    g = []
    for i in range(31):
        if kind == 0: g.append(rng.randrange(0, 0x10000) & ~7)
        elif kind == 1: g.append(rng.randrange(0, ARENA_SZ - 0x10000))
        else: g.append(rng.getrandbits(64))
    if site_br: g[18] = IB + site_br[0] + 4 * rng.randrange(site_br[1])
    return {'g': g, 'v': [rng.getrandbits(128) for _ in range(32)], 'nzcv': rng.randrange(16) << 28, 'tls': rng.randrange(0, 0x100000) & ~15, 'tlr': rng.randrange(0x100000, 0x200000) & ~15}

def trial(UO, SO, UP, SP, a, init):
    so = run(UO, SO, IB + a, False, init)
    sp_ = run(UP, SP, IB + a, True, init)
    ow = {x[0] for x in so[4]}
    sp_ = sp_[:4] + ([x for x in sp_[4] if not (SP0 - 128 <= x[0] < SP0 and x[0] not in ow)],) + sp_[5:]
    both = bool(so[5] and sp_[5])
    same = (so[5] == sp_[5]) if (so[5] or sp_[5]) else (so == sp_)
    return same, both, [k for k, (x, y) in enumerate(zip(so, sp_)) if x != y]

def work(job):
    idx, chunk, mutated = job
    rng = random.Random(A.seed * 1000003 + idx * 2 + mutated)
    arena0 = rng.randbytes(ARENA_SZ)
    UO, SO = mk(orig, arena0); UP, SP = mk(pat_mut if mutated else pat, arena0)
    out = []
    for a in chunk:
        n = eff = 0; diffs = []
        kinds = [0, 1] + ([] if a in brinfo else [2])
        extra = 0
        while kinds or (eff < A.min_eff and extra < A.max_extra):
            if kinds: kind = kinds.pop(0)
            else: kind = (1, 0)[extra % 2]; extra += 1
            same, both, fl = trial(UO, SO, UP, SP, a, init_state(rng, kind, brinfo.get(a)))
            n += 1; eff += not both
            if not same: diffs.append((kind, fl))
            if mutated and diffs: break
        if not mutated:
            for c, wo, to, cp, wp, tp_ in win_br.get(a, ()):
                for mode in ('不跳', '必跳'):
                    UO.mem_write(IB + c, struct.pack('<I', 0xD503201F if mode == '不跳' else enc_b(c, to)))
                    UP.mem_write(IB + cp, struct.pack('<I', 0xD503201F if mode == '不跳' else enc_b(cp, tp_)))
                    UO.ctl_remove_cache(IB + c, IB + c + 4); UP.ctl_remove_cache(IB + cp, IB + cp + 4)
                    for kind in (0, 1, 0, 1):
                        same, both, fl = trial(UO, SO, UP, SP, a, init_state(rng, kind))
                        n += 1; eff += not both
                        if not same: diffs.append((f'窗口分支 {c:#x} 强制{mode}', fl))
                    UO.mem_write(IB + c, struct.pack('<I', wo)); UP.mem_write(IB + cp, struct.pack('<I', wp))
                    UO.ctl_remove_cache(IB + c, IB + c + 4); UP.ctl_remove_cache(IB + cp, IB + cp + 4)
        out.append((a, n, eff, diffs))
    return out

def runall(lst, mutated):
    k = 64                                   # 切块数固定，结果只随 seed 变、不随 jobs 变 | Chunk count is fixed, so the result varies only with seed, not with jobs
    jobs = [(i, lst[i::k], mutated) for i in range(k) if lst[i::k]]
    with mp.get_context('fork').Pool(A.jobs) as p:
        return [r for part in p.imap_unordered(work, jobs) for r in part]

def ins_of(a): return next(md.disasm(orig[a:a + 4], a))
if __name__ == '__main__':
    res = sorted(runall(sites, 0))
    T = sum(r[1] for r in res); E = sum(r[2] for r in res)
    zero = [r for r in res if r[2] == 0]; low = [r for r in res if r[2] < A.min_eff]
    bad = [r for r in res if r[3]]
    for a, n, eff, diffs in bad:
        i = ins_of(a)
        for kind, fl in diffs: print(f'DIFF {a:#x} {i.mnemonic} {i.op_str} kind={kind} fields={fl}', flush=True)
    for a, n, eff, _ in zero:
        i = ins_of(a); print(f'NOEFF {a:#x} {i.mnemonic} {i.op_str} trials={n}')
    caught = miss = 0; mut_by = '-'
    if mut_sites:
        mres = runall(mut_sites, 1)
        caught = sum(1 for r in mres if r[3])
        for a, n, eff, d in mres:
            if not d: i = ins_of(a); print(f'MUTMISS {a:#x} {mut_kind[a]} {i.mnemonic} {i.op_str} trials={n} eff={eff}')
        miss = len(mres) - caught
        mk_ = {}
        for a, n, eff, d in mres: c = mk_.setdefault(mut_kind[a], [0, 0]); c[0] += 1; c[1] += bool(d)
        mut_by = ' '.join(f'{k}:{v[1]}/{v[0]}' for k, v in sorted(mk_.items()))
    if A.tsv:
        with open(A.tsv, 'w') as f:
            f.write('site\ttrials\teff\tdiffs\n')
            for a, n, eff, d in res: f.write(f'{a:#x}\t{n}\t{eff}\t{len(d)}\n')
    ok = not bad and not zero and not miss   # 变异漏抓 = 判据分不出坏改写，判红（早先漏抓只打印不判） | A missed mutation = the criterion cannot tell a bad rewrite apart, so fail (earlier a miss was only printed, not judged)
    print(f'EQUIV {MOD} {"OK" if ok else "FAIL"} 站点={len(res)} 试验={T} 有效试验={E} 双异常={T - E} '
          f'有效≥1站点={len(res) - len(zero)} 有效≥{A.min_eff}站点={len(res) - len(low)} 零有效={len(zero)} 分歧站点={len(bad)} '
          f'窗口={len(winend)} 其中含条件分支={len(win_br)}（{sum(len(v) for v in win_br.values())} 条×2 向强制） 门站点(结构核对)={ngate} 已登记剪口(结构核对)={ncut} 观测门(结构核对)={nobs} 变异={len(mut_sites)}（{mut_by}） 抓到={caught} 漏抓={miss} seed={A.seed}', flush=True)
    sys.exit(0 if ok else 1)
