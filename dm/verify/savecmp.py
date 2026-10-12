# 存档逐字节对拍：两份用户存档目录（各含 0/ 1/ ExtraData*）比 0/ 下全部文件。判据：校验和各自自洽、两处墙钟时间核来源、其余逐字节全同。
# 用法：savecmp.py <A 存档目录> <B 存档目录> [--a-meta=<A 局目录 meta.json>] [--b-meta=…] [--a-time=<秒>] [--b-time=<秒>]
#   退出码 0 = 一致；1 = 不一致；3 = 射程不足（一侧没有 .d2s）
#   墙钟字段不互比，改为「核来源」：给了 --x-meta（真值局目录 meta.json 的 wall_start / wall_end）⇒ 该侧时间须落在开跑—结束区间内；
#   给了 --x-time（宿主 DM_FIXED_TIME）⇒ 该侧时间须恰等于该值；两样都没给 ⇒ 该侧时间字段报「未核」并判红（不许静默放过）
# 不逐字节互比的字段（只这些，改动须经评审）：
#   .d2s 0x0c–0x0f 全文件校验和（任何字节变它都变；改核两边各自「校验和自洽」）
#   .d2s 0x20–0x23（版本 0x69）最后游玩时间、<角色>.d2s.meta 的 last_modified_time（纳秒）：系统时间输入。真值同起点同种子同 --fix-time 两局
#   （cvd4 / cvd4b）只差这两处（差 32 秒 = 两局开跑间隔；--fix-time 只定住建局那一次 time()）。两边值不同时按上面的「核来源」各自核
# Byte-by-byte save comparison: for two user save directories (each containing 0/ 1/ ExtraData*), compare all files under 0/. Criterion: each checksum self-consistent, the source of the two wall-clock times verified, everything else identical byte for byte.
# Usage: savecmp.py <save dir A> <save dir B> [--a-meta=<meta.json of run dir A>] [--b-meta=...] [--a-time=<seconds>] [--b-time=<seconds>]
#   exit code 0 = match; 1 = mismatch; 3 = insufficient coverage (one side has no .d2s)
#   wall-clock fields are not compared with each other but "source-verified": with --x-meta (wall_start / wall_end in the ground-truth run dir's meta.json) ⇒ that side's time must fall within the start-to-end interval;
#   with --x-time (the host's DM_FIXED_TIME) ⇒ that side's time must equal that value exactly; with neither ⇒ that side's time field is reported as 「未核」(unverified) and fails (no silent pass)
# Fields not compared byte for byte (only these; changes require review):
#   .d2s 0x0c–0x0f whole-file checksum (changes when any byte changes; instead check that each side's "checksum is self-consistent")
#   .d2s 0x20–0x23 (version 0x69) last played time, and last_modified_time (nanoseconds) in <character>.d2s.meta: system time inputs. Two ground-truth runs with the same start, same seed, and same --fix-time
#   (cvd4 / cvd4b) differ only in these two places (by 32 seconds = the interval between the two run starts; --fix-time pins only the single time() at game creation). When the two sides differ, each is checked by the "source verification" above
import re
import sys, os, struct

def files(d):
    d = os.path.join(d, '0')
    return {os.path.relpath(os.path.join(r, f), d) for r, _, fs in os.walk(d) for f in fs}

def d2s_sum(b):
    """D2 存档校验和：校验和字段置 0 后逐字节 sum = (sum << 1 | sum >> 31) + byte（32 位）。 | D2 save checksum: with the checksum field set to 0, per byte sum = (sum << 1 | sum >> 31) + byte (32-bit)."""
    z = bytearray(b); z[0x0c:0x10] = b'\0\0\0\0'; s = 0
    for c in z: s = (((s << 1) | (s >> 31)) + c) & 0xFFFFFFFF
    return s

import json
A, B = sys.argv[1], sys.argv[2]
opt = dict(x.split('=', 1) for x in sys.argv[3:] if x.startswith('--'))
def tsrc(side):
    if f'--{side}-time' in opt: v = int(opt[f'--{side}-time']); return (v, v, f'固定值 {v}')
    if f'--{side}-meta' in opt: m = json.load(open(opt[f'--{side}-meta'])); return (m['wall_start'], m['wall_end'], f'墙钟区间 [{m["wall_start"]}, {m["wall_end"]}]')
    return None
TS = {'A': tsrc('a'), 'B': tsrc('b')}
def check_time(lab, f, v):
    t = TS[lab]
    if t is None: print(f'  {f} {lab} 时间 {v} 未核（缺 --{lab.lower()}-meta / --{lab.lower()}-time）'); return 1
    if not t[0] <= v <= t[1]: print(f'  {f} {lab} 时间 {v} 不在{t[2]}内'); return 1
    return 0
fa, fb = files(A), files(B)
print(f'射程：A {len(fa)} 个文件，B {len(fb)} 个；.d2s A {sum(f.endswith(".d2s") for f in fa)} / B {sum(f.endswith(".d2s") for f in fb)}')
if not any(f.endswith('.d2s') for f in fa) or not any(f.endswith('.d2s') for f in fb): print('SAVECMP 射程不足'); sys.exit(3)
bad = 0
for f in sorted(fa ^ fb): print(f'  只在 {"A" if f in fa else "B"}：{f}'); bad += 1
for f in sorted(fa & fb):
    a = open(os.path.join(A, '0', f), 'rb').read(); b = open(os.path.join(B, '0', f), 'rb').read()
    if f.endswith('.d2s'):
        for lab, x in (('A', a), ('B', b)):
            if len(x) >= 16 and struct.unpack_from('<I', x, 0x0c)[0] != d2s_sum(x): print(f'  {f} {lab} 校验和不自洽'); bad += 1
        a2, b2 = bytearray(a), bytearray(b); a2[0x0c:0x10] = b2[0x0c:0x10] = b'\0\0\0\0'
        if a[4] == 0x69 and b[4] == 0x69 and a[0x20:0x24] != b[0x20:0x24]:     # 两边相同（本局没玩这个角色）不必核 | Identical on both sides (this character was not played in this run), no need to check
            for lab, x in (('A', a), ('B', b)): bad += check_time(lab, f, struct.unpack_from('<I', x, 0x20)[0])
        if a[4] == 0x69 and b[4] == 0x69: a2[0x20:0x24] = b2[0x20:0x24] = b'\0\0\0\0'
        a, b = bytes(a2), bytes(b2)
    if f.endswith('.d2s.meta'):
        if a != b:
            for lab, x in (('A', a), ('B', b)):
                mt = re.search(rb'"last_modified_time":(\d+)', x)
                if mt: bad += check_time(lab, f, int(mt.group(1)) // 10**9)      # 纳秒 | Nanoseconds
        a, b = (re.sub(rb'"last_modified_time":\d+', b'"last_modified_time":0', x) for x in (a, b))
    if a != b:
        bad += 1
        offs = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
        print(f'  不同：{f} 长 {len(a)}/{len(b)}，差 {len(offs) + abs(len(a) - len(b))} 字节，首处 {hex(offs[0]) if offs else hex(min(len(a), len(b)))}'
              + (f'（A {a[offs[0]:offs[0] + 8].hex()} / B {b[offs[0]:offs[0] + 8].hex()}）' if offs else ''))
print('SAVECMP 一致（校验和各自自洽、墙钟时间核过来源、其余逐字节全同）' if not bad else f'SAVECMP 不一致：{bad} 处'); sys.exit(1 if bad else 0)
