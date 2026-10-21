#!/usr/bin/env bash
# Narrow real-JVM/JNI proof: actual Java bridge -> actual native_text_event.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}}
framework=${ANDROID_JAR:-"$sdk/platforms/android-35/android.jar"}
java_home=${JAVA_HOME:-$(/usr/libexec/java_home -v 21 2>/dev/null || /usr/libexec/java_home)}
javac=${JAVAC:-javac}
java=${JAVA:-java}
cc=${CC:-clang}
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-utf16-jni.XXXXXX")
trap 'rm -rf "$work"' EXIT
[[ -f "$framework" ]] || { echo "android.jar not found: $framework" >&2; exit 2; }
mkdir -p "$work/stubs" "$work/classes"
python3 "$here/extract_native.py" "$root/dm/android/app/native.c" "$work/native_jni.c"
"$cc" -std=c11 -O1 -fPIC -shared \
    -I "$java_home/include" -I "$java_home/include/darwin" \
    -o "$work/libdmutf16jni.dylib" "$work/native_jni.c"
"$javac" --release 11 -cp "$framework" -d "$work/stubs" \
    $(find "$root/dm/android/tests/ime-activity-host-stubs/android" -name '*.java' -type f | sort)
"$javac" --release 11 -cp "$work/stubs:$framework" -d "$work/classes" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorView.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorHost.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsApi30.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObservation.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObserver.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorJniBridge.java" \
    "$here/DmUtf16JniTest.java"
"$java" -Djava.library.path="$work" -cp "$work/stubs:$work/classes:$framework" \
    org.d2rmobile.android.diagnostic.DmUtf16JniTest
