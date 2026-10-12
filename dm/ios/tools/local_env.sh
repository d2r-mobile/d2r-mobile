# 本机设备配置（不进仓）。被各脚本 source：按序找 $DM_LOCAL_ENV → dm/ios/local.env → ~/.config/dm-ios/local.env，读第一个存在的
# （后一个位置供同一台机器上的多个工作树共用）。样例与各项含义见 dm/ios/local.env.example。
# local_env_need <变量…>：缺哪项就报哪项并返回非零。
# Local device configuration (not committed to the repo). Sourced by the scripts: looks in order at $DM_LOCAL_ENV → dm/ios/local.env → ~/.config/dm-ios/local.env and reads the first that exists
# (the last location lets multiple worktrees on the same machine share it). For a sample and the meaning of each item see dm/ios/local.env.example.
# local_env_need <variables...>: reports whichever items are missing and returns nonzero.
_le_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
for _le_f in "${DM_LOCAL_ENV:-}" "$_le_dir/local.env" "$HOME/.config/dm-ios/local.env"; do
  [ -n "$_le_f" ] && [ -f "$_le_f" ] && { set -a; source "$_le_f"; set +a; DM_LOCAL_ENV_FILE=$_le_f; break; }
done
local_env_need() {
  local v miss=
  for v in "$@"; do [ -n "${!v:-}" ] || miss="$miss $v"; done
  [ -z "$miss" ] || { echo "🔴 缺本机配置：$miss（把 dm/ios/local.env.example 复制成 dm/ios/local.env 填好；本次读的是：${DM_LOCAL_ENV_FILE:-没找到配置文件}）" >&2; return 1; }
}
