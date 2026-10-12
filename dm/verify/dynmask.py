# 动画遮罩：同一冻结拍连截的两张图（服务端停在同一拍、客户端动画照走）逐像素做差，差异像素膨胀一圈 = 「在动的区域」，闸门比对时遮掉
# 用法：dynmask.py <a.png> <b.png> <输出遮罩.png> [阈值=8] [膨胀像素=3]   遮罩白 = 在动；打印动区占比
# Animation mask: diff two screenshots taken back to back at the same frozen tick (server stopped on the same tick, client animation still running) pixel by pixel; differing pixels dilated by one ring = the "moving region", masked out during gate-check comparison
# Usage: dynmask.py <a.png> <b.png> <output mask.png> [threshold=8] [dilation pixels=3]   white in mask = moving; prints the moving-region share
import sys
import numpy as np
from PIL import Image, ImageFilter
a, b, out = sys.argv[1:4]; th = int(sys.argv[4]) if len(sys.argv) > 4 else 8; dil = int(sys.argv[5]) if len(sys.argv) > 5 else 3
A = np.asarray(Image.open(a).convert('RGB'), dtype=int); B = np.asarray(Image.open(b).convert('RGB').resize((A.shape[1], A.shape[0])), dtype=int)
m = Image.fromarray(((np.abs(A - B).sum(2) > th) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * dil + 1))
m.save(out); print(f'DYNMASK 动区占比 {np.asarray(m).mean() / 255:.3f} → {out}')
