#!/usr/bin/env bash
# #187 focused host test: actual DmTextEditorHost + accepted View/connection.
# Narrow Activity/ViewGroup/View/IMM stubs are local to this test; exact
# production sources are also compiled against the real android.jar.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}}
framework=${ANDROID_JAR:-"$sdk/platforms/android-35/android.jar"}
javac=${JAVAC:-javac}
java=${JAVA:-java}
host_source=${IME_HOST_SOURCE:-$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorHost.java}
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ime-activity-host.XXXXXX")
trap 'rm -rf "$work"' EXIT

[[ -f "$framework" ]] || { echo "android.jar not found: $framework" >&2; exit 2; }
[[ -f "$host_source" ]] || { echo "IME host source not found: $host_source" >&2; exit 2; }
mkdir -p "$work/stubs" "$work/classes" "$work/real"
"$javac" --release 11 -cp "$framework" -d "$work/stubs" \
    $(find "$here/android" -name '*.java' -type f | sort)
"$javac" --release 11 -cp "$work/stubs:$framework" -d "$work/classes" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorView.java" \
    "$host_source" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObserver.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObservation.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsApi30.java" \
    "$here/DmTextEditorHostTest.java"
"$java" -cp "$work/stubs:$work/classes:$framework" \
    org.d2rmobile.android.diagnostic.DmTextEditorHostTest
"$javac" --release 11 -classpath "$framework" -d "$work/real" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorView.java" \
    "$host_source" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObserver.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsObservation.java" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmImeInsetsApi30.java"
echo "real android.jar compile: ok"
