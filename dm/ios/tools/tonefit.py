# 偏暗分类：我方像素 → 真值像素 的映射拟合（读 pages.sh 产物 mac_sel / mac_game；静态区域逐像素配对，已证同位姿：相位相关平移 0,0）。修一轮后重跑看 k 是否回到 1。用法：python3 dm/ios/tools/tonefit.py
# Darkness classification: fit of the mapping our pixels → ground truth pixels (reads pages.sh outputs mac_sel / mac_game; pixel-by-pixel pairing in static regions, same pose already proven: phase-correlation shift 0,0). Rerun after a round of fixes to see whether k returns to 1. Usage: python3 dm/ios/tools/tonefit.py
import numpy as np; from PIL import Image, ImageDraw
import os; DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
P = DM_ROOT + '/run/ios/pages/'; T = DM_ROOT + '/run/verify/'; O = DM_ROOT + '/run/ios/tonefit/'
import os; os.makedirs(O, exist_ok=True)
def lin(e): return np.where(e <= 0.04045, e / 12.92, ((e + 0.055) / 1.055) ** 2.4)
PAGES = {
    '选角': (P + 'mac_sel.png', T + 'truth_shots/选角页_saveref_joe_有DLC.png', (1600, 900),
            [(0, 0, 345, 900), (690, 110, 970, 900), (1055, 350, 1425, 490), (690, 800, 910, 850), (290, 540, 430, 720)]),
    '局内': (P + 'mac_game.png', T + 'dm/pq/p0.png', (640, 360),
            [(0, 295, 640, 360), (40, 40, 75, 120), (465, 90, 500, 160), (510, 250, 545, 305), (290, 110, 350, 200),
             (120, 170, 175, 235), (240, 120, 290, 205), (135, 70, 205, 105), (255, 60, 380, 95), (180, 230, 290, 305), (0, 0, 640, 12)]),
}
def density(x, y, title, path, fitlines):
    N = 400; R = float(max(np.quantile(x, 0.995), np.quantile(y, 0.995))) * 1.05
    img = Image.new('RGB', (N + 70, N + 50), (255, 255, 255)); d = ImageDraw.Draw(img)
    H, _, _ = np.histogram2d(np.clip(x, 0, R), np.clip(y, 0, R), bins=N, range=[[0, R], [0, R]])
    H = np.sqrt(H / H.max()); H = (255 * (1 - np.clip(H * 3, 0, 1))).astype(np.uint8)
    img.paste(Image.fromarray(np.flipud(H.T)).convert('RGB'), (50, 10))
    X = lambda v: 50 + v / R * N; Y = lambda v: 10 + N - v / R * N
    d.rectangle((50, 10, 50 + N, 10 + N), outline=(0, 0, 0)); d.line((X(0), Y(0), X(R), Y(R)), fill=(170, 170, 170))   # y = x
    for f, col in fitlines:
        xs = np.linspace(0, R, 200); ys = np.clip(f(xs), 0, R); d.line([(X(a), Y(b)) for a, b in zip(xs, ys)], fill=col, width=2)
    bins = np.linspace(0, R, 25)
    for i in range(24):
        q = (x >= bins[i]) & (x < bins[i + 1])
        if q.sum() > 30: mx, my = (bins[i] + bins[i + 1]) / 2, float(np.median(y[q])); d.ellipse((X(mx) - 3, Y(my) - 3, X(mx) + 3, Y(my) + 3), fill=(0, 160, 0))
    d.text((50, N + 15), f'{title}  x=truth y=ours  range 0..{R:.3f}  grey: y=x  blue: k*lin  red: pow  green: bin median', fill=(0, 0, 0)); img.save(path)
