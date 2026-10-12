#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib
r = lib.load(sys.argv[1])
rows = [(t, op) for t, op, _ in r.c2s_ops()]
ops = [op for _, op in rows]
# 点到目标（NPC / 物件 / 物品，dm_world.c 带约 7 毫米余量）会走过去交互，那是允许的：带单位的包 0x02 / 0x04 / 0x13 / 0x16 / 0x2f / 0x40 / 0x41。
# 不许的是「没有目标的点地移动」：0x01 / 0x03 前后 40 拍内必须有带单位的包，否则就是点空地走开了。
# Tapping a target (NPC / object / item; dm_world.c has a margin of about 7 mm) walks over and interacts, which is allowed: packets with a unit 0x02 / 0x04 / 0x13 / 0x16 / 0x2f / 0x40 / 0x41.
# What is not allowed is "tap-ground movement without a target": within 40 ticks before or after 0x01 / 0x03 there must be a packet with a unit, otherwise it was a tap on empty ground that walked away.
TARGET = {0x02, 0x04, 0x13, 0x16, 0x2f, 0x40, 0x41}
stray = [(t, op) for t, op in rows if op in (0x01, 0x03) and not any(o in TARGET and abs(t2 - t) <= 40 for t2, o in rows)]
lib.check(not stray, "没有无目标的点地移动包（0x01 / 0x03 都伴随带单位的交互包）：%s" % stray[:3])
lib.check(0x42 not in ops, "没有摇杆移动包（0x42）")
ev = r._panel_events
opened = [t for t, a, p in ev if a == "OpenPanel" and p == "PlayerInventoryExpansionLayout"]
closed = [t for t, a, p in ev if a == "ClosePanel" and p == "PlayerInventoryExpansionLayout" and opened and t > opened[0]]
lib.check(bool(opened), "功能键开物品栏")
lib.check(bool(closed), "点面板外后物品栏关闭")
sys.exit(lib.main())
