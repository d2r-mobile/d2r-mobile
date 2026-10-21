#!/usr/bin/env bash
# #173 focused host test for the actual InputConnection boundary.
# Compiles the exact production source and test against the real android.jar.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
jar=${ANDROID_JAR:-${DM_ANDROID_JAR:-}}
if [[ -z "$jar" && -n "$sdk" ]]; then
    jar=$sdk/platforms/android-35/android.jar
fi
javac=${JAVAC:-}
if [[ -z "$javac" ]]; then
    javac=$(command -v javac 2>/dev/null || true)
fi
java=${JAVA:-}
if [[ -z "$java" ]]; then
    java=$(command -v java 2>/dev/null || true)
fi

[[ -n "$javac" && -x "$javac" ]] || { echo "javac not found (set JAVA_HOME/JAVAC)" >&2; exit 2; }
[[ -n "$java" && -x "$java" ]] || { echo "java not found (set JAVA/JAVA_HOME)" >&2; exit 2; }
[[ -n "$jar" && -f "$jar" ]] || { echo "android.jar not found: ${jar:-<unset>} (set ANDROID_SDK_ROOT or ANDROID_JAR)" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/dm-ime-boundary.XXXXXX")
trap 'rm -rf "$work"' EXIT

source=${IME_SOURCE:-$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java}
[[ -f "$source" ]] || { echo "IME source not found: $source" >&2; exit 2; }

"$javac" -encoding UTF-8 -cp "$jar" -d "$work/classes" \
    "$source" \
    "$root/dm/android/tests/ime-stubs/DmInputConnectionTest.java"

"$java" -cp "$jar:$work/classes" org.d2rmobile.android.diagnostic.DmInputConnectionTest
