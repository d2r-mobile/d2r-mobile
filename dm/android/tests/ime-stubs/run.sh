#!/usr/bin/env bash
# #173 focused host test for the actual InputConnection boundary.
# Compiles the exact production source and test against the real android.jar.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}}
jar=${ANDROID_JAR:-"$sdk/platforms/android-35/android.jar"}
javac=${JAVAC:-javac}
java=${JAVA:-java}

if [[ ! -f "$jar" ]]; then
    echo "android.jar not found: $jar" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ime-boundary.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$javac" -encoding UTF-8 -cp "$jar" -d "$work/classes" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java" \
    "$root/dm/android/tests/ime-stubs/DmInputConnectionTest.java"

"$java" -cp "$jar:$work/classes" org.d2rmobile.android.diagnostic.DmInputConnectionTest
