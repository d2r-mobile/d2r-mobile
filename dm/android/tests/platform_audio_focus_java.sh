#!/usr/bin/env bash
# #170 主机 Java 测试：只替换 android.media 边界，编译真实 helper 源码并验证请求/放弃/释放语义。
# #170 host Java tests: replace only the android.media boundary, compile the real helper source and verify
# request/abandon/release semantics.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
sdk=${ANDROID_SDK_ROOT:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}
framework=$sdk/platforms/android-35/android.jar
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-af-java.XXXXXX")
trap 'rm -rf "$work"' EXIT

javac -d "$work/stubs" $(find "$here/java-stubs/android" -name '*.java' -type f | sort)
javac -cp "$work/stubs" -d "$work/classes" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java" \
    "$here/java-stubs/AudioFocusBoundaryTest.java"
java -cp "$work/stubs:$work/classes" AudioFocusBoundaryTest

# 有真实 android.jar 时再验证同一源码对真实平台签名可编译。| Also compile the same source against the real SDK when present.
if [[ -f "$framework" ]]; then
    javac --release 11 -classpath "$framework" -d "$work/real" \
        "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java"
    echo "real android.jar compile: ok"
fi
