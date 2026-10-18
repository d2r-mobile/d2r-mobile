#!/usr/bin/env bash
# Android 工作区缺省与仓库并列，编译临时文件也放在工作区；避免使用系统盘缓存。
# Default Android workspace sits beside the repository; compiler temporary files also stay in the workspace to avoid system-disk caches.
dm_android_parent=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
export DM_ROOT=${DM_ROOT:-$dm_android_parent/d2r-work}
export TMPDIR=${DM_ANDROID_TMPDIR:-$DM_ROOT/tmp}
