#!/usr/bin/env bash
# #176 必需 CI 的 SDK 分支：保证 platforms/android-35/android.jar 存在。
# 许可确认通过文件重定向喂给 sdkmanager，不用 `yes |` 管道，因此 sdkmanager 未读完 stdin 正常退出时不会产生
# SIGPIPE(141) 误报；只有 sdkmanager 真正失败或安装后 jar 仍缺失才失败，绝不用 || true 吞掉失败。
# #176 required-CI SDK branch: ensure platforms/android-35/android.jar exists. Licence answers are fed from a file
# instead of a `yes |` pipeline, so a successful sdkmanager exit that does not drain stdin cannot surface as a
# SIGPIPE(141) failure. Only a real sdkmanager failure or a still-missing jar fails; failures are never hidden with
# `|| true`.
set -uo pipefail

sdk=${DM_ANDROID_SDK_ROOT:-${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/local/lib/android/sdk}}}
jar=$sdk/platforms/android-35/android.jar
work=${DM_CI_WORK:-${RUNNER_TEMP:-/tmp}/dm-ci}
log=$work/logs/android-platform.log
mkdir -p "$(dirname "$log")"

if [[ -f "$jar" ]]; then
    echo "android.jar already present: $jar"
    exit 0
fi

sdkmanager=${DM_SDKMANAGER:-}
if [[ -z "$sdkmanager" ]]; then
    sdkmanager=$(command -v sdkmanager 2>/dev/null || true)
fi
if [[ -z "$sdkmanager" ]]; then
    sdkmanager=$(find "$sdk" -path '*/cmdline-tools/*/bin/sdkmanager' -type f 2>/dev/null | head -1)
fi
if [[ -z "$sdkmanager" || ! -x "$sdkmanager" ]]; then
    echo "sdkmanager not found; cannot install platforms;android-35" >&2
    exit 1
fi

licences=$(mktemp "${TMPDIR:-/tmp}/dm-sdk-licences.XXXXXX")
trap 'rm -f "$licences"' EXIT
for _ in $(seq 1 100); do printf 'y\n'; done > "$licences"

# stdin 来自文件而不是管道：consumer 提前退出不会触发 producer SIGPIPE。
# stdin comes from a file rather than a pipe: an early consumer exit cannot trigger a producer SIGPIPE.
"$sdkmanager" --sdk_root="$sdk" "platforms;android-35" < "$licences" > "$log" 2>&1
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "sdkmanager failed ($rc) installing platforms;android-35" >&2
    tail -20 "$log" >&2 2>/dev/null || true
    exit "$rc"
fi
if [[ ! -f "$jar" ]]; then
    echo "sdkmanager reported success but android.jar is missing: $jar" >&2
    exit 1
fi
echo "android.jar installed: $jar"
