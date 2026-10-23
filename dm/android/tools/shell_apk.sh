#!/bin/bash
# 出安卓的发布包：一个不含任何游戏内容的 APK。玩家用自己的游戏文件在电脑上做导入包（dm/tools/d2rm_android.py），拷进手机，App 打开时导入。
#   shell_apk.sh [release] [gradle 参数…]      例：shell_apk.sh -PdmAppId=org.example.test（换包名并排装）
#   缺省出调试包（可调试，测试用：art_compare.sh 要 run-as）；release = 发布用的正式包：release 构建、用正式钥匙签，钥匙与密码在本机
#   ${DM_ANDROID_SIGN:-~/.config/dm-android/release.env}（DM_ANDROID_KEYSTORE / DM_ANDROID_KEY_ALIAS / DM_ANDROID_KEY_PASS），不进仓库
# 产物（$DM_ROOT/build/android_shell/）：D2RMobile-android.apk；gen/ = 编进去的替身表；ref/ = 用本机游戏文件生成的真表（只用来核对，不发布）
# 做法同 iOS 壳（dm/ios/tools/shell_build.sh）：三份「从游戏文件生成、编进代码」的文件（经验表、编年史仓库表、技能图标索引）换成替身，
#   运行时从导入的 files/dm/derived.tsv 读（dm/host/dm_derived_rt.h）；亮度表 dm_bright_levels.inc 照 iOS 壳编进去（每个关卡号一位：随不随昼夜）。
# 游戏程序、着色器、覆盖层、界面素材本来就不进 APK（运行时在 files/dm/）。出包这台机器要有游戏文件：要用真实的生成结果定表的大小。
# 在源码副本里构建（$O/tree，只含仓库跟踪的与未被忽略的文件）：工作树里平时构建留下的真表（dm/host/dm_vault_tab.h 等）会被
#   #include "…" 先于生成目录找到，副本里没有它们。
# 环境：JAVA_HOME（JDK 17+）、ANDROID_HOME（SDK 36 + NDK 29.0.14206865 + CMake 3.31.6）；缺省取 Homebrew 的 openjdk@17 与 ~/Library/Android/sdk
# Build the Android release package: an APK with no game content. Players make the import pack on a computer from their own game files (dm/tools/d2rm_android.py), copy it to the phone, and the App imports it when opened.
#   shell_apk.sh [release] [gradle arguments...]      e.g. shell_apk.sh -PdmAppId=org.example.test (another application id, to install side by side)
#   Default is a debug package (debuggable, for testing: art_compare.sh needs run-as); release = the package for publishing: a release build signed with the release key, whose key and password live on this machine in
#   ${DM_ANDROID_SIGN:-~/.config/dm-android/release.env} (DM_ANDROID_KEYSTORE / DM_ANDROID_KEY_ALIAS / DM_ANDROID_KEY_PASS), never in the repository
# Products ($DM_ROOT/build/android_shell/): D2RMobile-android.apk; gen/ = the stand-in tables compiled in; ref/ = the real tables generated from this machine's game files (only for checking, never released)
# Same approach as the iOS shell (dm/ios/tools/shell_build.sh): the three files "generated from game files and compiled in" (experience table, chronicle vault table, skill icon index) are replaced by stand-ins
#   that read the imported files/dm/derived.tsv at run time (dm/host/dm_derived_rt.h); the brightness table dm_bright_levels.inc is compiled in as in the iOS shell (one bit per level number: follows day and night or not).
# The game program, shaders, overlay and UI art never go into the APK (they are in files/dm/ at run time). The building machine needs the game files: the real generated results set the table sizes.
# Builds in a copy of the source ($O/tree, only tracked and non-ignored files): the real tables a normal build leaves in the working tree (dm/host/dm_vault_tab.h and others) would be
#   found by #include "..." before the generated directory; the copy does not have them.
# Environment: JAVA_HOME (JDK 17+), ANDROID_HOME (SDK 36 + NDK 29.0.14206865 + CMake 3.31.6); default Homebrew's openjdk@17 and ~/Library/Android/sdk
set -u
KIND=debug; [ "${1:-}" = release ] && { KIND=release; shift; }
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); DM=$(cd "$HERE/../.." && pwd)
ROOT=${DM_ROOT:-$HOME/dm-work}; O=$ROOT/build/android_shell; G=$O/gen
ROMFS=${DM_ROMFS_SRC:-$ROOT/data/switch/3.3.94012/Update/RomFS}
PY=${DM_PYTHON:-$ROOT/build/cache/dm/venv/bin/python}
export JAVA_HOME=${JAVA_HOME:-$(brew --prefix openjdk@17 2> /dev/null)/libexec/openjdk.jdk/Contents/Home}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/Library/Android/sdk}; export ANDROID_SDK_ROOT=$ANDROID_HOME
t() { echo "[$(date +%T)] $*"; }; die() { t "🔴 $*"; exit 1; }
[ -f "$ROMFS/data/build_manifest.psv" ] || die "没找到游戏文件 ${ROMFS}（DM_ROMFS_SRC）"
W=$O/tree
rm -rf "$G" "$O/ref" "$O/D2RMobile-android.apk" && mkdir -p "$G" "$O/ref" "$W" || die "建不了 $O"
(cd "$DM/.." && git ls-files -z -co --exclude-standard dm | rsync -a -0 --ignore-missing-args --files-from=- . "$W/") || die "拷源码副本失败"
for f in host/dm_vault_tab.h host/dm_hud_exp.h host/dm_bright_levels.inc ios/Sources/dm_touch_native_art.c; do [ ! -e "$W/dm/$f" ] || die "副本里有生成文件 $f"; done
t "用本机游戏文件生成真表"
"$PY" "$DM/ios/tools/gen_vault_table.py" "$ROMFS" > "$G/dm_vault_tab.h" || die "生成 dm_vault_tab.h 失败"
"$PY" "$DM/ios/tools/gen_hud_exp.py" "$ROMFS" > "$G/dm_hud_exp.h" || die "生成 dm_hud_exp.h 失败"
"$PY" "$DM/tools/gen_bright_levels.py" "$ROMFS" > "$G/dm_bright_levels.inc" || die "生成 dm_bright_levels.inc 失败"
"$PY" "$DM/ios/tools/native_art/generate.py" "$ROMFS" --output-dir "$O/ref/native_art" > /dev/null || die "生成技能图标索引失败"
cp "$O/ref/native_art/dm_touch_native_art.c" "$G/" && cp "$G/dm_vault_tab.h" "$G/dm_hud_exp.h" "$G/dm_touch_native_art.c" "$O/ref/" || die "拷真表失败"
"$PY" "$DM/ios/tools/shell_stubs.py" data-gen "$G" "$O/ref/derived.tsv" || die "拆表失败"
"$PY" "$DM/ios/tools/shell_stubs.py" stubs-gen "$G" || die "写替身失败"
t "构建 APK"
if [ $KIND = release ]; then
  SIGN=${DM_ANDROID_SIGN:-$HOME/.config/dm-android/release.env}; [ -f "$SIGN" ] || die "没有正式钥匙配置 $SIGN"
  set -a; . "$SIGN"; set +a; [ -f "${DM_ANDROID_KEYSTORE:-}" ] && [ -n "${DM_ANDROID_KEY_PASS:-}" ] || die "钥匙配置不全：$SIGN"
  TASK=:app:assembleRelease; OUT=$W/dm/android/app/build/outputs/apk/release/app-release.apk
