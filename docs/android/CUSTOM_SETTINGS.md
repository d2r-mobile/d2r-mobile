# Android custom settings audit / 自定义设置接线检查

Checked on 2026-10-22 against upstream main `source snapshot`.
This is a production-code wiring audit, not a claim that every gameplay condition was tested on a phone.
本表检查正式运行链路，不代表每个职业、难度与掉落条件都已实机验收。

The playable APK links `host/*.c` through `dm/android/tools/host_link.py`. Its real settings page uses
`dm/host/dm_custom.c`, not the older diagnostic `dm/android/app/custom_preferences.c` key table.
正式 APK 使用共享宿主设置实现；旧诊断模块的冻结键表不能证明正式游戏功能已接通。

| Key / 设置键 | Android production path / 正式接线 | Result / 结果 |
|---|---|---|
| `lang` / 语言 | `hle_fs.c: settings_lang_apply` reads `dm_custom_lang_explicit` before mounting settings | Wired; restart required / 已接线，重启生效 |
| `auto_mana` / 自动喝药 | `hle_hid.c` → `dm_actions_pump` → `auto_pot_pump` | Wired / 已接线 |
| `merc_pot` / 佣兵喂药 | `dm_actions.c: auto_merc_pot` reads setting | Wired; only with a mercenary and usable potions / 已接线，需佣兵与药水 |
| `drop_light` / 掉落光柱 | `dm_dropfilter.c` redirects item models through the rewritten drop hook | Wired; needs matching overlay models; new drops/area reload reflect changes / 已接线，依赖覆盖层资源，新掉落或重进区域生效 |
| `map_mark` / 地图标记 | `dm_world.c` map-marker and goal paths read setting | Wired / 已接线 |
| `item_notes` / 物品注释 | `diag.c` annotation hook reads setting | Wired; Chinese/Spanish resources, restart required / 已接线，限对应中文／西语资源，需重启 |
| `icloud` / iCloud | Sync adapter only in `ios/Sources/cloud_ios.m` | Unsupported on Android; row hidden at runtime, stored key retained / Android 不支持，运行时隐藏，保留存储键 |
| `pot_pick` / 自动拾药 | `dm_world_pump` → `dm_potpick_step`, plus `dm_beltauto.c` and server validation | Wired / 已接线 |
| `drop_haptic` / 掉落震动 | `dm_drophaptic.c` → JNI `takeDropHaptic` → `DropHapticPoller` → Android haptic feedback | Wired; Android system haptic policy still applies / 已接线，受系统震动策略控制 |
| `gem_pick` / 自动拾符文宝石 | `dm_pouch.c` client step and server tick read setting | Wired; uses advanced-stash resources / 已接线，依赖高级仓库资源 |
| `store_btn` / 存放按钮 | `dm_pouch.c` visibility, request and server paths | Wired / 已接线 |
| `eco_fps` / 省电模式 | Vulkan `mtl_fps_target` reads `dm_fps_setting` | Wired; when enabled, overrides launcher limit to 30 FPS / 已接线，开启时覆盖启动页设置为30帧 |
| `q3d` / 画质策略 | `dm_custom.c` writes `dm_3d_quality_mode_set`; controller consumes presented intervals | Incomplete: Android `mtl_presented_time` returns 0, so this path lacks actual presentation feedback / 未完整接通：Android 上屏时间返回0，该路径缺少实际反馈 |
| `loot_filter` / 掉落过滤 | `dm_lootfilter.c` selects rules by difficulty and equipped bow; called from potion-pick loop | Wired; conditional on difficulty/equipment / 已接线，受难度与装备条件影响 |
| `auto_buff` / 自动补增益 | `dm_autobuff.c: enabled` requires iOS or `DM_APP` unless forced by test environment | Incomplete: Android is not iOS and production launch does not set `DM_APP` / 未接通正式默认路径，被平台条件挡住 |
| `touch_swap_above` / 换组键位置 | iOS `front_ios.m` forwards setting into `dm_tc_set_swap_above` | Incomplete: Android has no corresponding forwarding call / 未接通，Android 缺少转发调用 |

## Follow-up / 后续

Older imported overlays show only Chinese/English and end at auto-buff, while the current host expects four
language widgets and includes a skill-switch-position row. An APK alone does not replace imported game overlays.
The [APK-bundled interface](OVERLAY_UPDATES.md) now delivers these resources with application updates. The earlier
independent-import flow passed device checks; its manual entry is now replaced by automatic APK resource preparation.
The new Custom page shows all four language choices and both skill-switch-position choices. Language labels use a
compact font to avoid wrapping Spanish/Russian names.
This fixes the resource-version mismatch; it does not itself fix the three host-wiring gaps below.
旧覆盖层只有中英两项、缺少换组位置行。界面现改为随 APK 自动更新，四语言与换组位置选项完整显示；
语言标签字号已调整以避免西语、俄语折行。资源版本升级不代表以下三项功能已接通。

The iCloud row was absent on the actual Custom page, the item-notes/potion-pick rows were adjacent without a gap,
and scrolling/reopening the page retained the compact layout. Local ASan/UBSan tests cover hidden visibility flags,
row geometry, null/incomplete pages and repeated filtering; existing settings persistence and iOS layout tests pass.
实机确认隐藏与相邻行布局；自动化验证不代表真实外接手柄导航或各玩法效果已逐项实测。

Keep upstream keys stable. Android filters unsupported rows without deleting saved values or changing iOS output.
The iCloud filter also removes the row's space and skips it during controller navigation, including existing imported overlays.
隐藏 iCloud 不要求重新导入游戏包，也不删除存档或改写 iOS 页面。

The three incomplete settings need separate functional changes and device verification. Do not treat a saved checkbox
as proof that its behavior works. Frame-limit and quality settings also need one shared value across launcher and in-game UI.
三项未完整接通的设置需补功能与实测；帧率和画质还应统一启动页、游戏内设置来源，避免互相覆盖。
