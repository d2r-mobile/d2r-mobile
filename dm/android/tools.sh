#!/usr/bin/env bash
# 在 T2 工作区的工具环境中执行命令，供现有改写/解包脚本复用，不自动读取游戏或密钥。
# Execute commands in the T2 workspace tool environment for existing rewrite / extraction scripts; never automatically read games or keys.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/paths.sh"
if [[ $# == 0 ]]; then echo 'Usage: tools.sh <command> [arguments...]' >&2; exit 2; fi
export DM_PY=${DM_PY:-$DM_ROOT/build/cache/dm/venv/bin/python}
export HACTOOL=${HACTOOL:-$DM_ROOT/tools/hactool/hactool}
export PATH="$(dirname "$DM_PY"):$(dirname "$HACTOOL"):$PATH"
mkdir -p "$TMPDIR"
exec "$@"
