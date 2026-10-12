#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1]))
import lib
r = lib.load(sys.argv[1])
ops = [op for _, op, _ in r.c2s_ops()]
lib.check(0x19 in ops, "c2s 含 0x19 拿起")
lib.check(0x17 in ops, "c2s 含 0x17 丢到地上")
lib.check(0x19 in ops and 0x17 in ops and ops.index(0x19) < ops.index(0x17), "0x19 先于 0x17")
lib.check(0x18 not in ops, "没有放回格子（0x18）")
rows = r.state_rows() if callable(getattr(r, "state_rows", None)) else []
sys.exit(lib.main())
