"""底部 UI v4 的几何与素材（效果图脚本 make_bottom_ui_v4.py、六格版 make_bottom_ui_v4_6.py 搬进仓库：只留 gen_hud_v4.py 用到的几何量、石条 / 格子 / 雕像与字效，
去掉效果图的演示底图、抠球液、存图与自检表）。几何全部由常量算出，与效果图同一套数。
素材来源（构建时从玩家自己的游戏文件读，仓库里不放原版的图）：
  ② PC 版 D2R 的解包目录（环境变量 DM_PC_DATA，缺省 $DM_ROOT/data/assets-src/raw，下面是 hd/global/ui/...）：原版 2 倍图（v31 原始 RGBA），出的图与现行逐字节一致；
  ① 找不到 PC 版文件：用 Switch 版 RomFS 里的同名精灵图（半分辨率）放大到 PC 尺寸，会略糊。
字体两种都从 RomFS 取（hd/ui/fonts，与 PC 版逐字节相同）。
Geometry and assets of bottom UI v4 (the mockup script make_bottom_ui_v4.py and the six-slot version make_bottom_ui_v4_6.py moved into the repo: keeping only the geometry values, stone strip / slots / statues and text effects that gen_hud_v4.py uses,
dropping the mockup's demo background, orb liquid cutout, image saving and self-check table). Geometry is computed entirely from constants, the same set of numbers as the mockup.
Asset sources (read at build time from the player's own game files; the repo holds no images from the original game):
  ② Unpacked directory of PC D2R (environment variable DM_PC_DATA, default $DM_ROOT/data/assets-src/raw, with hd/global/ui/... beneath it): the original 2× images (v31 raw RGBA); the resulting images are byte-identical to the current ones;
  ① PC files not found: the same-named sprites in the Switch RomFS (half resolution) are upscaled to PC size, slightly blurry.
In both cases the fonts are taken from the RomFS (hd/ui/fonts, byte-identical to the PC version)."""
import os, struct
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np
import romfs_art
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)

# 素材：名字 → (包内路径, PC 版尺寸)
# Assets: name → (path in package, PC size)
ART = {'front_panel': ('hd/global/ui/PANEL/HUD_02/front_panel.sprite', (2822, 415)),
       'levelupbutton': ('hd/global/ui/Controller/HUD/LevelUpButton.sprite', (248, 121)),
       'greater_healing_potion': ('hd/global/ui/items/misc/potion/greater_healing_potion.sprite', (98, 98)),
       'healing_potion': ('hd/global/ui/items/misc/potion/healing_potion.sprite', (98, 98)),
       'greater_mana_potion': ('hd/global/ui/items/misc/potion/greater_mana_potion.sprite', (98, 98)),
       'rejuv_potion': ('hd/global/ui/items/misc/potion/rejuv_potion.sprite', (98, 98)),
       'perfect_skull': ('hd/global/ui/items/misc/gem/perfect_skull.sprite', (98, 98))}


class Source:
    """取原版素材：有 PC 版文件用 PC 版 2 倍图，没有就用 Switch 版放大。 | Fetch the original game's assets: with PC files use the PC 2× images, otherwise upscale the Switch version."""
    def __init__(self, romfs, pc=None):
        self.romfs = romfs
        pc = pc or os.environ.get('DM_PC_DATA') or os.path.join(DM_ROOT, 'data/assets-src/raw')
        self.pc = pc if all(os.path.isfile(os.path.join(pc, p)) for p, _ in ART.values()) else None
        if not self.pc and os.environ.get('DM_REQUIRE_PC_ART') == '1':   # 我们自己出包的机器在本机配置里设这一项：底栏不许悄悄变糊 | The machines we build our own packages on set this item in the local config: the bottom bar must not silently get blurry
            raise SystemExit('🔴 DM_REQUIRE_PC_ART=1 但没找到 PC 版 D2R 文件（DM_PC_DATA=%s，要有 hd/global/ui/... 下的原始 .sprite）：正式包的底栏会变糊，不出' % pc)
        if not self.pc: print('没找到 PC 版文件（DM_PC_DATA=%s），底栏用 Switch 素材放大，会略糊' % pc)
        else: print('底栏素材：PC 版 2 倍原图（%s）' % self.pc)

    def img(self, name):
        path, size = ART[name]
        if self.pc:
            b = open(os.path.join(self.pc, path), 'rb').read(); ver, fw, w, h = struct.unpack_from('<HHII', b, 4)
            assert b[:4] == b'SpA1' and ver == 31 and (w, h) == size, (path, ver, w, h)
            return Image.frombytes('RGBA', (w, h), b[40:40 + w * h * 4])
        return romfs_art.sprite(self.romfs, path).resize(size, Image.LANCZOS)

    def font(self, name, size): return ImageFont.truetype(romfs_art.font(self.romfs, name), size)


