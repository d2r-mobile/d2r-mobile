#!/usr/bin/env bash
# #184 host JVM 测试驱动：编译真实 Java bridge/harness（窄 Android stub），加载真实 C 适配器 .so 并运行。
# #184 host JVM driver: compile the real Java bridge/harness against narrow Android stubs, load the actual C adapter
# shared library and run it.
set -euo pipefail
lib=$1
javac=$2
java=$3
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-afj.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$javac" -encoding UTF-8 -d "$work/stubs" $(find "$here/java-stubs/android" -name '*.java' -type f | sort)
"$javac" -encoding UTF-8 -cp "$work/stubs" -d "$work/classes" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusJniBridge.java" \
    "$here/AudioFocusJniHarness.java"
"$java" -Xcheck:jni -Djava.library.path="$(dirname "$lib")" -cp "$work/stubs:$work/classes" AudioFocusJniHarness
