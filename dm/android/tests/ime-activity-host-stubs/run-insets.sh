#!/usr/bin/env bash
# #196 focused host test for the additive IME inset observer. The exact
# production host/View/connection + observer are compiled against narrow
# Activity/View/WindowInsets stubs, then against the real android.jar.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}}
framework=${ANDROID_JAR:-"$sdk/platforms/android-35/android.jar"}
javac=${JAVAC:-javac}
java=${JAVA:-java}
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ime-insets.XXXXXX")
trap 'rm -rf "$work"' EXIT

[[ -f "$framework" ]] || { echo "android.jar not found: $framework" >&2; exit 2; }
mkdir -p "$work/stubs" "$work/classes" "$work/real"
"$javac" --release 11 -cp "$framework" -d "$work/stubs" \
    $(find "$here/android" -name '*.java' -type f | sort)
"$javac" --release 11 -cp "$work/stubs:$framework" -d "$work/classes" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorView.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorHost.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObserver.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObservation.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsApi30.java" \
    "$here/DmImeInsetsTest.java"
"$java" -cp "$work/stubs:$work/classes:$framework" \
    org.d2rmobile.android.diagnostic.DmImeInsetsTest
"$javac" --release 11 -classpath "$framework" -d "$work/real" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorView.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorHost.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObserver.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObservation.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsApi30.java"
echo "real android.jar compile: ok"
