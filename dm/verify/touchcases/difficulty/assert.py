import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib

r = lib.load(sys.argv[1])
seen = [row.get("players") for row in r.state_rows()]
lib.check(seen and seen[0] == 1, f"起局人数档应为 1，实际 {seen[:1]}")
lib.check(seen and seen[-1] == 8, f"选第 8 格后人数档应为 8，实际 {seen[-1:]}")
lib.check(not any(op == 0x30 for _, op, _ in r.c2s_ops()), "点难度格不应打开功能页（0x30）")
sys.exit(lib.main())
