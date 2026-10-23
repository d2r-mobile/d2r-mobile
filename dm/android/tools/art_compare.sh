#!/bin/bash
# 安卓触屏控件与 iOS 画法逐像素对比（防两边各改各的、画法走开）：
#   iOS 一侧：art_ref.m 直接用 dm/ios/Sources/DmTouchControlArt.h 在 Mac 上离屏画参考图；
#   安卓一侧：手机上的调试包按同样尺寸用 TouchOverlay 的 Java 画法写 PNG（DmActivity 收到 dmArtDump 只做这件事就退出）。
# 用法：art_compare.sh <已导入的 overlay 目录（电脑上那份，d2rm_android.py 出的 android/overlay）> [包名]
#   ADB：adb 命令（缺省 adb；手机接在别的机器上就写成 "ssh 主机 adb -s 序列号"）；手机须 480 dpi（3 倍），装的是调试包且已导入。
# 判据（逐张，预乘 RGBA、0–255）：平均差 ≤ 2、99 分位差 ≤ 32——两套抗锯齿实现（CoreGraphics / Skia）边缘差几个级，图案或参数一走样就远超。
# Pixel-by-pixel comparison of the Android touch controls with the iOS drawing (so the two do not drift apart when each side changes):
#   iOS side: art_ref.m uses dm/ios/Sources/DmTouchControlArt.h directly to draw the reference images offscreen on the Mac;
#   Android side: the debug build on the phone writes PNGs of the same sizes with TouchOverlay's Java drawing (DmActivity does only that and exits when it gets dmArtDump).
# Usage: art_compare.sh <imported overlay directory (the copy on the computer, android/overlay from d2rm_android.py)> [package name]
#   ADB: the adb command (default adb; with the phone on another machine write "ssh host adb -s serial"); the phone must be 480 dpi (3x), with the debug build installed and the pack imported.
# Criterion (per image, premultiplied RGBA, 0–255): mean difference ≤ 2 and 99th percentile ≤ 32 -- the two antialiasing implementations (CoreGraphics / Skia) differ by a few levels at edges; a wrong pattern or parameter goes far beyond that.
set -u
OV=${1:?用法：art_compare.sh <overlay 目录> [包名]}; PKG=${2:-com.xiaoboluo.d2r}; ADB=${ADB:-adb}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); O=${DM_ROOT:-$HOME/dm-work}/build/android_art; rm -rf "$O" && mkdir -p "$O/ref" "$O/dev"
clang -fobjc-arc -O1 -o "$O/art_ref" "$HERE/art_ref.m" -framework Foundation -framework CoreGraphics -framework ImageIO -framework UniformTypeIdentifiers || exit 1
"$O/art_ref" "$OV/touchart/dm_mobile_socket.png" "$OV/hud_v4_ui/sil_dm_native_skill_0.png" "$O/ref" || exit 1
d=$($ADB shell wm density | awk '/Physical/{print $3}'); [ "$d" = 480 ] || { echo "🔴 手机密度 ${d}，不是 480（3 倍）"; exit 1; }
$ADB shell run-as "$PKG" rm -rf files/dm/artdump
$ADB shell am start -W -n "$PKG/com.xiaoboluo.d2r.DmActivity" --ez dmArtDump true > /dev/null || exit 1
for i in $(seq 1 20); do [ "$($ADB shell run-as "$PKG" ls files/dm/artdump 2> /dev/null | grep -c png)" -ge 15 ] && break; sleep 1; done
for f in $($ADB shell run-as "$PKG" ls files/dm/artdump | tr -d '\r'); do $ADB exec-out run-as "$PKG" cat "files/dm/artdump/$f" > "$O/dev/$f"; done
python3 - "$O/ref" "$O/dev" <<'EOF'
import os, sys
import numpy as np
from PIL import Image
ref, dev = sys.argv[1:3]; bad = 0; names = sorted(os.listdir(ref))
def pm(p):
    a = np.asarray(Image.open(p).convert('RGBA')).astype(np.float64); a[..., :3] *= a[..., 3:4] / 255; return a
print('%-16s %10s %8s %8s  %s' % ('控件', '尺寸', '平均差', '99分位', '判'))
for n in names:
    if not os.path.exists(os.path.join(dev, n)): print('%-16s 手机没出这张' % n); bad += 1; continue
    a, b = pm(os.path.join(ref, n)), pm(os.path.join(dev, n))
    if a.shape != b.shape: print('%-16s 尺寸不同 %s / %s' % (n, a.shape, b.shape)); bad += 1; continue
    d = np.abs(a - b); mean, p99 = d.mean(), np.percentile(d, 99); ok = mean <= 2 and p99 <= 32; bad += not ok
    print('%-16s %10s %8.2f %8.1f  %s' % (n[:-4], '%dx%d' % a.shape[1::-1], mean, p99, '✓' if ok else '✗'))
print('共 %d 张，不过 %d 张' % (len(names), bad)); sys.exit(1 if bad or not names else 0)
EOF
