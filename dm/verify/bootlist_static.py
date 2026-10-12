#!/usr/bin/env python3
"""启动块清单静态核对（预取是性能优化，闸门只核清单本身对不对，运行时命中数只报不判）。
用法：bootlist_static.py <清单.tsv> [RomFS/data 目录]
逐行核：数据文件存在；偏移 + 压缩长不越出文件；(文件, 偏移) 不重复；文件该偏移处的开头 16 字节与清单记录一致
（这一条证明清单块真实存在于当前资源包，与运行时调度无关）。末行 BOOTSTATIC OK|RED …；退出码 0/1。
Static check of the boot block list (prefetch is a performance optimization; the gate check only checks whether the list itself is correct, and runtime hit counts are only reported, not judged).
Usage: bootlist_static.py <list.tsv> [RomFS/data directory]
Checked line by line: the data file exists; offset + compressed length does not go past the file; (file, offset) is not duplicated; the first 16 bytes at that offset in the file match the list record
(this one proves the listed block really exists in the current resource package, independent of runtime scheduling). Last line BOOTSTATIC OK|RED ...; exit code 0/1.
"""
import os, sys
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
lst = sys.argv[1]
rom = sys.argv[2] if len(sys.argv) > 2 else os.path.join(DM_ROOT, 'data/switch/3.3.94012/Update/RomFS/data')
bad, seen, n, fhs = [], set(), 0, {}
holes = {}   # 剔零档案（<档案>.holes 区段表，宿主 hle_fs.c 同口径）：文件 → (原始大小, [(零段起, 零段止)…]) | zero-stripped archives (<archive>.holes extent table, same rule as the host's hle_fs.c): file -> (original size, [(zero run start, zero run end)...])
def hole_info(p):
    if p not in holes:
        hp = p + '.holes'; holes[p] = None
        if os.path.isfile(hp):
            ls = open(hp).read().split('\n'); holes[p] = (int(ls[0].split()[1]), [(int(x.split()[0]), int(x.split()[0]) + int(x.split()[1])) for x in ls[1:] if x.strip()])
    return holes[p]
def phys(p, off, ln):   # 原始偏移 → 剔零文件里的偏移；整段须落在保留段里，否则 None | original offset -> offset in the zero-stripped file; the whole range must fall inside a kept run, otherwise None
    h = hole_info(p)
    if h is None: return off
    cut = 0
    for a, b in h[1]:
        if b <= off: cut += b - a
        elif a < off + ln: return None
    return off - cut
for i, line in enumerate(open(lst), 1):
    if line.startswith('#') or not line.strip():
        continue
    n += 1
    f, off, clen, _dlen, head = line.split('\t')[:5]
    off, clen = int(off), int(clen)
    p = os.path.join(rom, f)
    if (f, off) in seen:
        bad.append(f'第 {i} 行 重复 {f}@{off}'); continue
    seen.add((f, off))
    if f not in fhs:
        fhs[f] = open(p, 'rb') if os.path.isfile(p) else None
    fh = fhs[f]
    if fh is None:
        bad.append(f'第 {i} 行 数据文件不存在 {p}'); continue
    h = hole_info(p)
    if off + clen > (h[0] if h else os.fstat(fh.fileno()).st_size):
        bad.append(f'第 {i} 行 越界 {f}@{off}+{clen}'); continue
    po = phys(p, off, clen)
    if po is None:
        bad.append(f'第 {i} 行 落在剔掉的零段里 {f}@{off}+{clen}'); continue
    fh.seek(po)
    if fh.read(len(head) // 2).hex() != head:
        bad.append(f'第 {i} 行 开头字节不符 {f}@{off}')
if n == 0:
    bad.append('清单 0 块')
for b in bad[:5]:
    print(b)
print(f'BOOTSTATIC {"OK" if not bad else "RED"} {n} 块，不符 {len(bad)} 处（{os.path.basename(lst)}，资源包 {rom}）')
sys.exit(1 if bad else 0)
