#!/bin/bash
# 把 Mac 宿主包成能双击起的 macOS App：mac_app.sh [输出目录]   产出 <输出目录>/D2R Mobile.app（缺省 $DM_ROOT/out/macapp）
# 包里有：宿主程序、改写后的游戏程序（产物目录，种类 DM_MAC_PRODUCT，缺省 touch）、有界面覆盖层就带上（DM_MAC_OVERLAY=<目录>）。
# DM_MAC_LINK=1：不带派生物的出法——改写后的游戏程序、着色器库、界面覆盖层都是从游戏内容派生的，公开发布的包不能带；
#   这样出的包里只有宿主程序和 Resources/paths.txt（三项各指向本机构建产物的路径），只在构建它的这台机器上能跑。
# 游戏资源不进包：第一次启动请玩家选资源文件夹（host/app_mac.m）。签名用本地临时签名，本机直接能跑。
# Package the Mac host as a macOS App that starts on double-click: mac_app.sh [output dir]   produces <output dir>/D2R Mobile.app (default $DM_ROOT/out/macapp)
# The package holds: the host program, the rewritten game program (build output directory, kind DM_MAC_PRODUCT, default touch), and the UI overlay when there is one (DM_MAC_OVERLAY=<dir>).
# DM_MAC_LINK=1: the build without derived content -- the rewritten game program, the shader library and the UI overlay are all derived from game content and must not be in a publicly released package;
#   a package built this way holds only the host program and Resources/paths.txt (three entries, each pointing at a build output on this machine) and runs only on the machine that built it.
# Game resources never go into the package: on first launch the player is asked to choose the resource folder (host/app_mac.m). Signing is local ad-hoc signing, which runs on this machine directly.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); DM=$HERE/..; ROOT=${DM_ROOT:-$HOME/dm-work}
OUT=${1:-$ROOT/out/macapp}; KIND=${DM_MAC_PRODUCT:-touch}; BID=${DM_MAC_BUNDLE_ID:-org.d2rmobile.mac}
P=$(bash "$HERE/product.sh" path "$KIND"); [ -f "$P/main.patched.bin" ] || { echo "🔴 没有产物 ${P}（先跑 product.sh build）"; exit 2; }
make -C "$DM/host" > /dev/null
APP="$OUT/D2R Mobile.app"; rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$DM/host/dmhost" "$APP/Contents/MacOS/D2RMobile"
LINK=${DM_MAC_LINK-1}; [ "$LINK" != 0 ] || LINK=; RES="$APP/Contents/Resources"; : > "$RES/paths.txt"
if [ -n "$LINK" ]; then echo "product=$P" >> "$RES/paths.txt"; else cp -R "$P" "$RES/product"; fi
cp "$DM/gfx/nvn/shader_map.tsv" "$APP/Contents/Resources/shader_map.tsv"
[ ! -f "$DM/gfx/nvn/c1_full.tsv" ] || cp "$DM/gfx/nvn/c1_full.tsv" "$APP/Contents/Resources/c1_full.tsv"   # 图形后端按「与映射表同目录」找它 | The graphics backend looks for it "in the same directory as the mapping table"
# 编好的着色器库（Mac 版，dmsh_build.py 出的）：只带运行要用的 .metallib 与反射 .json，源码 .metal 不带
# The compiled shader library (Mac variant, produced by dmsh_build.py): only the .metallib and reflection .json needed at run time are included, not the .metal sources
V=$(sed -n 's/.*DMSH_CHAIN "\(.*\)".*/\1/p' "$DM/gfx/nvn/dmsh_chain.h"); MSL=${DM_MSL_DIR:-$ROOT/build/cache/metal_shader_cache}/b-$V
[ -d "$MSL" ] || { echo "🔴 没有着色器库 ${MSL}（先跑 dm/gfx/tools/dmsh_build.py）"; exit 2; }
if [ -n "$LINK" ]; then echo "msl=$(dirname "$MSL")" >> "$RES/paths.txt"
else mkdir -p "$RES/msl/b-$V"; rsync -a --include='*.metallib' --include='*.json' --exclude='*' "$MSL/" "$RES/msl/b-$V/"; fi
# 界面覆盖层（自定义页、珍宝匣、秘境等我们的页面都在里面）：从游戏资源现生成（约 3 秒）；DM_MAC_OVERLAY=<目录> 用现成的，=none 不带（原版界面）
# UI overlay (the Custom page, the trove, rifts and our other pages all live in it): generated on the spot from the game resources (about 3 seconds); DM_MAC_OVERLAY=<dir> uses an existing one, =none leaves it out (original UI)
if [ "${DM_MAC_OVERLAY:-}" != none ]; then
  OVD="$RES/romfs_overlay"; [ -z "$LINK" ] || { OVD="$OUT/romfs_overlay.local"; rm -rf "$OVD"; echo "romfs_overlay=$OVD" >> "$RES/paths.txt"; }
  if [ -n "${DM_MAC_OVERLAY:-}" ]; then cp -R "$DM_MAC_OVERLAY" "$OVD"
  else DM_OVERLAY_MAC=1 python3 "$DM/ios/tools/gen_romfs_overlay.py" "${DM_ROMFS_SRC:-$ROOT/data/switch/3.3.94012/Update/RomFS}" "$OVD" > /dev/null || { echo "🔴 界面覆盖层生成失败"; exit 2; }; fi
fi
[ -s "$RES/paths.txt" ] || rm -f "$RES/paths.txt"
VER=$(cat "$DM/PRODUCT")
cat > "$APP/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>D2R Mobile</string>
<key>CFBundleDisplayName</key><string>D2R Mobile</string>
<key>CFBundleIdentifier</key><string>$BID</string>
<key>CFBundleExecutable</key><string>D2RMobile</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VER</string>
<key>CFBundleVersion</key><string>$(git -C "$DM" rev-parse --short HEAD 2>/dev/null || echo 0)</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>LSApplicationCategoryType</key><string>public.app-category.role-playing-games</string>
<key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
PL
codesign --force -s - "$APP" > /dev/null 2>&1 || { echo "🔴 本地签名失败"; exit 3; }
echo "MACAPP ${APP}（产物 $(basename "$P")，$(du -sh "$APP" | cut -f1)${LINK:+，不带派生物：只能在本机跑}）"
