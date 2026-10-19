#!/usr/bin/env bash
# Focused host control for ControllerHotplugBoundary. Only Android
# InputManager/Activity/Handler/Looper boundaries are replaced by stubs.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
sdk=${ANDROID_SDK_ROOT:-/Volumes/T2/Projects/d2r-work/tools/android/sdk}
jar=${ANDROID_JAR:-"$sdk/platforms/android-35/android.jar"}
work=$(mktemp -d "${TMPDIR:-/tmp}/controller-hotplug-java.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
mkdir -p "$work/stubs" "$work/host" "$work/real"
javac -d "$work/stubs" $(find "$here/java-stubs/android" -name '*.java' -type f | sort)
javac -cp "$work/stubs" -d "$work/host" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/ControllerHotplugBoundary.java" \
    "$here/java-stubs/ControllerHotplugBoundaryTest.java"
java -cp "$work/stubs:$work/host" ControllerHotplugBoundaryTest
if [[ -f "$jar" ]]; then
    javac --release 11 -cp "$jar" -d "$work/real" \
        $(find "$root/dm/android/app/java" -name '*.java' -type f | sort)
    echo "real android.jar compile: ok"
else
    echo "real android.jar compile skipped: $jar not found" >&2
    exit 1
fi
