#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib

r = lib.load(sys.argv[1])
ticks = sorted(r._state_by_tick)
initial = r.items_at(min(ticks))
source = [item for item in initial.values() if item.get("x") == 5 and item.get("y") == 3]
lib.check(len(source) == 1, "初始 (5,3) 目标物品唯一")
if source:
    item_id = source[0]["id"]
    final = r.items_at(ticks[-1]).get(item_id, {})
    lib.check(final.get("x") == 5 and final.get("y") == 3 and final.get("mode") == 0, "拖回后物品仍在 (5,3)、mode=0")
    lib.check(r.cursor_at(ticks[-1]) is None, "拖回最终 cursor 为空")
    lib.check(all(item_id == r.cursor_at(t) for t in ticks if r.cursor_at(t) is not None), "拖回全程没有拿起第二件物品")
ops = r.c2s_ops()
lib.check(any(op == 0x19 for _, op, _ in ops), "c2s 含 0x19 拿起")
lib.check(any(op == 0x18 for _, op, _ in ops), "c2s 含 0x18 放下")
sys.exit(lib.main())