for name, (po, pt, size, masks) in PAGES.items():
    a = np.asarray(Image.open(po).convert('RGB').resize(size, Image.BILINEAR)).astype(float) / 255
    b = np.asarray(Image.open(pt).convert('RGB').resize(size, Image.BILINEAR)).astype(float) / 255
    m = np.ones(size[::-1], bool)
    for x0, y0, x1, y1 in masks: m[y0:y1, x0:x1] = False
    o, t = a[m].ravel(), b[m].ravel()
    keep = (t > 8 / 255) & (t < 250 / 255) & (o < 250 / 255); o, t = o[keep], t[keep]
    # 稳健：先按线性光倍数粗拟合，剔除残差最大的 15%（残余错位、风吹草动），再在剩余样本上做三种拟合
    # Robustness: first a coarse fit by linear-light multiplier, discard the 15% with the largest residuals (residual misalignment, slight scene movement), then do the three fits on the remaining samples
    ol, tl = lin(o), lin(t); k0 = np.sum(ol * tl) / np.sum(tl * tl); r = np.abs(ol - k0 * tl); s = r < np.quantile(r, 0.85)
    o, t, ol, tl = o[s], t[s], ol[s], tl[s]
    k = np.sum(ol * tl) / np.sum(tl * tl); rk = np.sqrt(np.mean((o - np.clip(((k * tl) ** (1 / 2.4)) * 1.055 - 0.055, 0, 1)) ** 2))   # 残差按编码值算（近似反变换） | Residual computed on encoded values (approximate inverse transform)
    pos = (o > 1 / 255); lt, lo = np.log(t[pos]), np.log(o[pos])
    g = np.sum(lo * lt) / np.sum(lt * lt); rg = np.sqrt(np.mean((o - t ** g) ** 2))
    A = np.vstack([lt, np.ones_like(lt)]).T; (g2, c2), *_ = np.linalg.lstsq(A, lo, rcond=None); rg2 = np.sqrt(np.mean((o[pos] - np.exp(c2) * t[pos] ** g2) ** 2))
    # 分段：线性光空间暗部 / 中部 / 亮部的倍数（真值编码值分段）
    # Segments: multipliers for the dark / mid / bright parts in linear-light space (segmented by ground truth encoded value)
    seg = []
    for lo_, hi_ in ((0.03, 0.15), (0.15, 0.35), (0.35, 0.6), (0.6, 1.0)):
        q = (t >= lo_) & (t < hi_)
        seg.append((lo_, hi_, int(q.sum()), float(np.sum(ol[q] * tl[q]) / np.sum(tl[q] ** 2)) if q.sum() > 50 else float('nan')))
    # 分箱中位曲线（编码值）
    # Binned median curve (encoded values)
    bins = np.linspace(0, 1, 21); mid = [(bins[i], float(np.median(o[(t >= bins[i]) & (t < bins[i + 1])])) if ((t >= bins[i]) & (t < bins[i + 1])).sum() > 30 else None) for i in range(20)]
    print(f'== {name}：配对样本 {o.size}（通道合计，剔除最差 15% 后）')
    print(f'  ① 线性光过原点倍数 k = {k:.3f}（编码值残差 RMS {rk * 255:.1f}/255）')
    print(f'  ② 编码值幂 我方 = 真值^γ：γ = {g:.3f}（残差 RMS {rg * 255:.1f}/255）；带系数 我方 = {np.exp(c2):.3f}·真值^{g2:.3f}（残差 {rg2 * 255:.1f}/255）')
    print('  ③ 线性光分段倍数（按真值编码值段）：' + '；'.join(f'[{a_:.2f},{b_:.2f}) n={n} k={kk:.3f}' for a_, b_, n, kk in seg))
    print('  分箱中位（真值编码 → 我方编码）：' + ' '.join(f'{x:.2f}→{y:.3f}' for x, y in mid if y is not None))
    density(t, o, f'{name} sRGB encoded', O + f'{name}_编码值.png', [(lambda x, g=g: x ** g, (220, 0, 0)), (lambda x, k=k: np.clip(((k * lin(x)) ** (1 / 2.4)) * 1.055 - 0.055, 0, 1), (0, 0, 220))])
    density(tl, ol, f'{name} linear light', O + f'{name}_线性光.png', [(lambda x, k=k: k * x, (0, 0, 220))])
print('图：', O)
