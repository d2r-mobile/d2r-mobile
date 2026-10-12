# 两侧黑柱检查：统计截图里「整列纯黑」（所有像素 RGB 都 ≤ 阈值）的列数，分左半 / 右半报；用于宽屏黑柱回归
# 用法：blackcols.py <png> [阈值=4]   退出码 0 = 无整列纯黑
# Side black-bar check: counts the columns in a screenshot that are "entirely pure black" (every pixel's RGB ≤ threshold), reported separately for the left half / right half; used for the widescreen black-bar regression
# Usage: blackcols.py <png> [threshold=4]   exit code 0 = no entirely black column
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.int16)
th = int(sys.argv[2]) if len(sys.argv) > 2 else 4
black = (a <= th).all(axis=2).all(axis=0)
h, w = a.shape[:2]; left = int(black[:w // 2].sum()); right = int(black[w // 2:].sum())
runs = []; i = 0
while i < w:
    if black[i]:
        j = i
        while j + 1 < w and black[j + 1]: j += 1
        runs.append((i, j)); i = j + 1
    else: i += 1
print(f'{sys.argv[1]}：{w}x{h}，整列纯黑 左 {left} 列、右 {right} 列，区段 {runs[:6]}')
sys.exit(1 if left + right else 0)
