import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib

r = lib.load(sys.argv[1])
rows = r.state_rows()
lib.check(any(opcode == 0x26 for _, opcode, _ in r.c2s_ops()), "没有喝药包 0x26")
before = {(item.get("x"), item.get("id")) for item in rows[0].get("items", []) if item.get("mode") == 2} if rows else set()
after = {(item.get("x"), item.get("id")) for item in rows[-1].get("items", []) if item.get("mode") == 2} if rows else set()
lib.check(before != after, "腰带物品集合没有变化")
gone = sorted(x for x, _ in before - after)   # 脚本点的是底栏第 3 格 = 腰带第 3 列（格位 x = 2）：喝掉的必须是这一列的（第 3、4 列的键写反过，点第 3 格喝了第 4 列） | The script taps bottom bar slot 3 = belt column 3 (slot x = 2): what gets drunk must be from this column (the keys for columns 3 and 4 were once swapped, and tapping slot 3 drank from column 4)
lib.check(gone == [2], f"喝掉的不是第 3 列：消失的格位 {gone}")
sys.exit(lib.main())
