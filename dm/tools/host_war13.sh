#!/bin/bash
# 冻结用例war_summon13的宿主回放：服务器命令+客户端真实Y绑定，严格逐拍/存档对拍。
# 用法：bash dm/tools/host_war13.sh <war_summon13真值目录> [产物目录=r17]
# 这是固定案例输入，不是通用计划翻译器；2558开技能树、2670学会后2683绑定，E从events.jsonl取，只冻结存盘退出。
# Host replay of the frozen case war_summon13: server commands + the client's real Y binding, strict tick-by-tick / save comparison.
# Usage: bash dm/tools/host_war13.sh <war_summon13 ground-truth dir> [build output dir=r17]
# This is a fixed case input, not a general plan translator; the skill tree opens at 2558, the skill is learned at 2670 and then bound at 2683, E is taken from events.jsonl, and only save-and-exit is frozen.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TR=${1:?war_summon13真值目录}; OUT=${2:-$(bash "$(dirname "$0")/product.sh" path keypage)}
PY=${DM_ROOT:-$HOME/dm-work}/build/cache/dm/venv/bin/python
export LC_ALL=C HSC_WALL=${HSC_WALL:-180}
# 真值未固定墙钟；宿主固定在真值记录起点，savecmp以meta中的来源区间核验，不掩盖其它差异。
# The ground truth did not fix the wall clock; the host is fixed at the start of the ground-truth recording, and savecmp verifies against the source interval in meta without masking other differences.
FT=$($PY -c 'import json,sys; m=json.load(open(sys.argv[1])); t=m.get("fix_time"); print(int(t if t is not None else m["wall_start"]))' "$TR/meta.json") || exit 2
export FT
export KEYS_FRONT='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,DOWN@OpenPanel:CharacterSelectPanel>UpdateCharacterInfo,A@UpdateCharacterInfo%1>CharacterSelect:SelectCharacter,A@CharacterSelect:SelectCharacter>FrontEndNavigation:ToGame,PLUS@DM:Tick:2558#>OpenPanel:SkillsTreePanel,ZR@OpenPanel:SkillsTreePanel%0.5,ZR@%0.3,RIGHT@%0.3,UP@%0.3,UP@%0.3,Y@DM:Tick:2683#>OpenPanel:ButtonBindingModal,Y@OpenPanel:ButtonBindingModal%0.5>ClosePanel:ButtonBindingModal,B@ClosePanel:ButtonBindingModal%0.5>ClosePanel:SkillsTreePanel'
unset KEYS_EXIT
# 不向结果目录复制ctl、不豁免客户端文件；由游戏原绑定/原保存生成，host_savecmp比较全部文件。
# Does not copy ctl into the result directory and does not exempt client files; they are produced by the game's original binding / original save, and host_savecmp compares all files.
exec bash "$HERE/host_savecmp.sh" "$TR" "$OUT"
