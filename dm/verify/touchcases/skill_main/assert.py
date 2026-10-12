import os, json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib

# 主技能 = 原版施放包：对地点 0x05、对单位 0x06（真机 e2e/s6：05 ×16、06 ×6，有怪在射程内才出 06）。
# 只看前缀回放结束之后（前缀里的包是回放注入的，不算触屏产生）；末拍取本用例 prefix 文件声明的前缀
# Main skill = the original game's cast packets: 0x05 on a location, 0x06 on a unit (real device e2e/s6: 05 ×16, 06 ×6; 06 only appears when a monster is in range).
# Only look after the prefix replay ends (packets in the prefix are injected by the replay and do not count as touch-generated); the end tick is taken from the prefix declared in this case's prefix file
prefix = Path(os.environ["DM_TOUCH_PREFIX"])   # touchchain.sh 已把 prefix 文件里相对工作区根的路径拼成绝对路径 | touchchain.sh has already joined the workspace-root-relative path from the prefix file into an absolute path
last = max(json.loads(l)["tick"] for l in open(prefix / "c2s.jsonl") if l.strip())
r = lib.load(sys.argv[1])
lib.check(any(op in (0x05, 0x06) for _, op, _ in r.c2s_ops(t0=last + 1)), f"前缀末拍 {last} 之后没有主技能施放包 0x05 / 0x06")
sys.exit(lib.main())
