#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib
r = lib.load(sys.argv[1])
first_tick = min(r._state_by_tick)
initial = r.items_at(first_tick)
targets = [item for item in initial.values() if item.get("x") == 5 and item.get("y") == 3]
lib.check(len(targets) == 1, "初始 (5,3) 目标物品唯一")
if targets:
    item_id = targets[0]["id"]
    rows = [r.items_at(t).get(item_id) for t in sorted(r._state_by_tick)]
    lib.check(all(row and row.get("x") == 5 and row.get("y") == 3 for row in rows), "长按期间目标物品始终在 (5,3)")
lib.check(all(r.cursor_at(t) is None for t in sorted(r._state_by_tick)), "长按全程 cursor 为空")
lib.check(not any(op == 0x19 for _, op, _ in r.c2s_ops()), "长按不发 0x19")
sys.exit(lib.main())
