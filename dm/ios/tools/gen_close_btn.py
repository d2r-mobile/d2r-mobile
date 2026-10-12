#!/usr/bin/env python3
# 局内面板外框右上角的关闭按钮图（凹槽石面原样透出，上面嵌一个贴满凹槽的旧金色 X）。产出 NativeHUD/touch/close.png，由 gen_romfs_overlay.py 写进借用路径。
# 用法：python3 dm/ios/tools/gen_close_btn.py   尺寸：凹槽在 2868×1320 下 33.5×45.5 像素；界面贴图 1 像素 = 2 布局单位 = 1.394 屏幕像素 → 24×33，底透明
# Close button image for the top-right corner of the in-run panel frame (the groove's stone surface shows through unchanged, with an old-gold X inlaid on it that fills the groove). Produces NativeHUD/touch/close.png, written into the borrowed path by gen_romfs_overlay.py.
# Usage: python3 dm/ios/tools/gen_close_btn.py   Size: the groove is 33.5×45.5 pixels at 2868×1320; 1 UI texture pixel = 2 layout units = 1.394 screen pixels → 24×33, transparent background
import os
from PIL import Image, ImageDraw, ImageFilter, ImageChops
W, H, S = 24, 33, 16
def strokes(r, wd, off=(0, 0)):
    m = Image.new('L', (W * S, H * S), 0); d = ImageDraw.Draw(m); cx, cy = W * S / 2 + off[0] * S, H * S / 2 + off[1] * S
    hw, hh = 8.3 * r / 6.2, 13.2 * r / 6.2; cx += 0.1 * S; cy += 0.4 * S   # 贴满凹槽内框：半宽 8.1、半高 13.3（贴图像素），r = 6.2 时正好到角 | Fills the groove's inner frame: half-width 8.1, half-height 13.3 (texture pixels); reaches the corners exactly when r = 6.2
    for sx in (1, -1): d.line((cx - sx * hw * S, cy - hh * S, cx + sx * hw * S, cy + hh * S), fill=255, width=int(wd * S))
    return m
im = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
def lay(mask, col, a): 
    global im; l = Image.new('RGBA', im.size, col + (0,)); l.putalpha(mask.point(lambda v: int(v * a))); im = Image.alpha_composite(im, l)
# 旧金嵌条：暗描边 + 金色笔画 + 左上高光
# Old-gold inlay: dark outline + gold strokes + top-left highlight
lay(strokes(6.3, 5.0, (0.35, 0.45)), (8, 7, 6), 0.75); lay(strokes(6.2, 3.7), (150, 118, 62), 1.0); lay(strokes(6.0, 1.3, (-0.35, -0.35)), (226, 198, 132), 0.9)
lay(strokes(6.0, 0.9, (0.5, 0.5)), (92, 68, 34), 0.8)
im = im.resize((W, H), Image.LANCZOS); im.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'NativeHUD', 'touch', 'close.png'))
