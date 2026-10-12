#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib
r = lib.load(sys.argv[1])
opens = [(t, p) for t, a, p in r._panel_events if a in ("OpenPanel", "OpenExclusivePanel")]
inv = [t for t, p in opens if p == "PlayerInventoryExpansionLayout"]
skill = [t for t, p in opens if p == "SkillsTreePanel"]
quest = [t for t, p in opens if p == "QuestLogPanelExpansion"]
lib.check(bool(inv), "功能键开物品栏")
lib.check(bool(skill) and bool(inv) and skill[0] > inv[0], "点「技能树」页签后技能树打开")
lib.check(bool(quest) and bool(skill) and quest[-1] > skill[0], "再点「任务日志」页签后任务日志打开")
lib.check(not any(op == 0x19 for _, op, _ in r.c2s_ops()), "全程不拿起物品")
sys.exit(lib.main())
