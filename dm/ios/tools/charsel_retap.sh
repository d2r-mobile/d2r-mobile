#!/bin/bash
# 选角页回归（Mac 上的 iOS 包，真实触点路径）：四行各「点一次 → 再点两次 → 进入游戏」，核进的是该行的角色、期间没有发 CharacterSelect:SelectCharacter。
# 来由：换成键鼠版选角页后，再点已高亮的行会发「确认选择」、列表收起重开后高亮换了人，「进入游戏」进错角色。
# 认角色用进局后读到的等级：标准存档 save_ref_warlock 里四个角色等级各不相同（列表顺序 joe 76 / Warlock 1 / joebao 9 / 法师 88）；换存档要改 WANT。
# 用法：charsel_retap.sh <结果目录>。先跑过 smoke_mac.sh（要有包好的 .iosdd_mac/DmMacSmoke.app）。全过返回 0。
# Character select page regression (iOS app on Mac, real touch point path): for each of the four rows, "tap once → tap twice more → enter game", check that the character entered is that row's and that no CharacterSelect:SelectCharacter was sent in between.
# Background: after switching to the keyboard-and-mouse character select page, tapping an already highlighted row again sends 「确认选择」(confirm selection), the list collapses and reopens with the highlight on someone else, and 「进入游戏」(Play) enters the wrong character.
# Characters are identified by the level read after entering the run: the four characters in the standard save save_ref_warlock all have different levels (list order joe 76 / Warlock 1 / joebao 9 / 「法师」(Mage) 88); change WANT when changing the save.
# Usage: charsel_retap.sh <result dir>. Run smoke_mac.sh first (the packaged .iosdd_mac/DmMacSmoke.app must exist). Returns 0 if all pass.
set -u
OUT=${1:?用法：charsel_retap.sh <结果目录>}; TOOLS=$(cd "$(dirname "$0")" && pwd); mkdir -p "$OUT"; WANT=(76 1 9 88); YS=(47 92 128 165); bad=0
for r in 0 1 2 3; do y=${YS[$r]}
  printf '4.0 down 0 803,%s\n4.1 up 0\n5.8 down 0 803,%s\n5.9 up 0\n7.6 down 0 803,%s\n7.7 up 0\n10.0 down 0 478,395\n10.1 up 0\n12.0 down 0 478,163\n12.1 up 0\n' $y $y $y > "$OUT/t$r.txt"
  E2E_REF=956x440 E2E_KEYS='B@OpenPanel:NoSuchPanelEver' E2E_ENV='{"DM_TOUCH_SCRIPT_START":"menu","DM_FE_AUTO":"1"}' bash "$TOOLS/touch_e2e_mac.sh" "$OUT/t$r.txt" "$OUT/r$r" 26 > "$OUT/r$r.out" 2>&1
  sel=$(LC_ALL=C grep -a -c 'UIMSG.*CharacterSelect SelectCharacter' "$OUT/r$r/boot.txt" 2>/dev/null); lv=$(LC_ALL=C grep -a 'dm_hud' "$OUT/r$r/boot.txt" 2>/dev/null | LC_ALL=C grep -a -o 'Lv [0-9]*' | awk '$2>0{print $2; exit}')
  if [ "${sel:-1}" = 0 ] && [ "${lv:-}" = "${WANT[$r]}" ]; then echo "第 $((r+1)) 行 OK：进局等级 ${lv}，SelectCharacter 0 条"; else echo "【红】第 $((r+1)) 行：进局等级「${lv:-没进局}」（应为 ${WANT[$r]}），SelectCharacter ${sel:-?} 条"; bad=1; fi
done
[ $bad = 0 ] && echo "CHARSEL-RETAP OK 4/4" || echo "CHARSEL-RETAP RED"; exit $bad
