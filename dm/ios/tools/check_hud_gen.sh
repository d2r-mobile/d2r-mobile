#!/bin/bash
# 原版衍生物不入库（凡由原版表或图生成的都不进仓库，构建时从玩家自己的游戏文件生成，见 dm/README.md「从你的游戏文件生成资源」）：
# ① git 里不许再出现这些生成物（被人手动加回来就红）；② 本机有游戏文件时把底部 UI v4 实际生成一遍，跑不通就红（生成链最长的一条）。
# 用法：check_hud_gen.sh        退出 0 = 通过或跳过 ②，1 = 有生成物入库 / 生成失败
# Derivatives of the original game are not checked in (anything generated from the original game's tables or images stays out of the repository and is generated at build time from the player's own game files, see dm/README.md 「从你的游戏文件生成资源」(Generate assets from your game files)):
# ① these generated files must not reappear in git (fails if someone manually adds them back); ② when the local machine has game files, actually generate bottom UI v4 once, and fail if that fails (the longest generation chain).
# Usage: check_hud_gen.sh        exit 0 = passed or ② skipped, 1 = generated files checked in / generation failed
set -uo pipefail
I="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; W="$(cd "$I/../.." && pwd)"
BAD=$(git -C "$W" ls-files -- dm/ios/NativeHUD/v4 dm/ios/NativeHUD/mapgoal dm/ios/NativeHUD/widestash dm/ios/NativeHUD/pouch dm/ios/NativeHUD/touch/fe_rightpanel.* \
      'dm/ios/Sources/TouchArt/dm_native_*' dm/ios/Sources/dm_touch_native_art.c dm/host/dm_vault_tab.h dm/host/dm_hud_exp.h dm/host/dm_bright_levels.inc dm/ios/tools/touchrectv1_HUDPanelHD.json dm/ios/tools/native_art/skills.txt dm/ios/tools/native_art/skilldesc.txt)
[ -z "$BAD" ] || { echo "HUDGEN 红：原版衍生的生成物进了仓库（应该构建时从游戏文件生成）："; echo "$BAD" | sed 's/^/  /'; exit 1; }
R=${DM_ROMFS_SRC:-${DM_ROOT:-$HOME/dm-work}/data/switch/3.3.94012/Update/RomFS}
[ -f "$R/data/build_manifest.psv" ] || { echo "HUDGEN OK（入库检查）；生成检查跳过：这台机器没有游戏文件 $R"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 "$I/tools/native_art/generate.py" "$R" > "$T.art.log" 2>&1 || { echo "HUDGEN 红：技能图集生成不通（底栏要用它）"; tail -3 "$T.art.log"; exit 1; }   # 底栏的技能图标取自技能图集（Sources/TouchArt，git 忽略）；干净的树里没有，先生成 | The bottom bar's skill icons come from the skill atlas (Sources/TouchArt, git-ignored); a clean tree has none, so generate it first
python3 "$I/tools/gen_hud_v4.py" "$R" "$T" > "$T.log" 2>&1 || { echo "HUDGEN 红：gen_hud_v4.py 跑不通"; tail -3 "$T.log"; exit 1; }
n=$(find "$T" -type f | wc -l | tr -d ' '); [ "$n" -ge 79 ] || { echo "HUDGEN 红：底部 UI v4 只生成了 $n 个文件（应 79）"; exit 1; }
echo "HUDGEN OK：生成物没有入库；底部 UI v4 从游戏文件生成 $n 个文件$(grep -q '^没找到 PC' "$T.log" && echo '（Switch 素材放大版）')"
