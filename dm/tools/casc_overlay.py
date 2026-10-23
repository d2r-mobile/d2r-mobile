#!/usr/bin/env python3
"""构建期改写 Switch 静态 CASC：让指定逻辑路径指向新内容（原生方式，不用 D2R mod、运行时零改动）。

用法：casc_overlay.py <RomFS> <输出目录> <替换表.tsv>
替换表每行「逻辑路径<TAB>新文件」。输出目录 = 覆盖层：改动文件实写，其余逐文件软链接原件；空表 ⇒ 全部链接、逐字节等于原件。

格式（对原版实算核过，游戏实际读取以 DM_FSLOG 为准）：
- 游戏只读 data/.build.config 与 data/NN-MMMMMMMM.data；*.psv 清单是提取工具产物，游戏不读（这里只借它查路径 → 旧 EKey，
  再在 TVFS 容器文件表里按该 EKey 唯一命中定位条目，命中数 ≠ 1 即报错）。
- .build.config 的 vfs-root（154 字节小 TVFS）按 EKey 引用 vfs-1（全量 TVFS）；vfs-2 与 vfs-root 同键。改文件 ⇒ 新内容、新 vfs-1、
  新 vfs-root 都写进一个新数据档，再改 .build.config 的 vfs-root / vfs-1 / vfs-2 三行及其 -size。
- EKey（16 字节，key-layout 8 8 40）= MD5(存储头)[:9] + 块号 + 档号 + 40 位偏移；存储头：BLTE 为其头部，裸 zlib / 原样为整段。
- 按条目规格编码：z = 裸 zlib（级别 9 可逐字节复现原件）、n = 原样、b:… = BLTE（块校验 = MD5(编码块，含模式字节)）。
- TVFS 容器文件表条目（flags 0x0a）：EKey(16) + 编码长(4) + 规格序号(1) + 内容长(4)。
- 新增路径：替换表里逻辑路径前加「+」（如 +data/hd/items/weapon/zswg/axe__the_gnasher.json）。只支持「已有目录下新开一个子目录、里面放文件」：
  新子目录名的首字母不许与该目录现有子项相同（路径树是压缩前缀树，兄弟项首字母互异），按字节序插在兄弟间；新文件在容器文件表 / 文件表末尾各追加一条，
  祖先目录的尺寸与表头偏移随之更新。写完把整棵路径树重新解析一遍，核「原有路径与文件表位置一个不变 + 新路径都在」。
- 路径树单项格式（对原版 13 万条路径核过）：[0 = 前置分隔符] [长度 + 名字片段] [0 = 后置分隔符] [FF + 值 4 字节]；
  名字后既不是 0 也不是 FF = 隐含分隔符、名字没完（下一项接着拼）；值最高位 = 目录，低 31 位 = 尺寸（目录内容止 = 值之后 + 尺寸 − 4），否则 = 文件表内偏移。
  原版里 baal、endgame2 两处目录的分隔符写法与此不同（兄弟项共用首项的前置分隔符），新增路径不往这类目录里插。
- 独立数据档（老机型补充包）：`pack` 子命令把一批替换文件按各自条目的编码规格写进指定档号的数据档，并出条目表
  （逻辑路径、EKey、编码长、内容长）；生成覆盖层时 `--prebuilt 条目表` 让这些路径的 TVFS 条目直接指向那个数据档，内容不再复制进覆盖层的新档。
  覆盖层自己的新档里，其余替换按原顺序写、索引放最后，所以有没有 --prebuilt，同一批替换在新档里的偏移不变（预取清单照常命中）。
Controller/HUDPanelHD.json 的 HealthBall x 141→441，标准存档、不加 -mod 进局，血球右移、HUD 其余不变、无【红】；
EKey 或块校验算错时游戏读到新 vfs-root 即退出（有校验）。
Build-time rewrite of the Switch static CASC: makes the given logical paths point to new content (the native way, no D2R mod, zero runtime changes).
Usage: casc_overlay.py <RomFS> <output dir> <replacement-table.tsv>
Each line of the replacement table is "logical path<TAB>new file". Output dir = overlay: changed files are actually written, the rest are symlinked file by file to the originals; empty table ⇒ everything linked, byte-for-byte equal to the originals.
Format (computed and verified against the original game; what the game actually reads is determined by DM_FSLOG):
- The game reads only data/.build.config and data/NN-MMMMMMMM.data; the *.psv manifests are output of the extraction tool and the game does not read them (here they are only borrowed to look up path → old EKey,
  then the entry is located in the TVFS container file table by a unique hit on that EKey; a hit count ≠ 1 is an error).
- The vfs-root in .build.config (a small 154-byte TVFS) references vfs-1 (the full TVFS) by EKey; vfs-2 has the same key as vfs-root. Changing a file ⇒ the new content, the new vfs-1 and
  the new vfs-root are all written into one new data archive, then the three lines vfs-root / vfs-1 / vfs-2 in .build.config and their -size are changed.
- EKey (16 bytes, key-layout 8 8 40) = MD5(storage header)[:9] + chunk number + archive number + 40-bit offset; storage header: for BLTE its header, for raw zlib / as is the whole segment.
- Encode by entry spec: z = raw zlib (level 9 reproduces the original byte for byte), n = as is, b:... = BLTE (block checksum = MD5(encoded block, including the mode byte)).
- TVFS container file table entry (flags 0x0a): EKey(16) + encoded length(4) + spec index(1) + content length(4).
- Adding paths: prefix the logical path in the replacement table with "+" (e.g. +data/hd/items/weapon/zswg/axe__the_gnasher.json). Only "open one new subdirectory under an existing directory and put files in it" is supported:
  the first letter of the new subdirectory name must not equal that of any existing child of that directory (the path tree is a compressed prefix tree, sibling items have distinct first letters), and it is inserted among the siblings in byte order; each new file appends one record at the end of the container file table / file table,
  and the sizes of the ancestor directories and the header offsets are updated accordingly. After writing, the whole path tree is parsed again to verify "no existing path or file table position changed + all new paths present".
- Format of a single path tree item (verified against the original game's 130 thousand paths): [0 = leading separator] [length + name fragment] [0 = trailing separator] [FF + 4-byte value];
  if what follows the name is neither 0 nor FF = implicit separator, the name is unfinished (the next item continues it); top bit of the value = directory, low 31 bits = size (directory content end = after the value + size − 4), otherwise = offset within the file table.
  In the original game the directories baal and endgame2 write their separators differently (sibling items share the first item's leading separator); added paths are not inserted into such directories.
- Standalone data archive (Legacy Device Pack): the `pack` subcommand writes a batch of replacement files into the data archive with the given archive number using each entry's own encoding spec, and produces an entry table
  (logical path, EKey, encoded length, content length); when generating the overlay, `--prebuilt entry-table` makes the TVFS entries of those paths point directly at that data archive, and the content is no longer copied into the overlay's new archive.
  In the overlay's own new archive the remaining replacements are written in the original order with the index last, so with or without --prebuilt the offsets of the same batch of replacements in the new archive do not change (the prefetch list still hits as usual).
Controller/HUDPanelHD.json HealthBall x 141→441, standard save, entering a run without -mod: the health globe moves right, the rest of the HUD is unchanged, no 【红】 (red);
when the EKey or block checksum is computed wrong the game exits as soon as it reads the new vfs-root (it is checksummed).
"""

