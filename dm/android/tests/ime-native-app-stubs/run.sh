#!/usr/bin/env bash
# #190 focused bridge test: actual host/View/connection; native seam only.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}}
framework=${ANDROID_JAR:-"$sdk/platforms/android-35/android.jar"}
javac=${JAVAC:-javac}
java=${JAVA:-java}
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ime-native-app.XXXXXX")
trap 'rm -rf "$work"' EXIT
[[ -f "$framework" ]] || { echo "android.jar not found: $framework" >&2; exit 2; }
mkdir -p "$work/stubs" "$work/classes" "$work/real"
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
    "$here/DmTextEditorJniBridgeTest.java"
"$java" -cp "$work/stubs:$work/classes:$framework" \
    org.d2rmobile.android.diagnostic.DmTextEditorJniBridgeTest
"$javac" --release 11 -classpath "$framework" -d "$work/real" \
    $(find "$root/dm/android/app/java" -name '*.java' -type f | sort)
echo "real android.jar compile: ok"
