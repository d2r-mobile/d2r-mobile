#!/usr/bin/env bash
# #200 host JVM 驱动：真实 Java bridge/harness + 窄 Android stub，加载真实 native_audio/session 栈。
# #200 host JVM driver compiling the real bridge/harness against narrow Android stubs over the actual C stack.
set -euo pipefail
lib=$1
javac=$2
java=$3
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-naudio.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$javac" -encoding UTF-8 -d "$work/stubs" $(find "$here/java-stubs/android" -name '*.java' -type f | sort)
"$javac" -encoding UTF-8 -cp "$work/stubs" -d "$work/classes" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java" \
    "$here/../app/java/org/d2rmobile/android/diagnostic/AudioFocusJniBridge.java" \
    "$here/NativeAudioHarness.java"
"$java" -Xcheck:jni -Djava.library.path="$(dirname "$lib")" -cp "$work/stubs:$work/classes" NativeAudioHarness
