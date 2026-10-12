#!/usr/bin/env python3
# 真机屏幕级验收（App 内 DM_SCREEN_SHOT 截整个窗口）：核屏幕上实际显示的样子，补 DM_NVN_SHOT 只截游戏渲染帧看不到的问题
# （真机目测发现：画面转 90°、横向拉伸、黑边、选角页 3D 场景全黑——渲染帧对拍全过却都没测出）。
# 用法：screen_check.py <screen.png> <screen.png.txt> <宿主日志> [--want full|16:9]
#   ① 方向：界面方向为横屏（UIInterfaceOrientation 3/4），且截图宽 > 高；①b 全屏口径下整列纯黑列数 = 0（内容铺满，不只是视口铺满）
#   ② 宽高比：日志最后一行「出图视口」—— full：视口占满画布；16:9：视口 16:9 且居中（保底方案）；画布比例须等于截图比例（层没跟着转会不等）
#   ③ 画面不大面积全黑：视口区域内近黑像素（三通道最大值 < 8）占比 ≤ 85%（阈值本工具自定，目前没有共用的全黑阈值）
# 退出码 0 = 三项全过。
# Real device screen-level acceptance (the in-App DM_SCREEN_SHOT captures the whole window): checks what is actually displayed on screen, covering problems that DM_NVN_SHOT, which captures only the game render frame, cannot see
# (found by eye on a real device: picture rotated 90°, horizontal stretching, black bars, the character select page's 3D scene all black -- render-frame comparison all passed yet caught none of them).
# Usage: screen_check.py <screen.png> <screen.png.txt> <host log> [--want full|16:9]
#   ① Orientation: interface orientation is landscape (UIInterfaceOrientation 3/4), and screenshot width > height; ①b under the full-screen criterion the number of entirely black columns = 0 (content fills the screen, not just the viewport)
#   ② Aspect ratio: the last 「出图视口」(output viewport) line in the log -- full: viewport fills the canvas; 16:9: viewport is 16:9 and centered (fallback scheme); the canvas ratio must equal the screenshot ratio (they differ if the layer did not rotate along)
#   ③ Picture not largely black: the share of near-black pixels (max of the three channels < 8) within the viewport area ≤ 85% (threshold defined by this tool; there is currently no shared all-black threshold)
# Exit code 0 = all three pass.
import argparse, re, sys
import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument('png'); ap.add_argument('meta'); ap.add_argument('log')
ap.add_argument('--want', choices=['full', '16:9'], default='full')
a = ap.parse_args()
BLACK_MAX, BLACK_FRAC = 8, 0.85

img = np.asarray(Image.open(a.png).convert('RGB')).astype(np.int16)
H, W = img.shape[:2]
meta = dict(l.split('\t', 1) for l in open(a.meta).read().splitlines() if '\t' in l)
orient = int(meta['orientation'])
vps = re.findall(r'出图视口 (\d+),(\d+) (\d+)x(\d+) / 画布 (\d+)x(\d+)', open(a.log, errors='replace').read())
bad = 0
def res(ok, msg):
    global bad
    print(('过  ' if ok else '【红】') + msg); bad += not ok

res(orient in (3, 4) and W > H, f'① 方向：界面方向 {orient}（3/4 = 横屏），截图 {W}x{H}')
if not vps:
    res(False, '② 宽高比：日志里没有「出图视口」行（没出过图？）'); vx = vy = 0; vw, vh, cw, ch = W, H, W, H
else:
    vx, vy, vw, vh, cw, ch = map(int, vps[-1])
    same = abs(cw / ch - W / H) < 0.01
    if a.want == 'full': ok = same and vx == 0 and vy == 0 and abs(vw - cw) <= 1 and abs(vh - ch) <= 1; what = '占满画布'
    else: ok = same and abs(vw / vh - 16 / 9) < 0.01 and abs(2 * vx + vw - cw) <= 1 and abs(2 * vy + vh - ch) <= 1; what = '16:9 居中'
    res(ok, f'② 宽高比（要{what}）：视口 {vx},{vy} {vw}x{vh} / 画布 {cw}x{ch}（{cw / ch:.4f}）；截图比例 {W / H:.4f}')
# ①b 内容铺满（视口满不等于内容满—— 全屏两侧各 44 列游戏界面层画的黑柱，原判据测不出）：
#    取屏幕中间 80% 的行，每列最大亮度 ≤ 1 记为「整列纯黑」。全屏口径须 0 列；16:9 口径只报数（负对照应约 2×260 列）
# ①b Content fills the screen (a full viewport does not mean full content -- in full screen, the 44-column black pillars drawn by the game UI layer on each side were not caught by the original criterion):
#    take the middle 80% of the screen's rows; a column whose max brightness ≤ 1 counts as an "entirely black column". The full-screen criterion requires 0 columns; the 16:9 criterion only reports the count (the negative control should be about 2×260 columns)
band = img[int(H * 0.1):int(H * 0.9)].max(axis=(0, 2)); blk = band <= 1; nb = int(blk.sum())
lb = int(np.argmax(~blk)) if (~blk).any() else W; rb = int(np.argmax(~blk[::-1])) if (~blk).any() else W
if a.want == 'full': res(nb == 0, f'①b 内容铺满：整列纯黑 {nb} 列（左起连续 {lb}、右起连续 {rb}；判据 0）')
else: print(f'   ①b（16:9 口径只报数）整列纯黑 {nb} 列（左起连续 {lb}、右起连续 {rb}）')
sx, sy = W / cw, H / ch
reg = img[int(vy * sy):int((vy + vh) * sy), int(vx * sx):int((vx + vw) * sx)]
frac = float((reg.max(axis=2) < BLACK_MAX).mean()) if reg.size else 1.0
res(frac <= BLACK_FRAC, f'③ 不大面积全黑：视口区域近黑像素占 {frac * 100:.1f}%（阈值 ≤ {BLACK_FRAC * 100:.0f}%）')
sys.exit(1 if bad else 0)