def load(src, VAR='C'):
    """算出 v4 的几何与素材，返回一个命名空间字典（gen_hud_v4.py 用其中的 bar / slot / slots / L / R / 球心 / 经验条 / 文字基线 / fit / metal …）。 | Computes v4's geometry and assets, returns a namespace dict (gen_hud_v4.py uses its bar / slot / slots / L / R / orb centers / experience bar / text baseline / fit / metal ...)."""
    fp = src.img('front_panel'); lvl = src.img('levelupbutton').crop((0, 0, 124, 121))
    W, H = 2868, 1320
    # ---- 规格（px；1pt = 3px）----
    # ---- Spec (px; 1pt = 3px) ----
    K = 156 / 348                    # front_panel 设计单位 → px（球径 348 单位 = 52pt） | front_panel design units → px (orb diameter 348 units = 52pt)
    P, DM = 120, 114                 # 格距 40pt、格宽 38pt（格间 6px） | Slot pitch 40pt, slot width 38pt (6px between slots)
    TILE = (1673, 277, 1792, 391)    # 原版格图只取金框本体（外沿实测 x3–121、y15–128） | From the original game's slot image take only the gold frame itself (outer edge measured x3–121, y15–128)
    SH = round(DM * (TILE[3] - TILE[1]) / (TILE[2] - TILE[0]))
    B = 12; EXT = 60; BH = 156       # 条边 4pt、两端伸进球下 20pt、条高 = 球径 | Strip border 4pt, both ends extend 20pt under the orbs, strip height = orb diameter
    XC = 1390                        # 两球中线 | Centerline between the two orbs
    N = 6; SLOTS_W = N * DM + (N - 1) * (P - DM)   # 六格（回城挪到右侧技能钮上方的圆钮，底栏去掉回城格；原七格 834） | Six slots (town portal moved to the round button above the right-side skill buttons, the town portal slot removed from the bottom bar; originally seven slots, 834)
    SX0 = XC - SLOTS_W // 2; SY0 = H - BH + (BH - SH) // 2
    BAR_X0 = SX0 - B; BAR_X1 = SX0 + SLOTS_W + B
    SAFE = 0.09                      # 图标安全边距（内框每边 9%） | Icon safe margin (9% of the inner frame on each side)
    BADGE = 26                       # 角标字号（瓶数 / 张数 / Lv） | Badge font size (bottle count / scroll count / Lv)
    out = Image.new('RGBA', (W, H))  # 效果图的演示底图不要：几何只用到文字度量与雕像透明度，不读底图像素 | The mockup's demo background is dropped: geometry only uses text metrics and statue alpha, and reads no background pixels
    # ---- 石条 + 七格（同一格图逐像素复制）----
    # ---- Stone strip + seven slots (the same slot image copied pixel for pixel) ----
    STONE = (690, 330, 1170, 388); EDGE = (690, 276, 1170, 288)   # front_panel 左侧长凹槽：暗石内壁（二维纹理）+ 灰石边带；平铺不拉伸，不含任何石饰 | front_panel's long left groove: dark stone inner wall (2D texture) + gray stone edge band; tiled without stretching, containing no stone ornaments
    BW_ = BAR_X1 - BAR_X0 + 2 * EXT; bar = Image.new('RGBA', (BW_, BH))
    tile = fp.crop(STONE); edge = fp.crop(EDGE)
    for yy in range(0, BH, tile.height):
        for xx in range(0, BW_, tile.width): bar.alpha_composite(tile if (xx // tile.width) % 2 == 0 else tile.transpose(Image.FLIP_LEFT_RIGHT), (xx, yy))
    for xx in range(0, BW_, edge.width):
        bar.alpha_composite(edge, (xx, 0)); bar.alpha_composite(edge.transpose(Image.FLIP_TOP_BOTTOM), (xx, BH - edge.height))
    ImageDraw.Draw(bar).rectangle((0, 0, bar.width - 1, bar.height - 1), outline=(40, 33, 24, 255), width=3)
    slot = fp.crop(TILE).resize((DM, SH), Image.LANCZOS)
    ta = np.array(slot).astype(int); gold = (ta[..., 0] > 90) & (ta[..., 0] - ta[..., 2] > 40)
    def inner(line):             # 从两端向里：先遇金、再出金的位置 = 金框内沿 | From both ends inward: the position where gold is first met and then left = inner edge of the gold frame
        def scan(seq):
            i = 0
            while i < len(seq) and not seq[i]: i += 1
            while i < len(seq) and seq[i]: i += 1
            while i + 3 < len(seq) and any(seq[i:i + 3]): i += 1   # 金框有斑驳暗点：连续 3 像素非金才算出框 | The gold frame has mottled dark spots: only 3 consecutive non-gold pixels count as leaving the frame
            return i
        return scan(line), len(line) - 1 - scan(line[::-1])
    INNER = (2, 107, 7, 101)          # 金框内沿（PC 原图量出）：Switch 半分辨率放大后金色阈值找不准，兜底版直接用这组；PC 版照量并核对 | Inner edge of the gold frame (measured from the PC original image): after upscaling the Switch half-resolution image the gold threshold cannot locate it accurately, so the fallback version uses this set directly; the PC version still measures and cross-checks
    if src.pc:
        IX0, IX1 = inner(list(gold[SH // 2])); IY0, IY1 = inner(list(gold[:, DM // 2]))
        assert (IX0, IX1, IY0, IY1) == INNER, ('金框内沿和记录的不一致，改 INNER', IX0, IX1, IY0, IY1)
    else: IX0, IX1, IY0, IY1 = INNER
    IW, IH = IX1 - IX0 + 1, IY1 - IY0 + 1; BOX = round(min(IW, IH) * (1 - 2 * SAFE))
    slots = [(SX0 + i * P, SY0, SX0 + i * P + DM, SY0 + SH) for i in range(N)]
    # ---- 图标：按不透明外接框统一缩放进安全区，外接框中心对内框中心 ----
    # ---- Icons: uniformly scaled into the safe area by opaque bounding box, bounding box center aligned to inner frame center ----
    ns = {}
    def prep(im):
        a = np.array(im)[..., 3]; ys, xs = np.where(a > 24); return im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    def fit(im, i, name, text=False):
        # 规则：缩放系数 = min(78/最大边, 基准几何平均边长/本图几何平均边长)；文字类按字高 = 78×85%
        # Rule: scale factor = min(78/longest side, reference geometric-mean side length/this image's geometric-mean side length); text items use glyph height = 78×85%
        im = prep(im); w, h = im.size
        k = BOX * 0.85 / h if text else min(BOX / max(w, h), ns['REF_G'] / ((w * h) ** 0.5))
        im = im.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
        cx = slots[i][0] + IX0 + IW / 2; cy = slots[i][1] + IY0 + IH / 2
        ns['layer'].alpha_composite(im, (round(cx - im.width / 2), round(cy - im.height / 2)))
    pc = src.img
    exo = lambda s: src.font('exocetblizzardot-medium.otf', s)
    num = lambda s: src.font('blizzardglobal-v5_81.ttf', s)   # 角标与 Lv：原版界面字体等高数字 | Badge and Lv: lining figures in the original game's UI font
    def metal(t, size):           # 原版标题字效：暗金竖向渐变 + 细深描边 + 轻斜面（左上高光、右下暗影） | The original game's title text effect: dark gold vertical gradient + thin dark outline + light bevel (upper-left highlight, lower-right shadow)
        f = exo(size); pad = size // 4; W0, H0 = size * len(t) + 2 * pad, size * 2
        m = Image.new('L', (W0, H0)); ImageDraw.Draw(m).text((pad, pad), t, font=f, fill=255)
        a = np.array(m).astype(float) / 255; ys = np.where(a.max(1) > 0)[0]; y0, y1 = ys.min(), ys.max()
        g = np.zeros((H0, W0, 3)); top = np.array([236, 206, 140]); bot = np.array([120, 84, 36])
        for y in range(H0): t_ = min(1, max(0, (y - y0) / max(1, y1 - y0))); g[y, :] = top * (1 - t_) + bot * t_
        hi = np.roll(np.roll(a, -2, 0), -2, 1); sh = np.roll(np.roll(a, 3, 0), 3, 1)
        rgb = g * a[..., None]; rgb += np.clip(a - hi, 0, 1)[..., None] * np.array([255, 240, 200]) * 0.6; rgb *= (1 - 0.45 * np.clip(a - sh, 0, 1))[..., None]
        ol = np.array(m.filter(ImageFilter.MaxFilter(5))).astype(float) / 255
        out_ = np.zeros((H0, W0, 4)); out_[..., :3] = rgb + (1 - a)[..., None] * np.array([28, 18, 8]); out_[..., 3] = np.maximum(a, ol) * 255
        return Image.fromarray(np.clip(out_, 0, 255).astype('uint8'), 'RGBA')
    _r = prep(pc('greater_healing_potion')); _k = BOX / max(_r.size); ns['REF_G'] = ((_r.width * _k) * (_r.height * _k)) ** 0.5
    # ---- 两侧雕像（PC 定稿原图，按 K 缩放），球心 = 定稿布局 HealthBall(260,65)+174 / ManaBall(24,65)+174 ----
    # ---- Statues on both sides (final PC original images, scaled by K), orb center = final layout HealthBall(260,65)+174 / ManaBall(24,65)+174 ----
    L = fp.crop((0, 0, 610, 415)); R = fp.crop((2210, 0, 2822, 415))
    L = L.resize((round(L.width * K), round(L.height * K)), Image.LANCZOS); R = R.resize((round(R.width * K), round(R.height * K)), Image.LANCZOS)
    BR = round(174 * K)              # 球半径 78 | Orb radius 78
    LX = BAR_X0 + 6 - round(608 * K); RX = BAR_X1 - 6 - round(24 * K)
    LBC = (LX + round(434 * K), H - L.height + round(239 * K)); RBC = (RX + round(198 * K), H - R.height + round(239 * K))
    # ---- 经验条：两端对齐七格外沿 ----
    # ---- Experience bar: both ends aligned to the outer edges of the seven slots ----
    EX0, EX1 = slots[0][0], slots[-1][2]; EY = H - BH - 12
    # ---- 生命 / 法力：小一号，紧贴球顶（基线：不压雕像、与经验条留 ≥6）----
    # ---- Life / mana: one size smaller, tight against the orb top (baseline: not overlapping the statue, leaving ≥6 from the experience bar) ----
    d = ImageDraw.Draw(out); fz = src.font('blizzardglobal-v5_81.ttf', 30)
    statue = np.zeros((H, W), bool)
    for im_, x_ in ((L, LX), (R, RX)): statue[H - im_.height:H, x_:x_ + im_.width] |= np.array(im_)[..., 3] > 24
    TXT = [(LBC, '生命: 55 / 55'), (RBC, '法力: 20 / 20')]; GAP = 6
    def tbox(c, t, yb): x0, y0, x1, y1 = d.textbbox((c[0], yb), t, font=fz, anchor='mb', stroke_width=1); return x0, y0, x1 + 2, y1 + 2   # 含投影 | Includes the drop shadow
    def ok_at(yb):
        for c, t in TXT:
            x0, y0, x1, y1 = tbox(c, t, yb)
            if statue[y0:y1, x0:x1].any(): return False
            if x1 > EX0 - GAP and x0 < EX1 + GAP and y1 > EY - GAP: return False
        return True
    YB = LBC[1] - BR - 4
    while not ok_at(YB): YB -= 1
    ns.update(W=W, H=H, DM=DM, SH=SH, EXT=EXT, BH=BH, BAR_X0=BAR_X0, BADGE=BADGE, BOX=BOX, IX0=IX0, IX1=IX1, IY0=IY0, IY1=IY1,
              bar=bar, slot=slot, slots=slots, L=L, R=R, LX=LX, RX=RX, LBC=LBC, RBC=RBC, BR=BR, EX0=EX0, EX1=EX1, EY=EY, YB=YB,
              lvl=lvl, prep=prep, fit=fit, pc=pc, num=num, metal=metal, layer=Image.new('RGBA', (W, H)))
    return ns
