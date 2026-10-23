#!/usr/bin/env python3
# 储藏箱加宽用的贴图（构建时从玩家的游戏文件生成到 Gen/assets/NativeHUD/widestash，不进仓库；wide_stash.py 只拷）。用法：gen_widestash_art.py <原版 RomFS>
# 全部做成原包同款 SpA1 v61 = DXT5 + 块线性。不用原始 RGBA（v31）：1640×881 的 v31 游戏读出来有成排花点、切两张后大片花屏
# （本机宿主实测；选角页 401×1080 同类问题），而且显存是 DXT5 的 4 倍。
# ① 宽外框 MainPanelBG_Wide：取原包外框（两帧横排，第 1 帧 = 双面板）第 1 帧，在左半标题框中间（帧内 x 348，4 像素对齐）
#    把 152..348 这 196 像素的压缩块再拼一份 = 左半宽 196 像素（392 布局单位）。只搬原图压缩块，不重新编码。
# ② 储藏箱底图（个人 / 共享 / 通用）：解原图 → 格子区插 4 列、下段两侧花饰之间那整块右移 2 列居中、空出的两段补石纹 → 重新编码；
#    宝石 / 材料 / 符文三页：原图两侧本就透明，左右各补 98 像素透明。编码按块取主轴两端为端点：原图本来就是 DXT5，块对齐处基本还原。
# Textures for stash widening (generated at build time from the player's game files into Gen/assets/NativeHUD/widestash, not committed to the repo; wide_stash.py only copies). Usage: gen_widestash_art.py <original game RomFS>
# All made in the original package's SpA1 v61 format = DXT5 + block-linear. Raw RGBA (v31) is not used: a 1640×881 v31 is read by the game with rows of garbage dots, and with large areas of garbage after splitting into two images
# (measured on the local host; same kind of problem as the 401×1080 character select page), and it takes 4 times the video memory of DXT5.
# ① Wide outer frame MainPanelBG_Wide: take frame 1 of the original package's outer frame (two frames side by side, frame 1 = dual panel), and in the middle of the left half's title box (x 348 within the frame, 4-pixel aligned)
#    splice in one more copy of the compressed blocks of the 196 pixels 152..348 = left half 196 pixels wider (392 layout units). Only the original compressed blocks are moved, no re-encoding.
# ② Stash base images (personal / shared / generic): decode the original → insert 4 columns in the grid area, shift the whole block between the two side ornaments in the lower section right 2 columns to center it, fill the two vacated segments with stone texture → re-encode;
#    the gem / material / rune pages: the original images are already transparent on both sides, so 98 transparent pixels are added on each side. Encoding takes the two ends of each block's principal axis as endpoints: the originals are already DXT5, so block-aligned areas are essentially restored.
import ctypes as C, io, os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); DM = os.path.join(HERE, '..', '..'); ART = __import__('gen_paths').gen_dir('NativeHUD', 'widestash')
sys.path.insert(0, HERE); import touch_layout
X0, EX = 348, 196   # 宽外框插入点与插入宽度（像素，都是 4 的倍数） | Wide outer frame insertion point and insertion width (pixels, both multiples of 4)
BG = {'StashPanelBG': 'stash/v2/stashpanelbg', 'StashPanelBGPersonal': 'stash/v2/stashpanelbgpersonal', 'StashPanelBGShared': 'stash/v2/stashpanelbgshared'}
ADV = {'AdvancedStash_Gems_Console': 'stash/v2/additionalstash/advancedstash_gems_console',
       'AdvancedStash_Consumables_Console': 'stash/v2/additionalstash/advancedstash_consumables_console',
       'AdvancedStash_Runes_Console': 'stash/v2/additionalstash/advancedstash_runes_console'}
# 底图加宽参数（像素，按原图列亮度 / 目测量）：cut = 格子区插列处；yb = 格子区下沿（下段起点）；mid = 两侧花饰之间那整块（金币框，共享页另有翻页底板）的左右边
# Base image widening parameters (pixels, measured from the original image's column brightness / by eye): cut = column insertion point in the grid area; yb = lower edge of the grid area (start of the lower section); mid = left and right edges of the whole block between the two side ornaments (gold box; the shared page also has a page-turn base plate)
SPEC = {'StashPanelBG': dict(cut=133, yb=525, mid=(214, 438)), 'StashPanelBGPersonal': dict(cut=129, yb=515, mid=(216, 428)), 'StashPanelBGShared': dict(cut=129, yb=515, mid=(216, 428))}

