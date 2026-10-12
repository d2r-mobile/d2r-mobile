#!/usr/bin/env python3
# iOS 全屏验收（屏幕级截图）：② 不拉伸、④ 游戏输出分辨率 = 前端给的值；①（铺满无黑边）见 screen_check.py --want full。
# 全屏不留安全区（界面按游戏原样铺满，不为灵动岛 / 圆角往里收）→ ③ 只出参考数据与叠线图，不判红。
# 用法：fullscreen_check.py <页 sel|cls|game> <screen.png> <screen.png.txt> <boot.log> [16:9 负对照屏幕截图.png]
#   ② 不拉伸：(a) 出图横纵缩放差（闭式，三页）；(b) 局内页 HUD 中段对 16:9 负对照截图的横纵缩放比（图像）；均 ≤ 1%
#   ③（参考，不判）：选角页左侧菜单面板左缘对安全区左边距；三页都出叠了安全区框线的图 <screen>_安全区.png
#   ④ 日志「前端给游戏的输出分辨率 主机 WxH」== 「WindowInitialize 交换链 … 首张 WxH」
# iOS full-screen acceptance (screen-level screenshots): ② no stretching, ④ game output resolution = the value given by the front end; for ① (fills the screen with no black bars) see screen_check.py --want full.
# Full screen keeps no safe area (the UI fills the screen exactly as the game draws it, not inset for the Dynamic Island / rounded corners) → ③ only produces reference data and an overlaid-line image, and never fails anything.
# Usage: fullscreen_check.py <page sel|cls|game> <screen.png> <screen.png.txt> <boot.log> [16:9 negative-control screenshot.png]
#   ② No stretching: (a) difference between horizontal and vertical output scale (closed form, three pages); (b) on the in-run page, horizontal/vertical scale ratio of the HUD middle section against the 16:9 negative-control screenshot (image); both ≤ 1%
#   ③ (reference, not judged): left edge of the left menu panel on the character select page versus the safe-area left margin; all three pages produce an image with the safe-area frame lines overlaid, <screen>_安全区.png
#   ④ Log 「前端给游戏的输出分辨率 主机 WxH」(output resolution the front end gives the game, console WxH) == 「WindowInitialize 交换链 … 首张 WxH」(WindowInitialize swapchain ... first image WxH)
import sys, re
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

page, png, meta, log = sys.argv[1:5]; ref = sys.argv[5] if len(sys.argv) > 5 else None   # ref = 16:9 负对照的屏幕截图（同页） | ref = screenshot of the 16:9 negative control (same page)
img = np.asarray(Image.open(png).convert('RGB')).astype(int); H, W = img.shape[:2]
M = {l.split('\t')[0]: l.split('\t')[1:] for l in open(meta).read().splitlines() if '\t' in l}
scale = float(M['scale'][0]); top, left, bottom, right = (float(x) * scale for x in M['safe_pt'])   # 像素 | Pixels
L = open(log, errors='replace').read(); bad = 0
def res(ok, msg):
    global bad; print(('过  ' if ok else '【红】') + msg); bad += not ok

def ed(a): return np.abs(np.diff(a, axis=1))[:-1, :] + np.abs(np.diff(a, axis=0))[:, :-1]
# ② 不拉伸，两道：(a) 闭式：游戏画面 WxH 对出图视口的横纵缩放之差（日志「出图视口 … （游戏画面 WxH）」）；
#    (b) 图像：16:9 负对照屏幕截图（DM_GAME_RES=1600x900，同页同高度）的 HUD 中段作模板，在本截图上搜横 / 纵缩放，横纵比差 ≤ 1%。
#    （HUD 球不用：底部被 HUD 框遮住、液面与动画干扰，真值上也量不出正圆；人为横拉 2% 时本法测得 1.0226）
# ② No stretching, two checks: (a) closed form: the difference between the horizontal and vertical scale of the game picture WxH to the output viewport (log 「出图视口 … （游戏画面 WxH）」(output viewport ... (game picture WxH)));
#    (b) image: use the middle section of the HUD from the 16:9 negative-control screenshot (DM_GAME_RES=1600x900, same page, same height) as a template and search for the horizontal / vertical scale on this screenshot; aspect difference ≤ 1%.
#    (the HUD orbs are not used: the bottom is covered by the HUD frame, the liquid surface and animation interfere, and even on ground truth a perfect circle cannot be measured; with an artificial 2% horizontal stretch this method measured 1.0226)
vp = re.findall(r'出图视口 (\d+),(\d+) (\d+)x(\d+) / 画布 \d+x\d+（游戏画面 (\d+)x(\d+)）', L)
if vp:
    _, _, vw, vh, gw, gh = map(int, vp[-1]); d = (vw / gw) / (vh / gh) - 1
    res(abs(d) <= 0.01, f'② (a) 出图横纵缩放：{vw}/{gw} 对 {vh}/{gh}，横纵比差 {d * 100:+.2f}%（判据 ≤ 1%）')
