#!/usr/bin/env python3
"""老机型补充包：把游戏贴图里的 BC3 离线转成 ASTC 4x4（A15 / A16 等显卡不支持 BC 格式的机型用；采用离线转换、不在运行时转）。
玩家用自己的游戏文件（Switch 版 RomFS）在 Mac 上生成，仓库里不放任何原版衍生产物。

做法：
- .texture 文件头第 4 字节是游戏自己的格式码，游戏按 main+0x1df0c24 的表映射到 NVN 格式：0x3d → 0x44 BC3、0x3e → 0x48 BC3 sRGB、
  0x5f → 0x79 ASTC 4x4、0x51 → 0x87 ASTC 4x4 sRGB。游戏原生认 ASTC，所以只换格式码，翻译层照常按 NVN 的 ASTC 建贴图。
- BC3 与 ASTC 4x4 都是 4x4 像素一块、每块 16 字节，块线性排布只取决于块网格与块字节数 ⇒ 数据区逐块原地转码，不用解排布。
  数据区 = 文件头 mip 表首项指向处（偏移 40 的 32 位值 + 40）到文件尾；块与块互不依赖，所以把块拼成一张大图交给 astcenc 编，再按序拆回。
- 三类参数：颜色图（0x3e，sRGB）按 sRGB 编；法线图（_NRM，R 恒 255、B 恒 0、G = Y、A = X）与材质图等线性图按四通道线性编。
  法线图不改通道摆放（不做亮度 + 透明度那种重排），R / B 常量在 ASTC 端点里是精确的，着色器怎么取都与原样一致。
- 游戏建贴图时的格式不读 .texture 文件头，而是读 data/hd/texture_desc_cache.json（每张贴图一条：format 61 / 62 = 0x3d / 0x3e，
  另有内嵌的低分辨率缩略图 thumbnailBase64 = 几级小 mip 的 BC3 块，先上传顶着）。所以这份缓存也一起改：转过的贴图 format 改成 95 / 81，
  缩略图逐块转 ASTC（缩略图头：mip 数、表起点相对偏移，然后每级一对 (大小, 相对偏移)，偏移都从偏移字段自己算——
  照游戏 main+0x16c2d5c 的读法；第一级数据在 48 字节处，各级首尾相接到文件尾）；改好的缓存也放进补充包。
- 每张贴图算转码结果与 BC3 解码真值的峰值信噪比（PSNR，四通道合算），写 quality.tsv。
BC1 / BC4 / BC5 / BC6H 不在这里转（量小），由翻译层在不支持 BC 的机型上运行时解压。

完整流程（产物目录 D）：
  1. astc_pack.py <RomFS> D/work                        转码（Mac 全核约 7 分钟）
  2. casc_overlay.py pack <RomFS> D/pack D/work/repl.tsv 0-8   写补充包数据档 00-00000008.data + 条目表 pack_index.tsv
  3. astc_pack.py finalize D/pack                       算大小与 SHA-256：pack.json（随 App 打包用来校验）、
                                                        D/pack/Legacy Device Pack/{manifest.json, data/00-00000008.data}（下载站与「文件」拷贝用的目录，数据档是硬链接）
  4. 出 App 包时 DM_ASTC_PACK=D/pack（gen_romfs_overlay.py 据此出 ASTC 版覆盖层）
用法：astc_pack.py <RomFS> <输出目录> [--astcenc 路径] [--preset thorough] [--limit N] [--only 路径子串]
  产物：<输出目录>/files/<逻辑路径>（转好的 .texture）、repl.tsv（逻辑路径<TAB>文件，给 casc_overlay.py）、quality.tsv、summary.txt
astcenc：ARM 开源编码器（Apache 2.0），从源码编（cmake -DASTCENC_ISA_NEON=ON），路径用 --astcenc 或环境变量 ASTCENC 给。
Legacy Device Pack: converts the BC3 in the game textures to ASTC 4x4 offline (for devices such as A15 / A16 whose GPU does not support BC formats; conversion is done offline, not at runtime).
Players generate it on a Mac from their own game files (the Switch version's RomFS); no output derived from the original game is kept in the repository.
How it works:
- Byte 4 of the .texture file header is the game's own format code, which the game maps to an NVN format through the table at main+0x1df0c24: 0x3d → 0x44 BC3, 0x3e → 0x48 BC3 sRGB,
  0x5f → 0x79 ASTC 4x4, 0x51 → 0x87 ASTC 4x4 sRGB. The game natively recognizes ASTC, so only the format code is swapped and the translation layer creates the texture as NVN ASTC as usual.
- BC3 and ASTC 4x4 are both 4x4 pixels per block and 16 bytes per block, and the linear block layout depends only on the block grid and the bytes per block ⇒ the data area is transcoded in place block by block, with no need to undo the layout.
  Data area = from where the first entry of the header's mip table points (the 32-bit value at offset 40, + 40) to the end of the file; blocks are independent of one another, so the blocks are stitched into one big image, handed to astcenc to encode, then split back in order.
- Three kinds of parameters: color maps (0x3e, sRGB) are encoded as sRGB; normal maps (_NRM, R always 255, B always 0, G = Y, A = X) and linear maps such as material maps are encoded as four-channel linear.
  The channel placement of normal maps is not changed (no luminance + alpha style rearrangement); the R / B constants are exact in the ASTC endpoints, so however the shader samples them the result matches the original.
- When the game creates a texture it does not read the format from the .texture file header but from data/hd/texture_desc_cache.json (one record per texture: format 61 / 62 = 0x3d / 0x3e,
  plus an embedded low-resolution thumbnail thumbnailBase64 = the BC3 blocks of a few small mip levels, uploaded first as a stand-in). So this cache is edited too: the format of converted textures becomes 95 / 81,
  and thumbnails are converted to ASTC block by block (thumbnail header: mip count, relative offset of the table start, then one (size, relative offset) pair per level, every offset counted from the offset field itself --
  following the way the game reads it at main+0x16c2d5c; the first level's data is at byte 48, and the levels follow one another end to end up to the end of the file); the edited cache also goes into the Legacy Device Pack.
- For every texture the peak signal-to-noise ratio (PSNR, all four channels together) between the transcoded result and the BC3 decoded ground truth is computed and written to quality.tsv.
BC1 / BC4 / BC5 / BC6H are not converted here (small volume); the translation layer decompresses them at runtime on devices that do not support BC.
Full flow (build output directory D):
  1. astc_pack.py <RomFS> D/work                        transcode (about 7 minutes on all cores of a Mac)
  2. casc_overlay.py pack <RomFS> D/pack D/work/repl.tsv 0-8   write the Legacy Device Pack data archive 00-00000008.data + entry table pack_index.tsv
  3. astc_pack.py finalize D/pack                       compute sizes and SHA-256: pack.json (bundled with the App for verification),
                                                        D/pack/Legacy Device Pack/{manifest.json, data/00-00000008.data} (the directory used for the download site and for copying via 「文件」(Files); the data archive is a hard link)
  4. When building the App package set DM_ASTC_PACK=D/pack (gen_romfs_overlay.py uses it to produce the ASTC version of the overlay)
Usage: astc_pack.py <RomFS> <output dir> [--astcenc path] [--preset thorough] [--limit N] [--only path substring]
  Output: <output dir>/files/<logical path> (converted .texture), repl.tsv (logical path<TAB>file, for casc_overlay.py), quality.tsv, summary.txt
astcenc: ARM's open-source encoder (Apache 2.0), built from source (cmake -DASTCENC_ISA_NEON=ON); its path is given with --astcenc or the environment variable ASTCENC."""
import argparse, os, re, struct, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'ios', 'tools'))
import touch_layout   # _read_entry：按 build_manifest.psv 一行读出解压后的文件内容 | _read_entry: reads the decompressed file content for one line of build_manifest.psv

