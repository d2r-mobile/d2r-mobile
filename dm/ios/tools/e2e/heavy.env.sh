# 重场面起局环境（source 后再调 touch_e2e.sh / touch_e2e_mac.sh）：小站回放把 joe 带到营地小站 → 拍 135 开小站页 → ZR 切第二幕 → 下 ×3 → A 传到亡者大殿二层；起局人数档 8（怪更耐打，场面不会越打越轻）
# Heavy scene run-start environment (source it, then call touch_e2e.sh / touch_e2e_mac.sh): the waypoint replay brings joe to the camp waypoint → at tick 135 open the waypoint page → ZR switches to Act 2 → down ×3 → A teleports to Halls of the Dead Level 2; run-start /players setting 8 (monsters take more hits, so the scene does not get lighter as the fight goes on)
export PREFIX=${DM_ROOT:-$HOME/dm-work}/data/switch/truth_runs/wp1/c2s_to_E.jsonl
export E2E_KEYS='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,MSG:CharacterSelect:LoadCharacter:0@OpenPanel:CharacterSelectPanel%1>FrontEndNavigation:ToGame,A@DM:Tick:135#>OpenPanel:WaypointsPanelExpansion,ZR@OpenPanel:WaypointsPanelExpansion%1,DOWN@%0.5,DOWN@%0.5,DOWN@%0.5,A@%0.6'
export E2E_ENV='{"DM_FIXED_TIME":"1790000000","DM_PLAYERS":"8"}'
