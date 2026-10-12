#!/usr/bin/env python3
# 生成「进野外后向左走 + 连续攻击」的触点脚本（真机 touch_e2e.sh / Mac touch_e2e_mac.sh 通用，只用 @名字 目标，不依赖屏幕尺寸）。
# 用法：gen_wild.py <总秒数> [起始秒=18] > 脚本.txt；配 PREFIX=<wild_prefix_original_warlock>/c2s.jsonl（前缀回放约 16 s 把角色带到营地门外、区域 2 鲜血荒地）。
# 摇杆（触点 0）全程向左推满；攻击键（触点 1）每 0.6 秒点一下（按 0.15 秒）。证据：perf.txt 场景列的区域号 / 坐标在变，dm_hud 行的经验 ‰ 在涨。
# 出处：在 Mac 上试出的走法（进鲜血荒地打到沉沦魔，经验 0 → 108‰）。
# Generates the touch point script for "walk left after entering the wilderness + continuous attack" (works for both real device touch_e2e.sh / Mac touch_e2e_mac.sh, uses only @name targets, independent of screen size).
# Usage: gen_wild.py <total seconds> [start second=18] > script.txt; pair with PREFIX=<wild_prefix_original_warlock>/c2s.jsonl (the prefix replay takes about 16 s to bring the character outside the camp gate, area 2 Blood Moor).
# The stick (touch point 0) is pushed fully left the whole time; the attack key (touch point 1) is tapped every 0.6 seconds (held 0.15 seconds). Evidence: the area number / coordinates in the scene column of perf.txt are changing, and the experience ‰ on the dm_hud line is rising.
# Source: a route worked out by trial on Mac (entering the Blood Moor and fighting Fallen, experience 0 → 108‰).
import sys
total = float(sys.argv[1]); t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 18.0
print(f"{t0:.2f} down 0 @joy"); print(f"{t0 + 0.1:.2f} move 0 @joy+-40,0")
t = t0 + 0.5
while t < total - 1.0:
    print(f"{t:.2f} down 1 @main"); print(f"{t + 0.15:.2f} up 1"); t += 0.6
print(f"{total - 0.5:.2f} up 0")