class Desc(C.Structure): _fields_ = [('target', C.c_int), ('format', C.c_int), ('flags', C.c_int), ('samples', C.c_int), ('width', C.c_uint32), ('height', C.c_uint32),
                                     ('depth', C.c_uint32), ('levels', C.c_uint32), ('stride', C.c_uint32), ('pkg_block_h', C.c_int)]
class Layout(C.Structure): _fields_ = [('size', C.c_uint64), ('layer_size', C.c_uint64), ('align', C.c_uint32), ('layers', C.c_uint32), ('bpb', C.c_uint32), ('tileH', C.c_uint), ('tileD', C.c_uint)]

def lib():
    # 同 romfs_art：允许用本仓源码预编的库，Windows 不必另装系统 clang。
    # As in romfs_art: allow a library built from this tree, without system clang on Windows.
    p = os.environ.get('DM_DESWIZZLE_LIB')
    win = ['-Wl,/EXPORT:nvn_tex_layout', '-Wl,/EXPORT:nvn_deswizzle', '-Wl,/EXPORT:nvn_swizzle'] if os.name == 'nt' else ['-fPIC']   # 同 romfs_art.py | same as romfs_art.py
    if not p:
        t = tempfile.mkdtemp(); p = os.path.join(t, 'bl.dll' if os.name == 'nt' else 'bl.dylib')
        subprocess.run(['clang', '-O2', '-shared', *win, '-I' + os.path.join(DM, 'gfx', 'nvn'), '-I' + os.path.join(DM, 'gfx', 'tex'), '-o', p,
                        os.path.join(DM, 'gfx', 'tex', 'nvn_deswizzle.c'), os.path.join(DM, 'gfx', 'nvn', 'nvn_tex.c')], check=True)
    L = C.CDLL(p); L.nvn_tex_layout.argtypes = [C.POINTER(Desc), C.POINTER(Layout)]
    L.nvn_deswizzle.argtypes = [C.POINTER(Desc), C.c_uint, C.c_uint, C.c_void_p, C.c_void_p, C.c_size_t]
    L.nvn_swizzle.argtypes = [C.POINTER(Desc), C.c_uint, C.c_uint, C.c_void_p, C.c_size_t, C.c_void_p]
    return L

def orig(romfs, rel):
    for line in open(os.path.join(romfs, 'data', 'build_manifest.psv'), encoding='utf-8'):
        f = line.rstrip('\n').split('|')
        if len(f) >= 11 and f[9].lower().endswith('/ui/controller/panel/' + rel + '.sprite'): return touch_layout._read_entry(romfs, f)
    raise SystemExit('gen_widestash_art：包里没有 ' + rel)

def desc(b, w, h): return Desc(1, 0x44, 0, 0, w, h, 1, 1, 0, b[23] + 1)   # 0x44 = DXT5；块高参数取原图头第 23 字节（同解码处） | 0x44 = DXT5; the block height parameter is taken from byte 23 of the original image's header (same as at the decode site)

