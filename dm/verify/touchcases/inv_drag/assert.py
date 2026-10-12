#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib

r = lib.load(sys.argv[1])
ticks = sorted(r._state_by_tick)
first_tick = min(ticks)
initial = r.items_at(first_tick)
source = [item for item in initial.values() if item.get("x") == 5 and item.get("y") == 3]
target = [item for item in initial.values() if item.get("x") == 6 and item.get("y") == 3]
lib.check(len(source) == 1, "初始 (5,3) 目标物品唯一")
lib.check(not target, "初始 (6,3) 目标格为空")
if source:
    item_id = source[0]["id"]
    picked = [t for t in ticks if r.items_at(t).get(item_id, {}).get("mode") == 4 and r.cursor_at(t) == item_id]
    placed = [t for t in ticks if r.items_at(t).get(item_id, {}).get("x") == 6 and r.items_at(t).get(item_id, {}).get("y") == 3 and r.items_at(t).get(item_id, {}).get("mode") == 0 and r.cursor_at(t) is None]
    lib.check(bool(picked), "某拍 cursor 指向目标物品且 mode=4")
    lib.check(bool(placed), "最终目标物品在 (6,3)、mode=0 且 cursor 清空")
    lib.check(all(item_id == r.cursor_at(t) for t in ticks if r.cursor_at(t) is not None), "全程没有拿起第二件物品")
    lib.check(all(item.get("id") == item_id or item.get("mode") != 4 for t in ticks for item in r.items_at(t).values()), "全程没有误触发第二件物品 mode=4")
ops = r.c2s_ops()
lib.check(any(op == 0x19 for _, op, _ in ops), "c2s 含 0x19 拿起")
lib.check(any(op == 0x18 for _, op, _ in ops), "c2s 含 0x18 放下")
lib.check(next((t for t, op, _ in ops if op == 0x19), 1 << 60) < next((t for t, op, _ in ops if op == 0x18), 0), "0x19 先于 0x18")
sys.exit(lib.main())
