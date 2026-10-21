#!/usr/bin/env bash
# 构建仅含自有合成样例的 NativeActivity 诊断 APK；依据 SDK aapt2 / zipalign / apksigner，不读取游戏或用户密钥。
# Build a NativeActivity diagnostic APK with owned synthetic fixtures only; based on SDK aapt2 / zipalign / apksigner, never read games or user keys.
set -euo pipefail
umask 077
here=$(cd "$(dirname "$0")" && pwd)
source "$here/../paths.sh"
sdk=${ANDROID_SDK_ROOT:-$DM_ROOT/tools/android/sdk}
tools=$sdk/build-tools/35.0.0
framework=$sdk/platforms/android-35/android.jar
for executable in aapt2 zipalign apksigner; do
    [[ -x "$tools/$executable" ]] || { echo "Missing Android SDK build-tools 35.0.0: $tools/$executable" >&2; exit 1; }
done
[[ -f "$framework" ]] || { echo "Missing Android SDK platform 35: $framework" >&2; exit 1; }
bash "$here/../build.sh" -DDM_ANDROID_VULKAN=ON
host_build=${DM_ANDROID_HOST_BUILD_DIR:-$DM_ROOT/build/host-sanitized}
if [[ ! -f "$host_build/CMakeCache.txt" ]]; then bash "$here/../test.sh"; fi
cmake --build "$host_build" --target dm_artifact_test --parallel
native_build=${DM_ANDROID_BUILD_DIR:-$DM_ROOT/build/android/arm64-v8a}
build=$DM_ROOT/build/android/apk-diagnostic
mkdir -p "$build" "$TMPDIR" "$DM_ROOT/keys"
stage=$(mktemp -d "$TMPDIR/dm-diagnostic-apk.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/res/values" "$stage/res/values-zh-rCN" "$stage/assets" "$stage/lib/arm64-v8a"
table=$here/../../i18n/ui_text.tsv
english=$(awk -F '\t' '$1=="dmAndroidDiagnosticName" {print $3;exit}' "$table")
chinese=$(awk -F '\t' '$1=="dmAndroidDiagnosticName" {print $2;exit}' "$table")
[[ -n "$english" && -n "$chinese" ]]
printf '<resources><string name="app_name">%s</string></resources>\n' "$english" > "$stage/res/values/strings.xml"
printf '<resources><string name="app_name">%s</string></resources>\n' "$chinese" > "$stage/res/values-zh-rCN/strings.xml"
"$host_build/dm_artifact_test" --export "$stage/assets/synthetic.dma"
cp "$native_build/vulkan/indexed_textured.vert.spv" "$stage/assets/"
cp "$native_build/vulkan/indexed_textured.frag.spv" "$stage/assets/"
cp "$native_build/vulkan/hud.vert.spv" "$stage/assets/"
cp "$native_build/vulkan/hud.frag.spv" "$stage/assets/"
cp "$native_build/libdm_android_app.so" "$stage/lib/arm64-v8a/"
# #170 真实 Java 焦点边界：用 android.jar 编译，再 d8 成 classes.dex 一并打包；不再只是 hasCode=false 的原生壳。
# #170 real Java focus boundary: compile against android.jar, dex with d8 and package classes.dex.
java_src=$here/java
javac_bin=${JAVAC:-javac}
command -v "$javac_bin" >/dev/null || { echo "Missing javac for app/java" >&2; exit 1; }
mkdir -p "$stage/classes" "$stage/dex"
"$javac_bin" --release 11 -classpath "$framework" -d "$stage/classes" \
    $(find "$java_src" -name '*.java' -type f | sort)
"$tools/d8" --release --min-api 29 --lib "$framework" --output "$stage/dex" \
    $(find "$stage/classes" -name '*.class' -type f | sort)
cp "$stage/dex/classes.dex" "$stage/classes.dex"
"$tools/aapt2" compile --dir "$stage/res" -o "$stage/resources.zip"
"$tools/aapt2" link -o "$stage/unsigned.apk" --manifest "$here/AndroidManifest.xml" -I "$framework" -A "$stage/assets" "$stage/resources.zip"
(cd "$stage" && zip -0 -q unsigned.apk lib/arm64-v8a/libdm_android_app.so classes.dex)
"$tools/zipalign" -P 16 -f 4 "$stage/unsigned.apk" "$stage/aligned.apk"
keystore=$DM_ROOT/keys/android-diagnostic-debug.p12
if [[ ! -f "$keystore" ]]; then
    keytool -genkeypair -noprompt -storetype PKCS12 -keystore "$keystore" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 3650 -dname 'CN=Android Debug,O=D2R Development,C=DE'
fi
output=$build/d2r-diagnostic-debug.apk
sidecar=$output.provenance.json
rm -f "$sidecar"
"$tools/apksigner" sign --ks "$keystore" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$output" "$stage/aligned.apk"
"$tools/apksigner" verify --verbose "$output"
"$tools/zipalign" -c -P 16 -v 4 "$output"
python3 "$here/provenance.py" write \
    --apk "$output" \
    --output "$sidecar" \
    --source-root "$here/../../.." \
    --native-build "$native_build" \
    --build-tools "$tools" \
    --min-sdk 29 \
    --target-sdk 35
"$tools/aapt2" dump badging "$output"
printf 'Diagnostic APK (synthetic ARM64 / Vulkan only, no game): %s\n' "$output"
printf 'Provenance sidecar: %s\n' "$sidecar"
