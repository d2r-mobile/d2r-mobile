#!/bin/bash
# Apple TV 版构建：生成（与 iOS 同一个 ios/run.sh build 的生成步）→ tvOS 着色器（build_shaders.py）→ 图标（gen_icon.py）→ xcodebuild 目标 DmTV（实机）。
# 用法：dm/tvos/build.sh            要在 Xcode 里登录能签 tvOS 的账号；本机配置的 DM_TEAM_ID / DM_BUNDLE_ID（包名 = DM_BUNDLE_ID.tv）
#       DM_TV_NOSIGN=1 dm/tvos/build.sh   不签名，只核能不能编过（没有 Apple TV 和账号时用）
# 只出实机：tvOS 模拟器的显卡没有 Metal 4、不是统一内存、参数缓冲只有一档，图形层起不来（main.m 开头）。
# 环境：DM_MSL_MAC = Mac 着色器缓存根目录（缺省 $DM_ROOT/build/cache/metal_shader_cache）；DM_TV_DD = DerivedData 目录（缺省 $DM_ROOT/run/tvos/dd）
# Apple TV build: generate (the same generate step as iOS ios/run.sh build) → tvOS shaders (build_shaders.py) → icon (gen_icon.py) → xcodebuild target DmTV (device).
# Usage: dm/tvos/build.sh            requires an account that can sign tvOS logged in to Xcode; uses the locally configured DM_TEAM_ID / DM_BUNDLE_ID (bundle ID = DM_BUNDLE_ID.tv)
#       DM_TV_NOSIGN=1 dm/tvos/build.sh   no signing, only checks that it compiles (for use without an Apple TV and account)
# Device builds only: the tvOS simulator GPU has no Metal 4, no unified memory, and only one argument buffer tier, so the graphics layer cannot start (top of main.m).
# Environment: DM_MSL_MAC = Mac shader cache root (default $DM_ROOT/build/cache/metal_shader_cache); DM_TV_DD = DerivedData directory (default $DM_ROOT/run/tvos/dd)
set -u
cd "$(dirname "$0")/../ios"
t(){ echo "[$(date +%T)] $*"; }
ROOT=${DM_ROOT:-$HOME/dm-work}; DD=${DM_TV_DD:-$ROOT/run/tvos/dd}; LOG=$ROOT/run/tvos/build.log; mkdir -p "$ROOT/run/tvos"
SIGN=(-allowProvisioningUpdates); [ "${DM_TV_NOSIGN:-0}" = 1 ] && SIGN=(CODE_SIGNING_ALLOWED=NO)
DM_GEN_ONLY=1 ./run.sh build || { t "🔴 生成失败（ios/run.sh build）"; exit 1; }
python3 ../tvos/build_shaders.py "${DM_MSL_MAC:-$ROOT/build/cache/metal_shader_cache}" "$ROOT/build/tvos/msl" || { t "🔴 tvOS 着色器没编齐"; exit 1; }
python3 ../tvos/gen_icon.py Assets.xcassets/AppIcon.appiconset/AppIcon1024.png Gen/tvos/Assets.xcassets || { t "🔴 tvOS 图标没生成（要 Pillow）"; exit 1; }
V=$(sed -n 's/^#define DMSH_CHAIN "\(.*\)"/\1/p' ../gfx/nvn/dmsh_chain.h)
rm -rf Gen/tvos/msl; mkdir -p Gen/tvos/msl && cp -cR "$ROOT/build/tvos/msl/b-$V" Gen/tvos/msl/ && find Gen/tvos/msl -name '*.tvos.sha256' -delete
xcodebuild -project DmIOS.xcodeproj -scheme DmTV -configuration Release -sdk appletvos -destination 'generic/platform=tvOS' -derivedDataPath "$DD" \
  "${SIGN[@]}" build > "$LOG" 2>&1 || { t "🔴 构建失败（${LOG}）"; grep -E "error:" "$LOG" | head -10; exit 1; }
t "构建完成：$DD/Build/Products/Release-appletvos/DmTV.app"