import hashlib
from pathlib import Path
import struct
import sys
import zlib


def die(message):
    raise SystemExit(f"casc_overlay.py: {message}")


def norm_path(value):
    value = value.strip().replace("\\", "/")
    while value.lower().startswith("data:"):
        value = value[5:]
    return value.lstrip("/").lower()


def read_replacements(filename):
    result = {}
    with open(filename, encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 2:
                die(f"{filename}:{number}: expected path<TAB>file")
            add = fields[0].startswith("+")
            logical, replacement = ("+" if add else "") + norm_path(fields[0].lstrip("+")), Path(fields[1])
            if not logical or logical in result:
                die(f"{filename}:{number}: duplicate or empty logical path")
            if not replacement.is_file():
                die(f"{filename}:{number}: replacement is not a file: {replacement}")
            result[logical] = replacement
    return result


def link_tree(source, output, skip=()):
    output.mkdir(parents=True, exist_ok=True)
    for entry in source.iterdir():
        if entry.name in skip:
            continue
        target = output / entry.name
        if entry.is_dir() and not entry.is_symlink():
            link_tree(entry, target)
        else:
            target.symlink_to(entry.resolve())


def read_manifest(source):
    result = {}
    filename = source / "data" / "build_manifest.psv"
    if not filename.is_file():
        die(f"missing {filename}; TVFS lookup needs the extraction manifest")
    with filename.open(encoding="utf-8") as stream:
        for line in stream:
            fields = line.rstrip("\n").split("|")
            if len(fields) < 11 or fields[0].startswith("#"):
                continue
            path = norm_path(fields[9])
            if path and path not in result:
                result[path] = fields
    return result


def read_data_file(data_dir, ekey, encoded_size):
    chunk, archive = ekey[9], ekey[10]
    filename = data_dir / f"{chunk:02d}-{archive:08d}.data"
    with filename.open("rb") as stream:
        stream.seek(int.from_bytes(ekey[11:16], "big"))
        raw = stream.read(encoded_size)
    if raw[:4] != b"BLTE":
        die(f"expected BLTE at {filename}:{int.from_bytes(ekey[11:16], 'big'):#x}")
    header_size = struct.unpack(">I", raw[4:8])[0]
    block_count = int.from_bytes(raw[9:12], "big")
    position, output = header_size, bytearray()
    for index in range(block_count):
        encoded, decoded = struct.unpack(">II", raw[12 + index * 24:20 + index * 24])
        block = raw[position:position + encoded]
        if block[:1] == b"N":
            content = block[1:]
        elif block[:1] == b"Z":
            content = zlib.decompress(block[1:])
        else:
            die(f"unsupported BLTE block mode {block[:1]!r}")
        if len(content) != decoded:
            die("BLTE decoded length mismatch")
        output.extend(content)
        position += encoded
    return bytes(output)


def blte_encode(content, block_size=256 * 1024):
    blocks = []
    for start in range(0, len(content), block_size):
        block = content[start:start + block_size]
        blocks.append((b"Z" + zlib.compress(block, 9), block))
    if not blocks:
        blocks.append((b"N", b""))
    header_size = 12 + 24 * len(blocks)
    header = bytearray(b"BLTE" + struct.pack(">I", header_size) + b"\x0f" + len(blocks).to_bytes(3, "big"))
    for encoded, block in blocks:
        header.extend(struct.pack(">II", len(encoded), len(block)))
        header.extend(hashlib.md5(encoded).digest())   # 块校验 = MD5(编码后的块，含模式字节)；对原版 vfs-root / vfs-1 实算核过 | Block checksum = MD5(encoded block, including the mode byte); computed and verified against the original game's vfs-root / vfs-1
    return bytes(header) + b"".join(encoded for encoded, _ in blocks)


def available_location(data_dir):
    used = set()
    for entry in data_dir.glob("??-????????.data"):
        try:
            used.add((int(entry.name[:2]), int(entry.name[3:11])))
        except ValueError:
            continue
    for chunk in range(256):
        for archive in range(1, 0x100000000):
            if (chunk, archive) not in used:
                return chunk, archive
    die("no unused data-file location")


def config_values(filename):
    lines = filename.read_text(encoding="utf-8").splitlines(keepends=True)
    values = {}
    for line in lines:
        if " = " in line and not line.startswith("#"):
            key, value = line.rstrip("\n").split(" = ", 1)
            values[key] = value
    return lines, values


def replace_config(lines, key, value):
    prefix = key + " = "
    return [prefix + value + "\n" if line.startswith(prefix) else line for line in lines]


def path_entry(tvfs, pos):
    """路径树单项 → (下一项起点, 前置分隔符, 名字, 后置分隔符 0 无 / 1 显式 / 2 隐含, 值或 None) | Single path tree item → (next item start, leading separator, name, trailing separator 0 none / 1 explicit / 2 implicit, value or None)"""
    pre = post = 0
    name = b""
    value = None
    if tvfs[pos] == 0:
        pre, pos = 1, pos + 1
    if tvfs[pos] != 0xFF:
        length = tvfs[pos]
        name, pos = bytes(tvfs[pos + 1:pos + 1 + length]), pos + 1 + length
    if tvfs[pos] == 0:
        post, pos = 1, pos + 1
    if tvfs[pos] == 0xFF:
        value, pos = struct.unpack(">I", tvfs[pos + 1:pos + 5])[0], pos + 5
    else:
        post = 2
    return pos, pre, name, post, value


def walk_paths(tvfs, start, end, prefix=b"", parents=()):
    """逐项产出 (项起点, 到本项为止的路径, 值, 目录内容 (起, 止) 或 None, 祖先目录项起点) | Yields per item (item start, path up to this item, value, directory content (start, end) or None, ancestor directory item starts)"""
    pos, base = start, prefix
    while pos < end:
        entry = pos
        pos, pre, name, post, value = path_entry(tvfs, pos)
        prefix = prefix + (b"/" if pre else b"") + name + (b"/" if post else b"")
        if value is None:
            continue
        if value & 0x80000000:
            folder_end = pos + (value & 0x7FFFFFFF) - 4
            yield entry, prefix, value, (pos, folder_end), parents
            yield from walk_paths(tvfs, pos, folder_end, prefix, parents + (entry,))
            pos = folder_end
        else:
            yield entry, prefix, value, None, parents
        prefix = base


def trie_bytes(names):
    """一个目录里的文件 {名字 bytes: 文件表偏移} → 压缩前缀树编码（兄弟项首字母互异）；返回 (字节, 最大嵌套层数) | Files in one directory {name bytes: file table offset} → compressed prefix tree encoding (sibling items have distinct first letters); returns (bytes, maximum nesting depth)"""
    out, depth = bytearray(), 0
    groups = {}
    for name in sorted(names):
        groups.setdefault(name[:1], []).append(name)
    for _, group in sorted(groups.items()):
        if len(group) == 1:
            name = group[0]
            out += bytes((len(name),)) + name + b"\xff" + struct.pack(">I", names[name])
            continue
        common = group[0]
        for name in group[1:]:
            while not name.startswith(common):
                common = common[:-1]
        if any(name == common for name in group):
            die("新增路径：一个文件名是另一个的前缀，前缀树编码不了: " + common.decode())
        sub, sub_depth = trie_bytes({name[len(common):]: names[name] for name in group})
        out += bytes((len(common),)) + common + b"\xff" + struct.pack(">I", 0x80000000 | (len(sub) + 4)) + sub
        depth = max(depth, sub_depth + 1)
    if any(len(name) > 0xFE for name in names):
        die("新增路径：名字片段超过 254 字节")
    return out, depth


def offset_field_size(size):
    return 1 if size <= 0xFF else 2 if size <= 0xFFFF else 3 if size <= 0xFFFFFF else 4


def add_paths(tvfs, additions):
    """additions：{逻辑路径 str: (EKey 16 字节, 编码长, 规格序号, 内容长)} → 加好路径的新 TVFS（bytearray） | additions: {logical path str: (EKey 16 bytes, encoded length, spec index, content length)} → new TVFS with the paths added (bytearray)"""
    path_off, path_size, vfs_off, vfs_size, cft_off, cft_size = struct.unpack(">IIIIII", tvfs[12:36])
    est_off, est_size = struct.unpack(">II", tvfs[38:46])
    if not (path_off + path_size == est_off and est_off + est_size == cft_off and cft_off + cft_size == vfs_off and vfs_off + vfs_size == len(tvfs)):
        die("TVFS 各表不是首尾相接的（头 / 路径树 / 规格表 / 容器文件表 / 文件表），不认识这种布局")
    before = {path: value for _, path, value, folder, _ in walk_paths(tvfs, path_off, path_off + path_size) if folder is None}
    nodes = {path: (entry, folder, parents) for entry, path, value, folder, parents in walk_paths(tvfs, path_off, path_off + path_size) if folder}
    cft_field = offset_field_size(cft_size)
    if offset_field_size(cft_size + 25 * len(additions)) != cft_field:
        die("新增路径会让容器文件表偏移字段变宽，要重写全部文件表条目（没实现）")
    vfs_entry = 1 + 4 + 4 + cft_field
    new_cft, new_vfs = bytearray(), bytearray()
    groups = {}   # (父目录, 新子目录) → {文件名: 文件表偏移} | (parent directory, new subdirectory) → {file name: file table offset}
    for number, (logical, (ekey, encoded, espec, content)) in enumerate(sorted(additions.items())):
        parts = logical.encode().rsplit(b"/", 2)
        if len(parts) != 3:
            die("新增路径须是「已有目录/新子目录/文件」: " + logical)
        parent, folder, name = parts[0] + b"/", parts[1], parts[2]
        groups.setdefault((parent, folder), {})[name] = vfs_size + number * vfs_entry
        new_cft += ekey + struct.pack(">I", encoded) + bytes((espec,)) + struct.pack(">I", content)
        new_vfs += b"\x01" + struct.pack(">II", 0, content) + (cft_size + number * 25).to_bytes(cft_field, "big")
    path = bytearray(tvfs[path_off:path_off + path_size])   # 以下位置都相对路径树起点 | All positions below are relative to the start of the path tree
    grown = {}   # 祖先目录项起点（相对）→ 增量 | Ancestor directory item start (relative) → delta
    inserts = []
    max_depth = struct.unpack(">H", tvfs[36:38])[0]
    for (parent, folder), names in sorted(groups.items()):
        if parent.lower() not in {key.lower() for key in nodes}:
            die("新增路径：父目录在路径树里不是一个目录项: " + parent.decode())
        entry, (start, end), parents = next(nodes[key] for key in nodes if key.lower() == parent.lower())
        if any(key.lower().startswith(parent.lower() + folder.lower()) for key in list(before) + list(nodes)):
            die("新增路径：子目录已存在或与已有名字同前缀: " + (parent + folder).decode())
        sub, depth = trie_bytes(names)
        block = bytes((len(folder),)) + folder + b"\x00\xff" + struct.pack(">I", 0x80000000 | (len(sub) + 4)) + sub
        at, pos, cont = end, start, False
        while pos < end:   # 父目录的直接子项：首字母互异、按字节序；插在第一个首字母更大的前面（名字没完的续项不算新子项） | Direct children of the parent directory: first letters all differ, in byte order; insert before the first one with a larger first letter (a continuation item whose name is unfinished does not count as a new child)
            child = pos
            pos, pre, name, post, value = path_entry(tvfs, pos)
            first, was_cont, cont = (b"/" if pre else name[:1]), cont, value is None
            if was_cont:
                if value is not None and value & 0x80000000:
                    pos = pos + (value & 0x7FFFFFFF) - 4
                continue
            if first == folder[:1]:
                die("新增路径：新子目录首字母与已有子项冲突: " + (parent + folder).decode())
            if first > folder[:1] and at == end:
                at = child
            if value is not None and value & 0x80000000:
                pos = pos + (value & 0x7FFFFFFF) - 4
        inserts.append((at - path_off, block))
        for ancestor in parents + (entry,):
            grown[ancestor - path_off] = grown.get(ancestor - path_off, 0) + len(block)
        max_depth = max(max_depth, len(parents) + 2 + depth)
    for rel, delta in grown.items():   # 祖先目录尺寸（项内位置不受插入影响：插入点都在各自目录内容里，晚于值字段） | Ancestor directory sizes (positions inside an item are not affected by insertion: every insertion point is inside its own directory's content, after the value field)
        value_at = path_entry(path, rel)[0] - 4
        value = struct.unpack(">I", path[value_at:value_at + 4])[0]
        path[value_at:value_at + 4] = struct.pack(">I", 0x80000000 | ((value & 0x7FFFFFFF) + delta))
    for rel, block in sorted(inserts, reverse=True):
        path[rel:rel] = block
    head = bytearray(tvfs[:path_off])
    est = tvfs[est_off:est_off + est_size]
    cft = tvfs[cft_off:cft_off + cft_size] + new_cft
    vfs = tvfs[vfs_off:vfs_off + vfs_size] + new_vfs
    n_path_off = path_off
    n_est_off = n_path_off + len(path)
    n_cft_off = n_est_off + len(est)
    n_vfs_off = n_cft_off + len(cft)
    head[12:36] = struct.pack(">IIIIII", n_path_off, len(path), n_vfs_off, len(vfs), n_cft_off, len(cft))
    head[36:38] = struct.pack(">H", max_depth)
    head[38:42] = struct.pack(">I", n_est_off)
    result = head + path + est + cft + vfs
    after = {p: v for _, p, v, folder, _ in walk_paths(result, n_path_off, n_path_off + len(path)) if folder is None}
    added = {p: v for p, v in after.items() if p not in before}
    if any(after.get(p) != v for p, v in before.items()):
        die("新增路径自检：原有路径或其文件表位置变了")
    if sorted(p.decode().lower() for p in added) != sorted(additions):
        die(f"新增路径自检：解析出来的新路径与要加的不一致（{len(added)} / {len(additions)}）")
    return result


def set_cft_sizes(tvfs, pos, encoded_size, content_size):
    """TVFS 容器文件表条目（本构建 flags=0x0a：带编码规格表、无 CKey）：EKey(16) + 编码长(4) + 规格序号(1) + 内容长(4)。
    规格序号宽度 = 表示规格表大小所需字节数（vfs-root 表 12 字节、vfs-1 表 13 字节 ⇒ 1），序号原样保留。
    TVFS container file table entry (this build has flags=0x0a: with encoding spec table, no CKey): EKey(16) + encoded length(4) + spec index(1) + content length(4).
        Spec index width = number of bytes needed to represent the spec table size (vfs-root table 12 bytes, vfs-1 table 13 bytes ⇒ 1); the index is kept as is."""
    est_size = struct.unpack(">I", tvfs[42:46])[0]
    idx = 1 if est_size <= 0xff else 2 if est_size <= 0xffff else 3 if est_size <= 0xffffff else 4
    tvfs[pos + 16:pos + 20] = struct.pack(">I", encoded_size)
    tvfs[pos + 20 + idx:pos + 24 + idx] = struct.pack(">I", content_size)


def cft_position(vfs_content, cft_offset, cft_end, manifest, logical):
    """逻辑路径 → 它在 TVFS 容器文件表里的条目位置（按旧 EKey 唯一命中） | Logical path → the position of its entry in the TVFS container file table (unique hit by the old EKey)"""
    row = manifest.get(logical)
    if row is None:
        die("path not found in build_manifest.psv: " + logical)
    old_ekey = bytes.fromhex(row[6])
    matches = []
    search_at = cft_offset
    while True:
        position = vfs_content.find(old_ekey, search_at, cft_end)
        if position < 0:
            break
        matches.append(position)
        search_at = position + 1
    if len(matches) != 1:
        die(f"CFT key match count for {logical}: {len(matches)}")
    return matches[0]


def load_vfs1(source):
    data_dir = source / "data"
    lines, values = config_values(data_dir / ".build.config")
    vfs_key, vfs_ekey = values["vfs-1"].split()
    vfs_decoded_size, vfs_encoded_size = map(int, values["vfs-1-size"].split())
    vfs_content = bytearray(read_data_file(data_dir, bytes.fromhex(vfs_ekey), vfs_encoded_size))
    if len(vfs_content) != vfs_decoded_size or vfs_content[:4] != b"TVFS":
        die("vfs-1 is not a valid TVFS manifest")
    return vfs_content


def encode_by_espec(content, espec):
    # 按条目规格编码（对原版实算）：z = 裸 zlib（级别 9 可逐字节复现原件）、n = 原样、b:… = BLTE 容器
    # Encode by entry spec (computed and checked against the original game): z = raw zlib (level 9 reproduces the original byte for byte), n = as is, b:... = BLTE container
    return zlib.compress(content, 9) if espec == "z" else content if espec == "n" else blte_encode(content)


def ekey_for(encoded, chunk, archive, offset):
    header_size = struct.unpack(">I", encoded[4:8])[0] if encoded[:4] == b"BLTE" else len(encoded)
    return hashlib.md5(encoded[:header_size]).digest()[:9] + bytes((chunk, archive)) + offset.to_bytes(5, "big")   # EKey 前 9 字节 = MD5(BLTE 头)，后 7 字节 = 块号 / 档号 / 偏移（key-layout 8 8 40） | First 9 bytes of EKey = MD5(BLTE header), last 7 bytes = chunk number / archive number / offset (key-layout 8 8 40)


def write_pack(source, output, replacements, location):
    """独立数据档：replacements 逐条按其 TVFS 条目的编码规格写进 output/<块号>-<档号>.data，出条目表 output/pack_index.tsv。
    location = (块号, 档号)，须是原 RomFS 里没用过的位置。各条编码互不依赖，用多进程并行压缩
    Standalone data archive: writes each of replacements into output/<chunk number>-<archive number>.data using the encoding spec of its TVFS entry, and produces the entry table output/pack_index.tsv.
        location = (chunk number, archive number), which must be a location not used in the original RomFS. The entries are encoded independently, so compression runs in parallel across multiple processes"""
    import concurrent.futures as cf
    chunk, archive = location
    data_dir = source / "data"
    name = f"{chunk:02d}-{archive:08d}.data"
    if (data_dir / name).exists():
        die(f"pack location {name} already exists in source RomFS")
    vfs_content = load_vfs1(source)
    manifest = read_manifest(source)
    cft_offset, cft_size = struct.unpack(">II", vfs_content[28:36])
    est_off, est_size = struct.unpack(">II", vfs_content[38:46])
    especs = bytes(vfs_content[est_off:est_off + est_size]).split(b"\0")
    items = []
    for logical, replacement in replacements.items():
        if logical.startswith("+"):
            die("pack 不支持新增路径: " + logical)
        pos = cft_position(vfs_content, cft_offset, cft_offset + cft_size, manifest, logical)
        items.append((logical, replacement, especs[vfs_content[pos + 20]].decode()))
    output.mkdir(parents=True, exist_ok=True)
    offset = 0
    with (output / name).open("wb") as data, (output / "pack_index.tsv").open("w", encoding="utf-8") as index, cf.ProcessPoolExecutor() as pool:
        index.write(f"# pack\t{name}\n")
        for (logical, _, _), (encoded, decoded_size) in zip(items, pool.map(_encode_one, [(str(r), e) for _, r, e in items], chunksize=16)):
            ekey = ekey_for(encoded, chunk, archive, offset)
            data.write(encoded)
            index.write(f"{logical}\t{ekey.hex()}\t{len(encoded)}\t{decoded_size}\n")
            offset += len(encoded)
    print(f"pack: {len(items)} file(s) → {name}, {offset} bytes")


def _encode_one(job):
    path, espec = job
    content = Path(path).read_bytes()
    return encode_by_espec(content, espec), len(content)


def read_prebuilt(filename):
    result = {}
    for line in Path(filename).read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        logical, ekey, encoded_size, decoded_size = line.split("\t")
        result[norm_path(logical)] = (bytes.fromhex(ekey), int(encoded_size), int(decoded_size))
    return result


def rewrite_tvfs(source, output, replacements, prebuilt=None, link_source=True):
    data_dir = source / "data"
    config = data_dir / ".build.config"
    lines, values = config_values(config)
    if "vfs-1" not in values or "vfs-1-size" not in values:
        die(f"missing vfs-1 in {config}")
    vfs_key, vfs_ekey = values["vfs-1"].split()
    vfs_decoded_size, vfs_encoded_size = map(int, values["vfs-1-size"].split())
    vfs_content = bytearray(read_data_file(data_dir, bytes.fromhex(vfs_ekey), vfs_encoded_size))
    if len(vfs_content) != vfs_decoded_size or vfs_content[:4] != b"TVFS":
        die("vfs-1 is not a valid TVFS manifest")

    manifest = read_manifest(source)
    cft_offset, cft_size = struct.unpack(">II", vfs_content[28:36])
    cft_end = cft_offset + cft_size
    if cft_end > len(vfs_content):
        die("unsupported TVFS CFT bounds")

    updates = []
    adds = {k[1:]: v for k, v in replacements.items() if k.startswith("+")}
    replacements = {k: v for k, v in replacements.items() if not k.startswith("+")}
    for logical, replacement in replacements.items():
        updates.append((cft_position(vfs_content, cft_offset, cft_end, manifest, logical), replacement.read_bytes()))
    prebuilt_pos = [(cft_position(vfs_content, cft_offset, cft_end, manifest, logical), entry) for logical, entry in (prebuilt or {}).items()]

    new_chunk, new_archive = available_location(data_dir)
    new_data = bytearray()

    def append(content, espec="b:256K*=z"):
        encoded = encode_by_espec(content, espec)
        offset = len(new_data)
        new_data.extend(encoded)
        return ekey_for(encoded, new_chunk, new_archive, offset), len(content), len(encoded)

    est_off, est_size = struct.unpack(">II", vfs_content[38:46])
    especs = bytes(vfs_content[est_off:est_off + est_size]).split(b"\0")
    for pos, content in updates:
        espec = especs[vfs_content[pos + 20]].decode()   # 规格序号 1 字节（规格表 13 字节） | Spec index is 1 byte (spec table is 13 bytes)
        ekey, decoded_size, encoded_size = append(content, espec)
        vfs_content[pos:pos + 9] = ekey[:9]
        vfs_content[pos + 9:pos + 16] = ekey[9:]
        set_cft_sizes(vfs_content, pos, encoded_size, decoded_size)
    for pos, (ekey, encoded_size, decoded_size) in prebuilt_pos:   # 独立数据档里已写好的条目：只改指向，不复制内容 | Entries already written in the standalone data archive: only repoint them, do not copy the content
        vfs_content[pos:pos + 16] = ekey
        set_cft_sizes(vfs_content, pos, encoded_size, decoded_size)

    if adds:
        espec_index = next((i for i, e in enumerate(especs) if e == b"z"), 0)   # 新文件用裸 zlib（与原版小文件同） | New files use raw zlib (same as small files in the original game)
        encoded_adds = {}
        for logical, replacement in adds.items():
            if logical in manifest:
                die("新增路径已存在（应作替换）: " + logical)
            ekey, decoded_size, encoded_size = append(replacement.read_bytes(), especs[espec_index].decode())
            encoded_adds[logical] = (ekey, encoded_size, espec_index, decoded_size)
        vfs_content = add_paths(vfs_content, encoded_adds)
    new_vfs_ekey, new_vfs_decoded, new_vfs_encoded = append(bytes(vfs_content))
    # 游戏只从 .build.config 取 vfs-root（DM_FSLOG 实测），vfs-root 的 CFT 再按 EKey 引用 vfs-1
    # ⇒ vfs-root 里指向 vfs-1 的条目也要换成新键，vfs-root 本身写进新档；vfs-2 与 vfs-root 同键，一起改
    # The game takes only vfs-root from .build.config (measured with DM_FSLOG), and the CFT of vfs-root in turn references vfs-1 by EKey
    # ⇒ the entry in vfs-root that points to vfs-1 must also be changed to the new key, and vfs-root itself is written into the new archive; vfs-2 has the same key as vfs-root and is changed along with it
    root_ckey, root_ekey = values["vfs-root"].split()
    root_decoded_size, root_encoded_size = map(int, values["vfs-root-size"].split())
    root = bytearray(read_data_file(data_dir, bytes.fromhex(root_ekey), root_encoded_size))
    if len(root) != root_decoded_size or root[:4] != b"TVFS":
        die("vfs-root is not a valid TVFS manifest")
    old_vfs = bytes.fromhex(vfs_ekey)
    hits = [i for i in range(len(root) - 15) if root[i:i + 16] == old_vfs]
    if len(hits) != 1:
        die(f"vfs-root 里 vfs-1 键命中 {len(hits)} 次（期望 1）")
    pos = hits[0]
    root[pos:pos + 16] = new_vfs_ekey
    set_cft_sizes(root, pos, new_vfs_encoded, new_vfs_decoded)
    new_root_ekey, new_root_decoded, new_root_encoded = append(bytes(root))
    data_name = f"{new_chunk:02d}-{new_archive:08d}.data"
    (output / "data" / data_name).write_bytes(new_data)
    new_config = replace_config(lines, "vfs-1", hashlib.md5(bytes(vfs_content)).hexdigest() + " " + new_vfs_ekey.hex())
    new_config = replace_config(new_config, "vfs-1-size", f"{new_vfs_decoded} {new_vfs_encoded}")
    root_value = hashlib.md5(bytes(root)).hexdigest() + " " + new_root_ekey.hex()
    for key in ("vfs-root", "vfs-2"):
        if values.get(key) == values["vfs-root"]:
            new_config = replace_config(new_config, key, root_value)
            new_config = replace_config(new_config, key + "-size", f"{new_root_decoded} {new_root_encoded}")
    (output / "data" / ".build.config").write_text("".join(new_config), encoding="utf-8")
    if link_source:
        for entry in data_dir.iterdir():
            if entry.name not in {".build.config", data_name}:
                (output / "data" / entry.name).symlink_to(entry.resolve())
    print(f"overlay: {len(updates)} replacement(s), {len(adds)} addition(s), {len(prebuilt_pos)} prebuilt, TVFS rewritten, data={data_name}")


def make_overlay(source, output, replacements, prebuilt=None, link_source=True):
    if output.exists():
        die(f"output already exists: {output}")
    # App 的薄覆盖层由宿主回落到原资源，无需创建原档符号链接（Windows 可能无此权限）。
    # The App's thin overlay falls back to base resources at runtime and needs no symlink privileges.
    if link_source: link_tree(source, output, {"data"})
    else: output.mkdir(parents=True)
    (output / "data").mkdir()
    if not replacements and not prebuilt:
        if link_source:
            for entry in (source / "data").iterdir():
                (output / "data" / entry.name).symlink_to(entry.resolve())
        else:
            (output / "data" / ".build.config").write_bytes((source / "data" / ".build.config").read_bytes())
        print("empty replacement table: linked base" if link_source else "empty replacement table: thin base config")
        return
    rewrite_tvfs(source, output, replacements, prebuilt, link_source)


def main(argv):
    if len(argv) == 6 and argv[1] == "pack":   # pack <RomFS> <输出目录> <替换表.tsv> <块号-档号> | pack <RomFS> <output dir> <replacement-table.tsv> <chunk number-archive number>
        chunk, archive = (int(x) for x in argv[5].split("-"))
        write_pack(Path(argv[2]).resolve(), Path(argv[3]).resolve(), read_replacements(argv[4]), (chunk, archive))
        return
    link_source = "--thin" not in argv
    argv = [a for a in argv if a != "--thin"]
    prebuilt = None
    if len(argv) == 6 and argv[4] == "--prebuilt":
        prebuilt = read_prebuilt(argv[5]); argv = argv[:4]
    if len(argv) != 4:
        die("usage: casc_overlay.py <RomFS> <output-dir> <replacements.tsv> [--prebuilt pack_index.tsv] [--thin] | pack <RomFS> <out> <replacements.tsv> <chunk-archive>")
    make_overlay(Path(argv[1]).resolve(), Path(argv[2]).resolve(), read_replacements(argv[3]), prebuilt, link_source)


if __name__ == "__main__":
    main(sys.argv)
