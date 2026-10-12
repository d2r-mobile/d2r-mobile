#!/usr/bin/env python3
# 「重场面」触点脚本：亡者大殿二层（第二幕，区域号 57，levels.txt 怪物密度最高一档 2200，Lv76 角色 joe 的存档自带这个小站）。真机 touch_e2e.sh / Mac touch_e2e_mac.sh 通用。
# 用法：gen_heavy.py <总秒数> [到达后开始秒=17] > 脚本.txt，配套环境见同目录 heavy.env.sh（小站回放 + 键序传送 + 起局人数档 8）。
# 动作：先推摇杆离开小站（站在小站上按攻击键是「再开小站页」），之后摇杆每 4 秒左右换向来回走（边走边拉怪），攻击键每 0.5 秒点一下。
# 证据：perf.txt 场景列「区域57」、e2e_state.jsonl 的怪物表计数、boot.txt 的「每帧绘制」。
# "Heavy scene" touch point script: Halls of the Dead Level 2 (Act 2, area number 57, the highest monster density tier 2200 in levels.txt, the save of the Lv76 character joe already has this waypoint). Works for both real device touch_e2e.sh / Mac touch_e2e_mac.sh.
# Usage: gen_heavy.py <total seconds> [start second after arrival=17] > script.txt, the matching environment is in heavy.env.sh in the same directory (waypoint replay + key-sequence teleport + run-start /players setting 8).
# Actions: first push the stick to leave the waypoint (pressing the attack key while standing on the waypoint is "open the waypoint page again"), after that the stick changes direction about every 4 seconds, walking back and forth (pulling monsters while walking), and the attack key is tapped every 0.5 seconds.
# Evidence: 「区域57」(area 57) in the scene column of perf.txt, the monster table count in e2e_state.jsonl, 「每帧绘制」(draws per frame) in boot.txt.
import sys
total = float(sys.argv[1]); t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 17.0
ev = [(t0, "down 0 @joy"), (t0 + 0.1, "move 0 @joy+40,0")]
t = t0 + 2.0; left = True
while t < total - 1.0:
    ev.append((t, f"move 0 @joy+{-40 if left else 40},0")); left = not left; t += 4.0
ev.append((total - 0.5, "up 0"))
t = t0 + 2.2
while t < total - 1.0:
    ev.append((t, "down 1 @main")); ev.append((t + 0.15, "up 1")); t += 0.5
for t, s in sorted(ev, key=lambda e: e[0]): print(f"{t:.2f} {s}")
