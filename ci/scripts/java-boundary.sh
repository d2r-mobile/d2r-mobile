#!/usr/bin/env bash
# #176 必需 Java 证据：遍历当前 app/java 全部源码对真实 android.jar 编译，并执行已验收的 AudioFocusBoundary
# 主机控制（只替换 android.media/android.os 窄 stub）。缺失 JDK/android.jar 必须显式失败，不 skip。
# #176 mandatory Java evidence: compile every current app/java source against the real android.jar and run the
# accepted AudioFocusBoundary host controls (narrow android.media/android.os stubs only). Missing JDK or android.jar
# fails visibly; there is no skip in this required mode.
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=${DM_CI_WORK:-${RUNNER_TEMP:-/tmp}/dm-ci}
artifacts=$work/artifacts
logs=$work/logs
mkdir -p "$artifacts" "$logs" "$work/tmp"
export TMPDIR=$work/tmp
log=$artifacts/java-boundary.log
: > "$log"

log_line() {
    printf '%s\n' "$*" | tee -a "$log"
}

fail() {
    log_line "FAIL: $*"
    exit 1
}

resolve_javac() {
    local candidate=${DM_JAVAC:-}
    if [[ -z "$candidate" && -n "${JAVA_HOME:-}" ]]; then
        candidate=$JAVA_HOME/bin/javac
    fi
    if [[ -z "$candidate" ]]; then
        candidate=$(command -v javac 2>/dev/null || true)
    fi
    [[ -n "$candidate" && -x "$candidate" ]] || {
        echo "javac not found (set JAVA_HOME or DM_JAVAC)" >&2
        return 1
    }
    printf '%s\n' "$candidate"
}

resolve_jar() {
    local jar=${DM_ANDROID_JAR:-}
    if [[ -z "$jar" ]]; then
        local sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
        [[ -n "$sdk" ]] || {
            echo "ANDROID_SDK_ROOT/ANDROID_HOME is not set; the required Java job needs platforms/android-35" >&2
            return 1
        }
        jar=$sdk/platforms/android-35/android.jar
    fi
    [[ -f "$jar" ]] || {
        echo "android.jar not found: $jar (install platforms;android-35)" >&2
        return 1
    }
    printf '%s\n' "$jar"
}

# 编译给定源码根下的全部 Java；未来新增源自动纳入。| Compile every Java source under the given root.
compile_all() {
    local javac=$1 jar=$2 srcroot=$3 out=$4 logf=$5
    local sources
    sources=$(find "$srcroot" -name '*.java' -type f | sort)
    [[ -n "$sources" ]] || {
        echo "no Java sources under $srcroot" >&2
        return 1
    }
    mkdir -p "$out"
    "$javac" --release 11 -classpath "$jar" -d "$out" $sources >"$logf" 2>&1
    local rc=$?
    printf '%s\n' "$(printf '%s\n' "$sources" | wc -l | tr -d ' ')" > "$out/.source-count"
    return $rc
}

javac=$(resolve_javac) || fail "required Java compile needs a JDK (javac)"
jar=$(resolve_jar) || fail "required Java compile needs the real android.jar"
log_line "javac=$javac"
log_line "android.jar=$jar"

app_java=$root/dm/android/app/java
stubs=$root/dm/android/tests/java-stubs

if compile_all "$javac" "$jar" "$app_java" "$work/java/classes" "$logs/java-real-compile.log"; then
    log_line "PASS real android.jar compile: $(cat "$work/java/classes/.source-count") sources under app/java"
else
    cat "$logs/java-real-compile.log" >> "$log"
    fail "real android.jar compile failed"
fi

# 已验收的主机控制：窄 stub 只替换 Android 边界，不能替代真实 jar 编译。
# Accepted host controls: narrow stubs replace only the Android boundary; they do not substitute the real jar compile.
stub_sources=$(find "$stubs/android" -name '*.java' -type f | sort)
if "$javac" -d "$work/java/stubs" $stub_sources >"$logs/java-stubs.log" 2>&1 \
    && "$javac" -cp "$work/java/stubs" -d "$work/java/host" \
        "$app_java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java" \
        "$stubs/AudioFocusBoundaryTest.java" >"$logs/java-host-compile.log" 2>&1 \
    && java -cp "$work/java/stubs:$work/java/host" AudioFocusBoundaryTest >"$logs/java-host-test.log" 2>&1; then
    log_line "PASS host focus controls: $(tail -1 "$logs/java-host-test.log")"
else
    cat "$logs/java-stubs.log" "$logs/java-host-compile.log" "$logs/java-host-test.log" >> "$log" 2>/dev/null
    fail "accepted host focus controls failed"
fi

# 私有负控制：证明该门禁真的有牙齿，不提交故意损坏的源码。
# Private negative controls: prove the gate has teeth without committing deliberate broken code.
negative_failed=0
if DM_ANDROID_JAR="$work/does-not-exist/android.jar" resolve_jar >/dev/null 2>&1; then
    log_line "FAIL negative control: missing android.jar was accepted"
    negative_failed=1
else
    log_line "PASS negative control: missing android.jar rejected"
fi
if DM_JAVAC="$work/does-not-exist/javac" resolve_javac >/dev/null 2>&1; then
    log_line "FAIL negative control: missing javac was accepted"
    negative_failed=1
else
    log_line "PASS negative control: missing javac rejected"
fi

broken=$(mktemp -d "$work/tmp/java-broken.XXXXXX")
cp -R "$app_java/." "$broken/src/"
printf 'public class BrokenJavaSource { this is not valid Java }\n' > "$broken/src/BrokenJavaSource.java"
if compile_all "$javac" "$jar" "$broken/src" "$broken/classes" "$broken/compile.log"; then
    log_line "FAIL negative control: malformed source compiled"
    negative_failed=1
elif grep -q "BrokenJavaSource.java" "$broken/compile.log"; then
    log_line "PASS negative control: malformed source rejected at BrokenJavaSource.java"
else
    log_line "FAIL negative control: malformed source failed without naming the broken file"
    negative_failed=1
fi
rm -rf "$broken"

# 发布前清洗本机/工作区路径。| Scrub checkout/work paths before publishing evidence.
python3 "$root/ci/scripts/sanitize_log.py" "$log" "$root" "$work" >/dev/null 2>&1 || {
    log_line "FAIL: evidence sanitisation failed"
    exit 1
}

[[ "$negative_failed" == 0 ]] || fail "one or more negative controls did not fail as intended"
log_line "PASS required Java compile and focus host controls"
