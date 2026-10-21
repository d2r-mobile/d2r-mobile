#!/usr/bin/env bash
# #176 私有控制：SDK 安装分支只在 consumer 成功且最终 jar 存在时通过；consumer 失败或 jar 缺失必须失败。
# 使用本地窄 sdkmanager stub，不下载 SDK、不访问网络。
# #176 private controls: the SDK-install branch passes only when the consumer succeeds and the final jar exists;
# consumer failure or a missing jar must fail. Uses narrow local sdkmanager stubs; no SDK download or network.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
script=$root/ci/scripts/android-platform.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-sdk-branch.XXXXXX")
trap 'rm -rf "$work"' EXIT
checks=0
failures=0

check() {
    checks=$((checks + 1))
    if ! "$@"; then
        failures=$((failures + 1))
        echo "FAIL: $*" >&2
    fi
}

write_stub() {
    local path=$1 mode=$2
    {
        printf '#!/usr/bin/env bash\n'
        printf 'set -uo pipefail\n'
        case "$mode" in
            success)
                printf 'read -r _answer || true\n'
                printf 'mkdir -p "$DM_ANDROID_SDK_ROOT/platforms/android-35"\n'
                printf 'printf jar > "$DM_ANDROID_SDK_ROOT/platforms/android-35/android.jar"\n'
                printf 'exit 0\n'
                ;;
            fail)
                printf 'echo "stub sdkmanager failure" >&2\n'
                printf 'exit 7\n'
                ;;
            success_no_jar)
                printf 'exit 0\n'
                ;;
        esac
    } > "$path"
    chmod +x "$path"
}

run_case() {
    local sdk=$1 stub=$2
    DM_ANDROID_SDK_ROOT="$sdk" DM_SDKMANAGER="$stub" DM_CI_WORK="$work/ci" \
        bash "$script" >"$work/stdout.log" 2>"$work/stderr.log"
}

# 1. jar 已存在：即使 sdkmanager 不可用也必须成功，且不调用安装。
mkdir -p "$work/present/platforms/android-35"
printf jar > "$work/present/platforms/android-35/android.jar"
if run_case "$work/present" "$work/does-not-exist-sdkmanager"; then
    check true
else
    echo "FAIL: existing jar should pass without sdkmanager" >&2
    failures=$((failures + 1)); checks=$((checks + 1))
fi

# 2. consumer 成功且创建 jar：必须通过。stub 读取一行后立即退出，复现旧 yes| 管道的 SIGPIPE 场景。
mkdir -p "$work/success"
write_stub "$work/stub-success" success
if run_case "$work/success" "$work/stub-success"; then
    check test -f "$work/success/platforms/android-35/android.jar"
else
    echo "FAIL: successful consumer should pass ($(tail -1 "$work/stderr.log"))" >&2
    failures=$((failures + 1)); checks=$((checks + 1))
fi

# 3. consumer 真正失败：必须非零，并保留失败原因，不能被吞掉。
mkdir -p "$work/failure"
write_stub "$work/stub-fail" fail
if run_case "$work/failure" "$work/stub-fail"; then
    echo "FAIL: failing consumer must fail the branch" >&2
    failures=$((failures + 1))
else
    rc=$?
    check test "$rc" -eq 7
    check grep -q "sdkmanager failed (7)" "$work/stderr.log"
fi

# 4. consumer 成功但 jar 仍缺失：必须失败且明确指出缺失。
mkdir -p "$work/nojar"
write_stub "$work/stub-nojar" success_no_jar
if run_case "$work/nojar" "$work/stub-nojar"; then
    echo "FAIL: successful consumer without a jar must fail" >&2
    failures=$((failures + 1))
else
    check grep -q "reported success but android.jar is missing" "$work/stderr.log"
fi

echo "java-ci-sdk-branch: $checks checks, $failures failures"
[[ "$failures" == 0 ]]
