#!/usr/bin/env python3
import json
import sys
from pathlib import Path

run = Path(sys.argv[1])

def fail(message):
    print(f"断言失败：{message}", file=sys.stderr)
    raise SystemExit(1)

c2s = [json.loads(line) for line in (run / "c2s.jsonl").open() if line.strip()]
if not any(row.get("q") != "sys" and row.get("pkt", "").startswith("42") for row in c2s):
    fail("没有移动包")

xs = []
for line in (run / "ticks.jsonl").open():
    row = json.loads(line)
    unit = row.get("u")
    if unit and unit.get("type") == 0 and unit.get("id") == 1:
        xs.append((row["t"], unit["x"]))
if len(xs) < 2 or xs[-1][1] <= xs[0][1]:
    fail("x 没增大")
print(f"触屏移动断言通过：移动包存在，x {xs[0][1]}→{xs[-1][1]}；DM_TICKS 射程={len(xs)} 条单位收样")
