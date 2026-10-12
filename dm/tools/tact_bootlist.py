#!/usr/bin/env python3
# 启动块清单：从一局 DM_TACTCHK 记录（每次 TACT zlib 块解码一行：输入开头16字节hex \t 输入长度 \t 输出长度 \t 输出MD5 \t 发起线程句柄）
# 反查每块在 RomFS 存储里的位置，按首次请求顺序输出，同一块只留首次（mini A/B：去重不影响预取命中；按偏移排序则 ④ 慢约 0.07 s，顺序不能改）。宿主 DM_TACT_PREFETCH 按它提前解码（只是性能提示：块与清单不符时宿主退回原路径）。
# 输出每行：数据文件名 \t 块内容偏移（'Z' 之后） \t 压缩长度 \t 解码长度 \t 压缩数据开头16字节hex（宿主查缓存的键，免得启动时再读盘） \t 发起线程句柄（宿主按线程分流）
# 文件头一行「# BUILD_ID\t<main 构建号>」（取自 ExeFS/main 的 NSO 头 0x40–0x50，与改写器 cuts.tsv 同口径）：清单随产物目录（out/<版本>-rN/bootlist.tsv），
# 宿主核对产物 main.cuts.tsv 的构建号，不符整段不启用。出新 r 版本时与产物一起重出。
# 产物不再每版重录（记录有竞态：同产物三次记录块集合不同）：输出入库为 dm/data/bootlist_<构建号>.tsv，patch_nso.py 出 main 时拷进产物目录；
# 只在构建号变化或主动改进时重录，提交附 mini A/B 读数。
# 用法：tact_bootlist.py <DM_TACTCHK 记录> <输出清单> [RomFS 目录] [ExeFS/main]
# Boot block list: from the DM_TACTCHK record of one run (one line per TACT zlib block decode: first 16 input bytes hex \t input length \t output length \t output MD5 \t initiating thread handle)
# looks up each block's position in the RomFS storage and outputs them in first-request order, keeping only the first occurrence of a block (A/B on the second Mac: deduplication does not affect prefetch hits; sorting by offset makes ④ about 0.07 s slower, so the order must not be changed). The host's DM_TACT_PREFETCH decodes ahead of time according to it (a performance hint only: when a block does not match the list the host falls back to the original path).
# Each output line: data file name \t block content offset (after 'Z') \t compressed length \t decoded length \t first 16 bytes of compressed data hex (the key the host uses to look up its cache, sparing another disk read at startup) \t initiating thread handle (the host dispatches by thread)
# One header line at the top of the file, "# BUILD_ID\t<main build ID>" (taken from 0x40–0x50 of the NSO header of ExeFS/main, same convention as the rewriter's cuts.tsv): the list goes with the build output directory (out/<version>-rN/bootlist.tsv),
# the host checks it against the build ID in the build output's main.cuts.tsv, and on mismatch the whole thing is not enabled. When a new r version is issued it is regenerated together with the build output.
# Build outputs are no longer re-recorded for every version (recording is racy: three recordings of the same build output gave different block sets): the output is checked in as dm/data/bootlist_<build ID>.tsv, and patch_nso.py copies it into the build output directory when producing main;
# re-record only when the build ID changes or for a deliberate improvement, and attach A/B readings from the second Mac to the commit.
# Usage: tact_bootlist.py <DM_TACTCHK record> <output list> [RomFS dir] [ExeFS/main]
import sys, os, mmap, struct, zlib, hashlib, collections
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
chk, out = sys.argv[1], sys.argv[2]
R = (sys.argv[3] if len(sys.argv) > 3 else os.path.join(DM_ROOT, 'data/switch/3.3.94012/Update/RomFS')) + '/data'
NSO = sys.argv[4] if len(sys.argv) > 4 else os.path.join(DM_ROOT, 'data/switch/3.3.94012/Update/ExeFS/main')
BUILD_ID = open(NSO, 'rb').read(0x50)[0x40:0x50].hex()
maps = {}
def mm(fn):
    if fn not in maps: fh = open(f'{R}/{fn}', 'rb'); maps[fn] = (fh, mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ))
    return maps[fn][1]
idx = collections.defaultdict(list)                     # (开头16字节hex, 压缩长) → [(文件, 偏移)]，同键可多处（撞键） | (first 16 bytes hex, compressed length) → [(file, offset)]; the same key may be in several places (key collision)
for l in open(f'{R}/build_manifest.psv', encoding='utf-8'):
    if l.startswith(('FileId', '#')): continue
    f = l.rstrip('\n').split('|')
    if len(f) < 10: continue
    fn = f'{int(f[2]):02d}-{int(f[3]):08d}.data'; off = int(f[6][22:32], 16); es = int(f[7]); enc = f[8]; m = mm(fn)
    if enc == 'z': idx[(m[off:off + 16].hex(), es)].append((fn, off)); continue
    if enc.startswith('b') and m[off:off + 4] == b'BLTE':
        hs = struct.unpack_from('>I', m, off + 4)[0]
        if hs == 0: continue
        nc = struct.unpack_from('>I', m, off + 8)[0] & 0xffffff; p = off + hs
        for k in range(nc):
            ce, cd = struct.unpack_from('>II', m, off + 12 + k * 24)
            if m[p:p + 1] == b'Z': idx[(m[p + 1:p + 17].hex(), ce - 1)].append((fn, p + 1))
            p += ce
rows = []; seen = set(); miss = amb = 0
for l in open(chk):
    q = l.rstrip('\n').split('\t')
    if len(q) < 4: continue
    if len(q[0]) != 32 or len(q[3]) != 32: continue      # 进程到点退出时没写完的残行 | Partial line left unfinished when the process exited at its deadline
    h, n, dl, md = q[0], int(q[1]), int(q[2]), q[3]; th = q[4] if len(q) > 4 and q[4].startswith('0x') else '0'
    cands = idx.get((h, n), [])
    if len(cands) > 1: amb += 1
    hit = None
    for fn, off in cands:                               # 同键多处：按输出 MD5 选中真正那一块 | Same key in several places: pick the real block by output MD5
        if len(cands) == 1 or hashlib.md5(zlib.decompress(mm(fn)[off:off + n])).hexdigest() == md: hit = (fn, off); break
    if not hit: miss += 1; continue
    if hit in seen: continue
    seen.add(hit)
    rows.append(f'{hit[0]}\t{hit[1]}\t{n}\t{dl}\t{h}\t{th}')
with open(out, 'w') as f: f.write(f'# BUILD_ID\t{BUILD_ID}\n' + '\n'.join(rows) + '\n')
print(f'启动块清单（main 构建号 {BUILD_ID}）{len(rows)} 块（撞键消歧 {amb}，查不到 {miss}）：压缩 {sum(int(r.split()[2]) for r in rows)/1048576:.1f} MiB → 解码 {sum(int(r.split()[3]) for r in rows)/1048576:.1f} MiB → {out}')