else: res(False, '② (a) 日志里没有「出图视口」行')
if page == 'game' and ref:
    F = np.asarray(Image.open(png).convert('L')).astype(float); N = np.asarray(Image.open(ref).convert('L')).astype(float); Hn, Wn = N.shape
    tpl = N[int(Hn * 0.87):Hn, Wn // 2 - 420:Wn // 2 + 420]; best = (-1,)
    for sx in np.arange(0.985, 1.0151, 0.0025):
        for sy in np.arange(0.985, 1.0151, 0.0025):
            t = np.asarray(Image.fromarray(tpl).resize((int(round(tpl.shape[1] * sx)), int(round(tpl.shape[0] * sy))), Image.BILINEAR)).astype(float)
            te = ed(t); te = (te - te.mean()) / (te.std() + 1e-9); th, tw = te.shape
            for dy in range(-6, 7, 2):
                for dx in range(-12, 13, 2):
                    yy, xx = H - th - 1 + dy, W // 2 - tw // 2 + dx
                    if yy < 0 or yy + th + 1 > H: continue
                    f = ed(F[yy:yy + th + 1, xx:xx + tw + 1]); f = (f - f.mean()) / (f.std() + 1e-9); c = float((f * te).mean())
                    if c > best[0]: best = (c, sx, sy)
    c, sx, sy = best
    res(c > 0.8 and abs(sx / sy - 1) <= 0.01, f'② (b) HUD 中段对 16:9 负对照：横 {sx:.4f} 纵 {sy:.4f}，横纵比差 {(sx / sy - 1) * 100:+.2f}%（相关 {c:.3f}，步长 0.25%，判据 ≤ 1%）')
elif page == 'game': print('   ② (b) 没给 16:9 负对照截图，跳过图像核对')
# ③
if page == 'sel':
    lum = img.mean(axis=2); sat = img.max(axis=2) - img.min(axis=2)
    band = (lum > 95) & (sat < 40); band[: int(H * 0.1)] = False; band[int(H * 0.9):] = False
    frac = band.mean(axis=0); cols = np.nonzero(frac[: W // 3] > 0.35)[0]
    if cols.size: print(f'   ③（参考）左侧菜单左缘 x={cols[0]} px（{cols[0] / scale:.1f} pt），安全区左边距 {left:.0f} px（{left / scale:.1f} pt）')
    else: print('   ③（参考）没找到左侧菜单面板')
o = Image.open(png).convert('RGB'); d = ImageDraw.Draw(o)
d.rectangle((left, top, W - 1 - right, H - 1 - bottom), outline=(255, 0, 255), width=4); o.save(png.replace('.png', '_安全区.png'))
print(f'   ③ 安全区（像素）上 {top:.0f} 左 {left:.0f} 下 {bottom:.0f} 右 {right:.0f}；叠线图 {png.replace(".png", "_安全区.png")}')
# ④
f = re.search(r'前端给游戏的输出分辨率 主机 (\d+)x(\d+)', L); w = re.search(r'WindowInitialize 交换链 \d+ 张 间隔 \d+ 首张 (\d+)x(\d+)', L)
res(bool(f and w and f.groups() == w.groups()), f'④ 前端给 {f.groups() if f else "?"}，游戏交换链 {w.groups() if w else "?"}')
sys.exit(1 if bad else 0)