CODE_ASTC = {0x3d: 0x5f, 0x3e: 0x51}   # 游戏格式码：BC3 → ASTC 4x4、BC3 sRGB → ASTC 4x4 sRGB | Game format codes: BC3 → ASTC 4x4, BC3 sRGB → ASTC 4x4 sRGB
BATCH_BLOCKS = 1 << 22                  # 每批约 400 万块（6700 万像素，RGBA 256 MiB） | About 4 million blocks per batch (67 million pixels, 256 MiB of RGBA)
ROW_BLOCKS = 1024                       # 拼图每行 1024 块（宽 4096 像素） | The stitched image has 1024 blocks per row (4096 pixels wide)


def bc3_decode(raw):
    """BC3 块 → (块数, 16, 4) uint8，像素按块内行优先。颜色块在 BC3 里一律按四色模式解。 | BC3 blocks → (block count, 16, 4) uint8, pixels in row-major order within each block. In BC3 the color block is always decoded in four-color mode."""
    b = np.frombuffer(raw, np.uint8).reshape(-1, 16)
    n = len(b)
    a0 = b[:, 0].astype(np.int32); a1 = b[:, 1].astype(np.int32)
    abits = np.zeros(n, np.uint64)
    for i in range(6): abits |= b[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    k = np.arange(8, dtype=np.int32)
    p8 = np.where(k == 0, a0[:, None], np.where(k == 1, a1[:, None], ((8 - k) * a0[:, None] + (k - 1) * a1[:, None] + 3) // 7))
    p6 = np.where(k == 0, a0[:, None], np.where(k == 1, a1[:, None], np.where(k == 6, 0, np.where(k == 7, 255, ((6 - k) * a0[:, None] + (k - 1) * a1[:, None] + 2) // 5))))
    apal = np.where((a0 > a1)[:, None], p8, p6)
    aidx = ((abits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)).astype(np.int64)
    alpha = np.take_along_axis(apal, aidx, 1)
    c0 = b[:, 8].astype(np.int32) | (b[:, 9].astype(np.int32) << 8); c1 = b[:, 10].astype(np.int32) | (b[:, 11].astype(np.int32) << 8)
    def rgb(c): r, g, bl = (c >> 11) & 31, (c >> 5) & 63, c & 31; return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (bl << 3) | (bl >> 2)], -1)
    e0, e1 = rgb(c0), rgb(c1)
    cpal = np.stack([e0, e1, (2 * e0 + e1 + 1) // 3, (e0 + 2 * e1 + 1) // 3], 1)   # (n, 4, 3)
    cbits = b[:, 12].astype(np.uint32) | (b[:, 13].astype(np.uint32) << 8) | (b[:, 14].astype(np.uint32) << 16) | (b[:, 15].astype(np.uint32) << 24)
    cidx = ((cbits[:, None] >> (np.uint32(2) * np.arange(16, dtype=np.uint32))) & np.uint32(3)).astype(np.int64)
    col = np.take_along_axis(cpal, cidx[:, :, None].repeat(3, 2), 1)
    return np.concatenate([col, alpha[:, :, None]], -1).astype(np.uint8)


def blocks_to_image(px):
    n = len(px); rows = (n + ROW_BLOCKS - 1) // ROW_BLOCKS
    pad = np.zeros((rows * ROW_BLOCKS - n, 16, 4), np.uint8)
    a = np.concatenate([px, pad]).reshape(rows, ROW_BLOCKS, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(rows * 4, ROW_BLOCKS * 4, 4)
    return a


def image_to_blocks(img, n):
    rows = img.shape[0] // 4
    return img.reshape(rows, 4, ROW_BLOCKS, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)[:n]


def encode_batch(enc, preset, srgb, px, tmp):
    """px (n,16,4) → (ASTC 块字节 n*16, 解码回来的像素 (n,16,4)) | px (n,16,4) → (ASTC block bytes n*16, pixels decoded back (n,16,4))"""
    n = len(px); src, out, dec = (os.path.join(tmp, f) for f in ('in.png', 'out.astc', 'dec.png'))
    Image.fromarray(blocks_to_image(px), 'RGBA').save(src, compress_level=1)
    prof = '-cs' if srgb else '-cl'
    subprocess.run([enc, prof, src, out, '4x4', '-' + preset, '-silent'], check=True)
    a = open(out, 'rb').read()
    assert a[:4] == bytes.fromhex('13aba15c') and a[4:7] == bytes((4, 4, 1)), 'astcenc 输出头不对'
    w = int.from_bytes(a[7:10], 'little'); h = int.from_bytes(a[10:13], 'little')
    assert w == ROW_BLOCKS * 4 and h % 4 == 0
    blocks = np.frombuffer(a[16:], np.uint8).reshape(h // 4, ROW_BLOCKS, 16).reshape(-1, 16)[:n].tobytes()
    subprocess.run([enc, '-ds' if srgb else '-dl', out, dec, '-silent'], check=True)
    back = image_to_blocks(np.asarray(Image.open(dec).convert('RGBA')), n)
    return blocks, back


def psnr(a, b):
    mse = float(np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2))
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)


def finalize(packdir):
    import hashlib, json
    idx = open(os.path.join(packdir, 'pack_index.tsv'), encoding='utf-8').readline().rstrip('\n').split('\t')
    assert idx[0] == '# pack', 'pack_index.tsv 首行不对'
    name = idx[1]; src = os.path.join(packdir, name); h = hashlib.sha256()
    with open(src, 'rb') as f:
        for b in iter(lambda: f.read(64 << 20), b''): h.update(b)
    info = {'name': name, 'size': os.path.getsize(src), 'sha256': h.hexdigest()}
    json.dump(info, open(os.path.join(packdir, 'pack.json'), 'w'), ensure_ascii=False, indent=1)
    root = os.path.join(packdir, '老机型补充包'); os.makedirs(os.path.join(root, 'data'), exist_ok=True)
    dst = os.path.join(root, 'data', name)
    if os.path.exists(dst): os.remove(dst)
    os.link(src, dst)
    json.dump({'files': [info]}, open(os.path.join(root, 'manifest.json'), 'w'), ensure_ascii=False, indent=1)
    print(f'astc_pack finalize：{name} {info["size"]} 字节 sha256 {info["sha256"]}')


def desc_cache(o, converted, tmp, repl):
    """改 data/hd/texture_desc_cache.json：转过的贴图 format 61 → 95、62 → 81，缩略图逐块转 ASTC | Edit data/hd/texture_desc_cache.json: for converted textures format 61 → 95 and 62 → 81, thumbnails converted to ASTC block by block"""
    import base64, json
    logical = 'data/hd/texture_desc_cache.json'
    row = next(f for f in (l.rstrip('\n').split('|') for l in open(os.path.join(o.romfs, 'data', 'build_manifest.psv'), encoding='utf-8')) if len(f) >= 11 and f[9].split(':', 1)[-1].lower() == logical)
    J = json.loads(touch_layout._read_entry(o.romfs, row))
    jobs = {False: [], True: []}   # sRGB 分开：[(条目, 缩略图字节, [(块起点, 块数)…], 解码像素)] | Split by sRGB: [(entry, thumbnail bytes, [(block start, block count)...], decoded pixels)]
    nconv = 0
    for e in J:
        p = e['filename'].lower()
        if p not in converted or e['format'] not in (61, 62): continue
        srgb = converted[p]
        if (e['format'] == 62) != srgb: sys.exit(f'astc_pack：{p} 缓存里的格式 {e["format"]} 与文件头不一致')
        e['format'] = 81 if srgb else 95; nconv += 1
        t = base64.b64decode(e['thumbnailBase64']) if e.get('thumbnailBase64') else b''
        if not t: continue
        n = struct.unpack_from('<I', t, 0)[0]; tab = 4 + struct.unpack_from('<i', t, 4)[0]; regs = []; px = []
        for i in range(n):
            size = struct.unpack_from('<I', t, tab + 8 * i)[0]; rf = tab + 8 * i + 4; start = rf + struct.unpack_from('<i', t, rf)[0]
            if size % 16 or start < tab + 8 * n or start + size > len(t): sys.exit(f'astc_pack：{p} 缩略图第 {i} 级（大小 {size}，起点 {start}）不是表后的整块')
            regs.append((start, size // 16)); px.append(bc3_decode(t[start:start + size]))
        jobs[srgb].append((e, t, regs, np.concatenate(px)))
    for srgb, items in jobs.items():
        if not items: continue
        blocks, _ = encode_batch(o.astcenc, o.preset, srgb, np.concatenate([it[3] for it in items]), tmp)
        at = 0
        for e, t, regs, px in items:
            nt = bytearray(t)
            for start, nb in regs: nt[start:start + nb * 16] = blocks[at * 16:(at + nb) * 16]; at += nb
            e['thumbnailBase64'] = base64.b64encode(bytes(nt)).decode()
    dst = os.path.join(o.out, 'files', logical); os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8').write(json.dumps(J, ensure_ascii=False, separators=(',', ':')))
    repl.write(f'{logical}\t{dst}\n')
    print(f'astc_pack：贴图描述缓存改了 {nconv} 条（缩略图 {sum(len(v) for v in jobs.values())} 张转 ASTC）')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'finalize': finalize(sys.argv[2]); return
    ap = argparse.ArgumentParser(); ap.add_argument('romfs'); ap.add_argument('out')
    ap.add_argument('--astcenc', default=os.environ.get('ASTCENC')); ap.add_argument('--preset', default='thorough')
    ap.add_argument('--limit', type=int, default=0); ap.add_argument('--only', default='')
    o = ap.parse_args()
    if not o.astcenc or not os.access(o.astcenc, os.X_OK): sys.exit('astc_pack：找不到 astcenc（--astcenc 或环境变量 ASTCENC）')
    rows = [l.rstrip('\n').split('|') for l in open(os.path.join(o.romfs, 'data', 'build_manifest.psv'), encoding='utf-8')]
    rows = [f for f in rows if len(f) >= 11 and f[9].lower().endswith('.texture') and o.only in f[9].lower()]
    seen = set(); todo = []
    for f in rows:
        p = f[9].split(':', 1)[-1].lower()
        if p not in seen: seen.add(p); todo.append((p, f))
    if o.limit: todo = todo[:o.limit]
    os.makedirs(os.path.join(o.out, 'files'), exist_ok=True)
    t0 = time.time(); stat = {'跳过非 BC3': 0, '转码': 0, '块数': 0}
    pend = {False: [], True: []}   # 按 sRGB 分两批：[(路径, 原字节, 数据起点, 解码像素)] | Two batches split by sRGB: [(path, original bytes, data start, decoded pixels)]
    qual = open(os.path.join(o.out, 'quality.tsv'), 'w'); qual.write('逻辑路径\t类\t宽\t高\t块数\tPSNR\n')
    repl = open(os.path.join(o.out, 'repl.tsv'), 'w')
    tmp = tempfile.mkdtemp(prefix='astc_pack_'); converted = {}

    def flush(srgb):
        items = pend[srgb]
        if not items: return
        px = np.concatenate([it[3] for it in items])
        blocks, back = encode_batch(o.astcenc, o.preset, srgb, px, tmp)
        at = 0
        for p, b, start, dec in items:
            n = len(dec); nb = bytearray(b); nb[4] = CODE_ASTC[b[4]]; nb[start:] = blocks[at * 16:(at + n) * 16]
            dst = os.path.join(o.out, 'files', p); os.makedirs(os.path.dirname(dst), exist_ok=True); open(dst, 'wb').write(nb)
            repl.write(f'{p}\t{dst}\n'); converted[p] = srgb
            w, h = struct.unpack_from('<II', b, 8)
            cls = 'sRGB 颜色' if srgb else ('法线' if p.endswith('_nrm.texture') else '线性')
            qual.write(f'{p}\t{cls}\t{w}\t{h}\t{n}\t{psnr(dec, back[at:at + n]):.2f}\n')
            at += n
        pend[srgb] = []
        print(f'astc_pack：已转 {stat["转码"]} 张、{stat["块数"]} 块，用时 {time.time() - t0:.0f} 秒', flush=True)

    for i, (p, f) in enumerate(todo):
        b = touch_layout._read_entry(o.romfs, f)
        if len(b) < 48 or b[4] not in CODE_ASTC: stat['跳过非 BC3'] += 1; continue
        start = 40 + struct.unpack_from('<I', b, 40)[0]
        if start >= len(b) or (len(b) - start) % 16: sys.exit(f'astc_pack：{p} 数据区不是 16 字节整块（起点 {start}，文件 {len(b)} 字节）')
        dec = bc3_decode(b[start:]); srgb = b[4] == 0x3e
        pend[srgb].append((p, b, start, dec)); stat['转码'] += 1; stat['块数'] += len(dec)
        if sum(len(x[3]) for x in pend[srgb]) >= BATCH_BLOCKS: flush(srgb)
    flush(False); flush(True); qual.close()
    desc_cache(o, converted, tmp, repl)
    repl.close()
    s = f'astc_pack：贴图 {len(todo)} 张，转码 {stat["转码"]} 张（{stat["块数"]} 块），跳过非 BC3 {stat["跳过非 BC3"]} 张；档 {o.preset}；墙钟 {time.time() - t0:.0f} 秒'
    open(os.path.join(o.out, 'summary.txt'), 'w').write(s + '\n'); print(s)


if __name__ == '__main__':
    main()