def pack(L, hdr0, lin, w, h, fw, frames=1):
    """线性 DXT5 块 → 块线性，套原图头（改显示宽、宽、帧数、数据字节数）。 | Linear DXT5 blocks → block-linear, with the original image's header attached (display width, width, frame count, data byte count changed)."""
    d = desc(hdr0, w, h); lay = Layout(); L.nvn_tex_layout(C.byref(d), C.byref(lay)); dst = C.create_string_buffer(lay.size); src = C.create_string_buffer(lin, len(lin))
    assert L.nvn_swizzle(C.byref(d), 0, 0, src, (w + 3) // 4 * 16, dst) == 0
    hdr = bytearray(hdr0[:40]); struct.pack_into('<HI', hdr, 6, fw, w); struct.pack_into('<I', hdr, 12, h); struct.pack_into('<H', hdr, 20, frames); struct.pack_into('<I', hdr, 0x20, lay.size)
    return bytes(hdr) + dst.raw

def bc3(img):
    """RGBA → 线性 DXT5 块（行优先）。颜色：块内像素投到主轴上取两端；透明度：最大 / 最小 8 级。 | RGBA → linear DXT5 blocks (row-major). Color: pixels in the block are projected onto the principal axis and the two ends taken; alpha: max / min, 8 levels."""
    a = np.asarray(img.convert('RGBA'), dtype=np.float64); h, w = a.shape[:2]; H, W = (h + 3) // 4 * 4, (w + 3) // 4 * 4
    p = np.zeros((H, W, 4)); p[:h, :w] = a; p[h:, :] = p[h - 1:h, :] if h < H else 0; p[:, w:] = p[:, w - 1:w] if w < W else 0
    blk = p.reshape(H // 4, 4, W // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4); rgb, al = blk[:, :, :3], blk[:, :, 3]
    mu = rgb.mean(1, keepdims=True); c = rgb - mu; cov = np.einsum('npi,npj->nij', c, c); v = np.ones((len(blk), 3))
    for _ in range(8): v = np.einsum('nij,nj->ni', cov, v); v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-9)
    t = np.einsum('npi,ni->np', c, v); e0 = rgb[np.arange(len(blk)), t.argmax(1)]; e1 = rgb[np.arange(len(blk)), t.argmin(1)]
    def q565(e): r, g, b_ = (np.round(e[:, 0] * 31 / 255).astype(int), np.round(e[:, 1] * 63 / 255).astype(int), np.round(e[:, 2] * 31 / 255).astype(int)); return r << 11 | g << 5 | b_
    def un565(c_): return np.stack([(c_ >> 11 & 31) * 255 / 31, (c_ >> 5 & 63) * 255 / 63, (c_ & 31) * 255 / 31], 1)
    c0, c1 = q565(e0), q565(e1); dm = c0 < c1; c0, c1 = np.where(dm, c1, c0), np.where(dm, c0, c1)
    p0, p1 = un565(c0), un565(c1); pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)
    ci = ((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1).argmin(2); ci[c0 == c1] = 0
    a0, a1 = al.max(1).round().astype(int), al.min(1).round().astype(int)
    ap = np.stack([a0, a1] + [((7 - k) * a0 + k * a1) / 7 for k in range(1, 7)], 1); ai = np.abs(al[:, :, None] - ap[:, None, :]).argmin(2); ai[a0 == a1] = 0
    out = bytearray()
    for n in range(len(blk)):
        ab = sum(int(ai[n, k]) << (3 * k) for k in range(16)); cb = sum(int(ci[n, k]) << (2 * k) for k in range(16))
        out += struct.pack('<BB', a0[n], a1[n]) + ab.to_bytes(6, 'little') + struct.pack('<HHI', c0[n], c1[n], cb)
    return bytes(out)

def decode(L, b):
    """原包 v61 精灵图 → RGBA（解块线性后套 DDS 头交给 PIL 解 DXT5）。 | Original package v61 sprite → RGBA (after un-block-linearizing, a DDS header is attached and PIL decodes the DXT5)."""
    w, h = struct.unpack_from('<II', b, 8); d = desc(b, w, h); pitch = (w + 3) // 4 * 16; lin = C.create_string_buffer(pitch * ((h + 3) // 4)); src = C.create_string_buffer(b[40:], len(b) - 40)
    assert L.nvn_deswizzle(C.byref(d), 0, 0, src, lin, pitch) == 0
    hdr = [124, 0x81007, h, w, len(lin), 0, 1] + [0] * 11 + [32, 4, struct.unpack('<I', b'DXT5')[0], 0, 0, 0, 0, 0] + [0x1000, 0, 0, 0, 0]
    return Image.open(io.BytesIO(b'DDS ' + struct.pack('<31I', *hdr) + lin.raw)).convert('RGBA')

def widen_bg(im, stone_src, cut, yb, mid):
    w, h = im.size; out = Image.new('RGBA', (w + EX, h)); L_, R_ = mid; H = EX // 2
    top = im.crop((0, 0, w, yb)); out.paste(top.crop((0, 0, cut + EX, yb)), (0, 0)); out.paste(top.crop((cut, 0, w, yb)), (cut + EX, 0))   # 格子区：插进 4 列（复制左边 4 列格子） | Grid area: insert 4 columns (duplicating the left 4 columns of cells)
    B = im.crop((0, yb, w, h)); bh = h - yb
    out.paste(B.crop((0, 0, L_, bh)), (0, yb)); out.paste(B.crop((R_, 0, w, bh)), (R_ + EX, yb)); out.paste(B.crop((L_, 0, R_, bh)), (L_ + H, yb))   # 左花饰不动、右花饰右移 4 列、中间整块右移 2 列 | Left ornament unchanged, right ornament shifted right 4 columns, the whole middle block shifted right 2 columns
    col = Image.new('RGBA', (H, bh)); y = 0; k = 0
    while y < bh: p = stone_src if k % 2 == 0 else stone_src.transpose(Image.FLIP_TOP_BOTTOM); col.paste(p, (0, y)); y += p.height; k += 1   # 上下交替翻转铺，接缝处镜像连续 | Tiled with alternating vertical flips, mirror-continuous at the seams
    out.paste(col, (L_, yb)); out.paste(col, (R_ + H, yb)); return out

def main(romfs):
    L = lib()
    b = orig(romfs, 'mainpanelbg'); ver, fw, w, h = struct.unpack_from('<HHII', b, 4)
    assert b[:4] == b'SpA1' and ver == 61 and (fw, w, h, struct.unpack_from('<H', b, 20)[0]) == (1440, 2888, 881, 2)
    d = desc(b, w, h); bw, bh = w // 4, (h + 3) // 4; lin = C.create_string_buffer(bw * 16 * bh); src = C.create_string_buffer(b[40:], len(b) - 40)
    assert L.nvn_deswizzle(C.byref(d), 0, 0, src, lin, bw * 16) == 0
    f1 = w // 2 // 4; rows = []
    for y in range(bh):
        fr = lin.raw[y * bw * 16:(y + 1) * bw * 16][f1 * 16:]
        rows.append(fr[:X0 // 4 * 16] + fr[(X0 - EX) // 4 * 16:X0 // 4 * 16] + fr[X0 // 4 * 16:])
    W = w // 2 + EX; o = pack(L, b, b''.join(rows), W, h, fw + EX)   # 显示宽 = 原帧显示宽 + 插入宽 | Display width = original frame display width + inserted width
    open(os.path.join(ART, 'MainPanelBG_Wide.sprite'), 'wb').write(o); print('MainPanelBG_Wide：%d×%d，%d 字节' % (W, h, len(o)))
    imgs = {n: decode(L, orig(romfs, rel)) for n, rel in {**BG, **ADV}.items()}
    stone = imgs['StashPanelBGPersonal'].crop((248, 515 + 18, 248 + EX // 2, 515 + 62))   # 补空用的石纹：个人页金币框正上方第 18–62 行（避开格子区下沿暗线与金币框） | Stone texture used for filling gaps: rows 18–62 directly above the personal page's gold box (avoiding the dark line at the grid area's lower edge and the gold box)
    for n, rel in {**BG, **ADV}.items():
        b = orig(romfs, rel); assert struct.unpack_from('<HHII', b, 4)[:3] == (61, 652, 652), n   # 原图都是 652 宽 | The original images are all 652 wide
        if n in BG: im = widen_bg(imgs[n], stone, **SPEC[n])
        elif n == 'AdvancedStash_Gems_Console':   # 宝石页这张顶替成珍宝匣底图（宝石 / 符文 / 材料三页合一，gen_trove_art.py）：内容 732 宽，两侧各留原版那 58 像素透明边 | the gems page image is replaced by the Trove background (gems / runes / materials merged into one page, gen_trove_art.py): content 732 wide, keeping the original 58 px transparent margin on each side
            import gen_trove_art as T
            im = Image.new('RGBA', (652 + EX, imgs[n].height)); im.paste(T.build(imgs['AdvancedStash_Gems_Console'], imgs['AdvancedStash_Runes_Console'], imgs['AdvancedStash_Consumables_Console'], decode(L, orig(romfs, 'charactersheet/v2/charactersheet_bg'))), (T.X0, 0))
        else: im = Image.new('RGBA', (652 + EX, imgs[n].height)); im.paste(imgs[n], (EX // 2, 0))
        ww, hh = im.size; o = pack(L, b, bc3(im), ww, hh, ww); open(os.path.join(ART, n + '_wide.sprite'), 'wb').write(o); print('%s：%d×%d，%d 字节' % (n, ww, hh, len(o)))

if __name__ == '__main__': main(sys.argv[1])
