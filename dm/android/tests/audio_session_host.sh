#!/usr/bin/env bash
# #195 主机 JVM 集成测试驱动：编译真实 Java bridge 与 harness（窄 Android stub），加载真实 C audio_session 栈。
# #195 host JVM driver: compile the real Java bridge/harness against narrow Android stubs and load the actual
# audio_session C stack.
set -euo pipefail
lib=$1
javac=$2
java=$3
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-afsession.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$javac" -encoding UTF-8 -d "$work/stubs" $(find "$here/java-stubs/android" -name '*.java' -type f | sort)
"$javac" -encoding UTF-8 -cp "$work/stubs" -d "$work/classes" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusJniBridge.java" \
    "$here/AudioSessionHarness.java"
"$java" -Xcheck:jni -Djava.library.path="$(dirname "$lib")" -cp "$work/stubs:$work/classes" AudioSessionHarness