else unset DM_ANDROID_KEYSTORE; TASK=:app:assembleDebug; OUT=$W/dm/android/app/build/outputs/apk/debug/app-debug.apk; fi
rm -f "$OUT"   # 增量打包会在旧 APK 上追加、留下空洞（实测 25 MB 的内容出了 46 MB），整份重写 | incremental packaging appends to the old APK and leaves holes (25 MB of content came out as 46 MB in a test); rewrite it whole
gradle --no-daemon -q -p "$W/dm/android" $TASK -PdmGenDir="$G" -PdmPython="$PY" "$@" > "$O/build.log" 2>&1 || { grep -E "error|FAIL" "$O/build.log" | head -8; die "构建失败，日志 $O/build.log"; }
cp "$OUT" "$O/D2RMobile-android.apk" || die "没找到构建出的 APK"
# 核对：APK 里不许有游戏内容——真表里的字符串（编年史仓库的物品键与图标路径，只取源码里本来没有的：多语表等会带物品名）不能出现在原生库里，
#   也不许带游戏程序 / 着色器包 / 覆盖层
# Check: no game content in the APK -- strings from the real tables (the chronicle vault's item keys and icon paths, only those not already in the source: the string table and others carry item names) must not appear in the native library,
#   and no game program / shader bundles / overlay may be packed
A=$O/D2RMobile-android.apk; L=$O/libdm.so; unzip -p "$A" lib/arm64-v8a/libdm.so > "$L" || die "APK 里没有 lib/arm64-v8a/libdm.so"
n=0; hit=0
while IFS= read -r k; do [ ${#k} -ge 6 ] || continue; git -C "$DM" grep -qF -- "$k" -- . && continue
  n=$((n + 1)); LC_ALL=C grep -qaF -- "$k" "$L" && { hit=$((hit + 1)); echo "  原生库里有：$k"; }
done < <(awk -F'\t' '$1=="VTROW"{print $8; print $9}' "$O/ref/derived.tsv" | sort -u)
[ "$n" -ge 100 ] || die "能核的字符串只有 $n 个，核不了"
[ "$hit" = 0 ] || die "原生库里出现了真表里的字符串（$hit 处 / 核了 $n 个）：替身没生效"
# 也不许带本机路径（用户名 / 目录结构）：整个 APK 解开逐个文件查（CMakeLists.txt 用 -ffile-prefix-map 换掉了编译路径）
X=$O/apk_x; rm -rf "$X" && mkdir -p "$X" && (cd "$X" && unzip -q "$A") || die "解不开 APK"
leak=$(LC_ALL=C grep -raoE -- "/Users/[A-Za-z0-9._-]+|$HOME" "$X" | head -3)
[ -z "$leak" ] || die "APK 里带着本机路径：$leak"
# 正式包：签名必须是正式钥匙（证书指纹与钥匙库一致、主体只有项目名），且不可调试
# Release package: the signature must be the release key (certificate fingerprint equal to the keystore's, subject only the project name), and it must not be debuggable
if [ $KIND = release ]; then
  BT=$(ls -d "$ANDROID_HOME"/build-tools/* | tail -1)
  got=$("$BT/apksigner" verify --print-certs "$A" 2>&1 | sed -n 's/.*certificate SHA-256 digest: //p' | head -1)
  want=$("$JAVA_HOME/bin/keytool" -list -v -keystore "$DM_ANDROID_KEYSTORE" -storepass "$DM_ANDROID_KEY_PASS" 2>/dev/null | sed -n 's/^.*SHA256: //p' | head -1 | tr -d ':' | tr 'A-F' 'a-f')
  [ -n "$got" ] && [ "$got" = "$want" ] || die "签名不是正式钥匙（APK $got / 钥匙 ${want}）"
  dn=$("$BT/apksigner" verify --print-certs "$A" 2>&1 | sed -n 's/.*certificate DN: //p' | head -1); [ "$dn" = "CN=D2R Mobile, O=D2R Mobile" ] || die "证书主体不对：$dn"
  "$BT/aapt2" dump xmltree --file AndroidManifest.xml "$A" | grep -q 'android:debuggable' && die "正式包是可调试的"
fi
bad=$(unzip -l "$A" | awk '{print $4}' | grep -E '\.(xgs|patched\.bin|data)$|^assets/|overlay|derived\.tsv' | head -3)
[ -z "$bad" ] || die "APK 里带了不该有的文件：$bad"
t "好了（${KIND}）：${A}（$(($(stat -f %z "$A") / 1048576)) MB）；核了真表里 $n 个源码里没有的字符串，原生库里 0 处；APK 里没有游戏程序 / 着色器包 / 覆盖层，也没有本机路径"
