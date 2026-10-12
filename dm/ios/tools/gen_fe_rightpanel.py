#!/usr/bin/env python3
# 选角页右侧面板底图（键鼠版选角页阉割后只留存档列表；去掉「转换角色」）：原版 FrontEnd_RightPanel.sprite 底部两个槽 + 中间十字花，
# 改成居中一个槽——按列从原图同一行段搬过来拼（x 112 起：原 52–109 的左槽、原 112–171 的槽中段、两列过渡、再接左槽），只动 y 980–1039 这一条。
# 编成原包同款 DXT5 + 块线性（gen_widestash_art.bc3 / pack；原始 RGBA 的 v31 在 401×1080 上读出竖条花屏）。
# 用法：gen_fe_rightpanel.py <原版 RomFS（Update）>（构建时跑，产物 Gen/assets/NativeHUD/touch/fe_rightpanel.sprite；gen_romfs_overlay.py 写进借用路径）
# Base image for the right-hand panel of the character select page (the keyboard-and-mouse character select page trimmed down to just the save list; 「转换角色」(Convert Character) removed): the original FrontEnd_RightPanel.sprite has two slots at the bottom + a cross ornament in the middle,
# changed to a single centered slot - assembled column by column from the same row band of the original image (starting at x 112: the left slot from original 52–109, the slot middle from original 112–171, a two-column transition, then the left slot again); only the strip y 980–1039 is touched.
# Encoded as DXT5 + block-linear like the original package (gen_widestash_art.bc3 / pack; raw RGBA v31 reads back as vertical-stripe garbage at 401x1080).
# Usage: gen_fe_rightpanel.py <original RomFS (Update)> (run at build time, output Gen/assets/NativeHUD/touch/fe_rightpanel.sprite; gen_romfs_overlay.py writes it into the borrowed path)
import os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import romfs_art, gen_widestash_art as G
from gen_paths import gen_dir

SRC = 'hd/global/ui/FrontEnd/HD/Final/FrontEnd_RightPanel.sprite'
Y0, Y1 = 980, 1040
COPY = ((112, 52, 58), (170, 112, 60), (230, 53, 1), (231, 52, 1), (232, 52, 58))   # (目标列, 来源列, 列数) | (destination column, source column, column count)

def main(romfs):
    b = romfs_art.read(romfs, SRC); o = np.array(romfs_art.sprite(romfs, SRC)); w, h = o.shape[1], o.shape[0]
    assert (w, h) == (401, 1080), (w, h)
    m = o.copy()
    for d, s, n in COPY: m[Y0:Y1, d:d + n] = o[Y0:Y1, s:s + n]
    p = os.path.join(gen_dir('NativeHUD', 'touch'), 'fe_rightpanel.sprite')
    open(p, 'wb').write(G.pack(G.lib(), b, G.bc3(Image.fromarray(m, 'RGBA')), w, h, w))
    print('fe_rightpanel：%d×%d → %s' % (w, h, p))

if __name__ == '__main__':
    main(sys.argv[1])
