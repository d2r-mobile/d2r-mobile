#!/usr/bin/env bash
# #170 主机 Java 测试：只替换 android.media 边界，编译真实 helper 源码并验证请求/放弃/释放语义。
# #170 host Java tests: replace only the android.media boundary, compile the real helper source and verify
# request/abandon/release semantics.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
framework=${DM_ANDROID_JAR:-}
if [[ -z "$framework" && -n "$sdk" ]]; then
    framework=$sdk/platforms/android-35/android.jar
fi
command -v javac >/dev/null 2>&1 || { echo "javac not found; install a JDK" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-af-java.XXXXXX")
trap 'rm -rf "$work"' EXIT

javac -d "$work/stubs" $(find "$here/java-stubs/android" -name '*.java' -type f | sort)
javac -cp "$work/stubs" -d "$work/classes" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java" \
    "$here/java-stubs/AudioFocusBoundaryTest.java"
java -cp "$work/stubs:$work/classes" AudioFocusBoundaryTest

# 真实 android.jar 是必需 CI 的硬性条件；本地可选模式会明确标注 skip，绝不假装已验证。
# The real android.jar is mandatory in required CI; the optional local workflow labels its skip explicitly.
if [[ -n "$framework" && -f "$framework" ]]; then
    javac --release 11 -classpath "$framework" -d "$work/real" \
        "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java"
    echo "real android.jar compile: ok"
elif [[ "${DM_JAVA_REQUIRE_ANDROID:-0}" == "1" ]]; then
    echo "required Java mode: android.jar missing ($framework)" >&2
    exit 1
else
    echo "optional local workflow: real android.jar compile skipped (set ANDROID_SDK_ROOT or DM_ANDROID_JAR)" >&2
fi
