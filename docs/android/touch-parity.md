# r2d touch parity matrix (T0)

Project baseline: `0d397fddedbc199e05d3e9d8c63de40984109a80`.

Integration base: `c91c83c060aac2ddf2989128497451c45feb575e`.

Status: design inventory for issue #31. This is not Android acceptance and not a
claim that the game is playable on a phone. The final product target is smooth
Xiaomi 15 Ultra play with the complete r2d game-specific touch set; a generic
virtual gamepad is explicitly out of scope. Parent: #19. The parent and the game
playability target stay open.

## Freeze and evidence rules

- Frozen upstream source: `source snapshot`
  (verified through the local git object database; no history fetch).
- Current fork head inspected: `source snapshot`
  (the branch base after the peer #13 Vulkan merge; the frozen touch files have
  no diff from `source snapshot`, the earlier base).
- The frozen touch files have no diff between `source snapshot` and this fork head.
- SHA-256 of the frozen source blobs:

| File | SHA-256 at `source snapshot` |
|---|---|
| `dm/ios/Sources/dm_touch_controls.c` | `93d6c3d55111619821bd775ea7742dc58bb82b010eee68bba4562c6331036088` |
| `dm/ios/Sources/dm_touch_controls.h` | `6d090e7dc54d774f7ce0190b8415623d80071e8dbc201744ba0f516c357d34b8` |
| `dm/ios/Sources/dm_touch_game_state.c` | `573adf3c866b5ce88149e1d2bf41d3632bd1a3c66e7e885f0cf34294ad7edfc2` |
| `dm/ios/Sources/dm_touch_game_state.h` | `83c6688c45db8a31a15ea8dcf6d774558a67170b3d3bbbbe63dd5231af1931f6` |
| `dm/ios/Sources/dm_touch_game_identity.c` | `60a04eb4744c25f074b025b6500f31f69145141b8c2d07a1158c8e3be75f0a06` |
| `dm/ios/Sources/dm_touch_game_identity.h` | `6f857b086432f5a6ee965555dea12e9429e44d49a11dd80ce2f6370ccad73b01` |
| `dm/ios/Sources/dm_touch_game_bridge.c` | `d936e62fce5d5241cf56a34857065d2fbcfeafa83f258d4e4011f6daa94f0358` |
| `dm/ios/Sources/front_ios.m` | `96da101e8f7e52179672fd4fa3861734b462669da21de7613cac51bb571853bb` |
| `dm/host/hle_touch.c` | `672d3a8a1773cda52b7bdb8a4e6cf60ce020baeb7ee3360e05f0050c3730ae77` |
| `dm/host/hle_hid.c` | `14e88ef41befe215bc9d6b01ab9ca13387efb305e72e9f090394435c1daf5887` |
| `dm/host/dm_textinput.c` | `1ec867057fe4c8004151c94cd6a5f5b3afa2d98f2b758e9629792cf5a21f2a26` |

Only source-backed behavior is recorded as implemented below. The iOS README is
a claim/documentation source, not execution evidence. Existing `dm/verify/touchcases`
are Apple/local-host fixtures and historical game-log checks; they are not new
Android or Xiaomi evidence.

`docs/android/check_touch_parity.py` checks structure, required issue coverage,
numeric line ranges and the frozen source hashes. It does not validate symbols,
gesture behavior or Android execution; test names such as
`dm/ios/tests/t_touch_controls.c:world_tap` are references, not line anchors.
A passing checker run is not behavioral or device acceptance.

## Observable-operation matrix

Column meanings:

- **State/timing** = the portable gesture state machine in
  `dm/ios/Sources/dm_touch_controls.{c,h}`.
- **Platform/hook/asset** = Apple platform glue, host hit/read bridge, game
  version dependency or art/layout dependency.
- **Android gap** = what exists in this Android tree today.
- **Device** = remaining real-game/device acceptance. Every row below still
  requires the Xiaomi 15 Ultra verification in #40.

| # | Game context | Intended gesture | State/timing | Platform / hook / asset | Android current gap | Issue | Existing evidence | Device |
|---|---|---|---|---|---|---|---|---|
| 1 | Game vs panel / dialogue / death | Recognise game state without guessing from keys; drive navigation mode | `dm_tc_navigation`, `dm_tc_panel_mode`, `dm_tc_scene_dialog_only`; `dm/ios/Sources/dm_touch_controls.h:133-142`, `dm/ios/Sources/dm_touch_controls.c:697-714` | Original UI messages + panel stack; iOS `dm/ios/Sources/front_ios.m:762-821`; AutoMap is an overlay, not navigation | No Android panel/UI-message bridge; no scene state | #33 | `dm/ios/tests/t_touch_controls.c:ui_scene_messages`; touchcases `no_move_taps`, `difficulty_menu` | Xiaomi #40 |
| 2 | Panel/item hit testing | Classify item area / tab / outside / covered button / vendor goods / stack / locked item; decide drops | `dm_tc_pointer_item`, `dm_tc_pointer_release*`, pointer counters; `dm/ios/Sources/dm_touch_controls.h:73-113,189-196` | Host `dm_ui_item_query/result`, `dm_ui_drop_query`, panel tree and widget hit testing (`dm/host/dm_cursor.h:46-80`) | No Android UI tree reader or hit bridge; no panel-state source | #33 | `dm/ios/tests/t_touch_controls.c:pointer_tap`, `pointer_item_tap`, `pointer_control_hit`, `pointer_stack_tap`; touchcases `inv_*`, `tab_tap` | Xiaomi #40 |
| 3 | Skill identity / binding | Know whether a bound skill is currently owned; bind empty skill slots | `dm_tg_snapshot`, `dm_tg_group`, `dm_tg_hud_identity`; `dm/ios/Sources/dm_touch_game_state.h:7-37`, `dm/ios/Sources/dm_touch_game_identity.h:5-18` | Same-process Mach read, HUD cohort identity, game getter hook; `dm/ios/Sources/dm_touch_game_bridge.c:12-27,116`; offsets tied to image | No Android game-state read / identity / binding bridge | #33 | `t_native_binding.c`, `t_binding_group.c`, `t_binding_freshness.c`, `t_native_hud_bridge.c`, `t_native_hud_identity.c` | Xiaomi #40 |
| 4 | In-run movement | Left-zone joystick; dead zone, diagonal pre-correction, bounded base, multi-finger ownership, system-cancel resume | `dm_tc_begin_at/move_at/end_at`, joy fields, `DM_TC_JOY_RESUME_S=0.35`, `DM_TC_JOY_GRAB_K=1.5`; `dm/ios/Sources/dm_touch_controls.c:114-239,451-532`; `dm/ios/Sources/dm_touch_controls.h:199-204` | UIKit points; `dm/ios/Sources/front_ios.m:493-568`; host HID `dm/host/hle_hid.c:57-70,402-404` | Only `dm_android_pad` Npad snapshot (`runtime/input.c`); no touch-to-axis adapter, no HUD stick | #35 | `dm/ios/tests/t_touch_controls.c:joystick_deadzone`, `joystick_clamp_956`, `joystick_axes`, `joystick_plus_skills`, `no_auto_handoff`, `background_no_key`; touchcase `move` | Xiaomi #40 |
| 5 | World interaction | Tap world target (NPC/item/chest); hold without movement; never turn a non-stick tap into ground movement | World tap threshold/seq and `dm_tc_belt_outside`; `dm/ios/Sources/dm_touch_controls.c:89-94,427-439,554-656` | `dm_ui_world_tap` → `dm_world_tap` (`dm/host/dm_cursor.h:63`, `dm/host/dm_world.c` target hit); minimap tap `dm/ios/Sources/front_ios.m:450-451` | No Android world hit-test or coordinate bridge; input queue ignores touch | #37 | `dm/ios/tests/t_touch_controls.c:world_tap`; touchcases `no_move_taps`, `skill_main`; host world tests | Xiaomi #40 |
| 6 | Main skill / interaction | Main button tap/hold; short press becomes a guaranteed pulse; drag changes aim | Main target + `DM_TC_KEY_MIN_S=0.035`, `DM_TC_AIM_SETTLE_S=0.08`; `update_aim`, `update`, `pulse_button/epoch`; `dm/ios/Sources/dm_touch_controls.c:127-239,335-359` | Host Npad button read `hle_hid.c`; game cast packet / target; `front_ios.m` tick + pulse | Only Npad button snapshots; no aim axes/timing adapter | #35 | `dm/ios/tests/t_touch_controls.c:key_hold_and_ownership`, `pointer_pulse_queue`, `pointer_game_mode`; touchcase `skill_main` | Xiaomi #40 |
| 7 | Combat skills 1–4 | Tap or drag-to-aim each skill; four independent fingers; release pulse | `DM_TC_SKILL1..4`, `key_down`, `aim_*`, `pulse_*`; `dm/ios/Sources/dm_touch_controls.c:127-239,554-656`; `dm/ios/Sources/dm_touch_controls.h:12-15,27-65` | HID right axis and face buttons; host action mapping; HUD widgets / art | No Vulkan skill HUD, no Android aim/button timing | #34/#35 | `dm/ios/tests/t_touch_controls.c:key_hold_and_ownership`, `two_fingers_same_key`, `enabled_targets`; touchcase `skill_main` | Xiaomi #40 |
| 8 | Two skill sets / ZL lead | Toggle set locally; second set presses ZL before the skill, never same frame; short second-set tap deferred until lead is complete | `skill_set`, `defer_button/defer_at_s`, `DM_TC_ZL_LEAD_S=0.17`; `dm/ios/Sources/dm_touch_controls.c:62-66,153-239,665-684` | Original game's ZL alt-slot semantics; HID buttons; no ZL on belt pulses | No Android set mode, ZL lead scheduler, or deferred pulse state | #35 | `dm/ios/tests/t_touch_controls.c:deferred_skill_vs_belt`, `held_skill2_vs_belt`, `swap_key` | Xiaomi #40 |
| 9 | Belt — player drink | Tap/release a belt cell = player drinks; several taps queue drinks; no double-tap and no mercenary feed | `belt_q`, `belt_pulse_s`, `belt_gap_*`, `DM_TC_BELT_SWIPE_PT=24`; `dm/ios/Sources/dm_touch_controls.c:441-450,554-656,665-684` | Host belt key / column mapping; `dm/ios/Sources/front_ios.m:510-511`; HUD belt state | No belt HUD or Android belt mapping | #35 | `dm/ios/tests/t_touch_controls.c:belt_single_double`, `deferred_skill_vs_belt`, `held_skill2_vs_belt`; touchcase `belt_drink` | Xiaomi #40 |
| 10 | Belt — expanded rows | Swipe up/down on a belt cell to expand/collapse; tap upper row to drink; tap elsewhere collapses | `belt_rows`, `belt_open`, `belt_request`, `dm_tc_belt_outside`; `dm/ios/Sources/dm_touch_controls.h:117-130`; `dm/ios/Sources/dm_touch_controls.c:427-450` | Host `dm_belt_rows`/`dm_belt_expand`; row/column key mapping | No Android belt row model or HUD expansion | #35 | `dm/ios/tests/t_touch_controls.c:belt_expand`, `belt_single_double` | Xiaomi #40 |
| 11 | Town portal button | Product-specific free-town-portal action; layout does not claim an original game binding | `DM_TC_PORTAL`, portal pending/armed/ack, binding identity + scene epoch; `dm/ios/Sources/dm_touch_controls.c:403-425`; `dm/ios/Sources/front_ios.m:506,516` | Host `dm_tp_request` and binding refresh; town-portal art/asset | No Android portal button, binding identity, or action bridge | #35 | `dm/ios/tests/t_touch_controls.c:circles_before_joystick`, portal sequence tests; not proof of original binding | Xiaomi #40 |
| 12 | Map / minimap | Tap minimap block to open full map; tap full map to return; map-owned cancel region | `DM_TC_MAP`, `inside_map_cancel`, `map_cancel_active`; `dm/ios/Sources/dm_touch_controls.c:120-126,554-656` | Host `dm_ui_map_full`, map geometry; fullscreen map is game-mode only (`dm/ios/Sources/front_ios.m:189,450-451`) | No Android map HUD or map-state bridge | #37 | `dm/ios/tests/t_touch_controls.c:circles_before_joystick`, `cancel_and_resize`; map host tests | Xiaomi #40 |
| 13 | Inventory — tap / double / long | Single tap in item area moves cursor only; double tap uses/equips/opens; long press refills belt; identify-first path | `pointer_item`, `ident_seq`, `use_seq`, `long_press_seq`, `stash_x_*`; `dm/ios/Sources/dm_touch_controls.c:576-620,665-684`; `dm/ios/Sources/dm_touch_controls.h:73-114` | Host item info, identify/use/long-press APIs; `dm/host/dm_cursor.h:63-80` | No Android item hit/model or cursor-overlay action bridge | #36 | `dm/ios/tests/t_touch_controls.c:pointer_item_tap`, `pointer_long`, `pointer_long_seq`, `pointer_long_then_drag`; touchcases `inv_tap`, `inv_double_tap`, `inv_long_press` | Xiaomi #40 |
| 14 | Inventory — drag/drop | Drag item; lift point vs finger point; drop / return / discard; no second item pickup | `pointer_dragged`, `release_x/y`, `pointer_release*`, `DM_TC_POINTER_LIFT_PT=40`; `dm/ios/Sources/dm_touch_controls.c:306-312,554-656` | Host `dm_ui_drop_query`, `dm_ui_drag_*`, item rectangles | No Android drag layer or drop hit bridge | #36 | `dm/ios/tests/t_touch_controls.c:pointer_drag`, `pointer_drag_out`, `pointer_long_then_drag`; touchcases `inv_drag`, `inv_drag_back`, `inv_drag_out` | Xiaomi #40 |
| 15 | Vendor | Drag from shelf = buy only on inventory drop; long press = hold Y bulk buy; second tap with stash/vendor open must not be compare | `pointer_shop`, `shop_drop_seq`, pulse 1.3 s, `g_vendor_open`; `dm/ios/Sources/dm_touch_controls.c:277-289,566-570`; `dm/ios/Sources/dm_touch_controls.h:95-99` | Host vendor page, shelf rectangle / cursor restore, `dm_ui_shop_buy_at` | No Android vendor hit/page bridge | #36 | `dm/ios/tests/t_touch_controls.c:pointer_shop_drag`; source tests only; no touchcase for buy | Xiaomi #40 |
| 16 | Stash / cube / chronicle | Double tap inventory: stack via X then Y fallback; cube side-by-side; chronicle deposit; stacked-cell take one | `g_bank_open`, `g_cube_open`, `g_vault_open`, `g_cur_stack`, `stash_x_check_s`; `dm/ios/Sources/dm_touch_controls.c:289-304,576-620,675-684` | Host panel state bits, item container/stack reads, vault UI; `dm_cursor.h` panel state | No Android stash/cube/vault state or item model | #36 | `dm/ios/tests/t_touch_controls.c:pointer_stack_tap`, `pointer_item_tap`; host stash/cube tests | Xiaomi #40 |
| 17 | Locked single-item slot / imbue / socket / personalize | Press other item while slot occupied = cursor/tooltip only; never drag/double/long; quest slot takes item by Y hold | `pointer_lock`, `locked_drag`, `g_imbue_open`; `dm/ios/Sources/dm_touch_controls.c:278-308,566-604`; `dm/ios/Sources/dm_touch_controls.h:100-106` | Host item query result 10, panel state bit 16, hold-Y mapping | No Android locked-slot/imbue panel bridge | #36 | `dm/ios/tests/t_touch_controls.c:pointer_control_hit`, `pointer_item_tap` | Xiaomi #40 |
| 18 | Tabs / panels | Tap tab to switch page; tap outside panel to close with repeated B until closed; stopped by confirmation modal | `tab_tap_seq`, `close_left/close_next_s`, `g_close_stop`; `dm/ios/Sources/dm_touch_controls.c:290-290,587-590,665-684` | Host `dm_ui_tab_go`, panel stack, UI messages; `dm_cursor.c` tab walk | No Android tab/panel navigation | #37 | `dm/ios/tests/t_touch_controls.c:ui_scene_messages`, `pointer_outside_close`, `pointer_tap`; touchcase `tab_tap` | Xiaomi #40 |
| 19 | Scroll / slider | Vertical drag = scroll; horizontal drag = slider; threshold fixed by initial direction; no key/click | `ui_drag` 1/2, `dm_tc_move_at`; `dm/ios/Sources/dm_touch_controls.c:533-553`; unit `ui_drag_typing` | Host `dm_ui_drag_begin/move/end`, wheel/slider path; `dm/ios/Sources/front_ios.m:536-543` | No Android scroll/slider gesture bridge | #37 | `dm/ios/tests/t_touch_controls.c:ui_drag_typing`; host drag tests | Xiaomi #40 |
| 20 | Difficulty | Tap bottom cell 0 toggles 1–8 strip; pick a level → host `/players`; no key emitted | `diff_open`, `diff_request`, pick cells; `dm/ios/Sources/dm_touch_controls.c:60-73,451-482` | Host `dm_hud_request_players`, bottom HUD; `dm/ios/Sources/front_ios.m:238` | No Android difficulty HUD or request bridge | #37 | `dm/ios/tests/t_touch_controls.c:difficulty_picker`; touchcases `difficulty`, `difficulty_menu` | Xiaomi #40 |
| 21 | Custom settings / prefs page | Tap checkboxes; apply/discard/default; scroll rows; controller-selected row scrolls into view | Pending/applied values (`g_pend/g_val`); `AcceptChanges`/`CancelChanges`/`ResetToDefaults`; `dm/host/dm_custom.c:21-99` | Host `dm_custom` settings page; `dm/host/dm_custom.c:1-11,92-99`; layout `dm/ios/tools/touch_layout.py:1267-1290` `_custom_page` | No Android settings page, persistence/prefs, or widget bridge | #39 | Host `t_textbox`/custom tests (source-owned); no Android custom-page test | Xiaomi #40 |
| 22 | Text / numeric input | Tap text box opens system keyboard; number box numeric keypad with min/max clamp and half/all | `dm_textinput_pump`; `dm/host/dm_textinput.c:21-58`; `hle_swkbd.c` | System keyboard path, `g_swkbd_numeric_max`, `dm/ios/Sources/front_ios.m:955-1020` | Android app has no IME/text bridge; input queue handles only BACK | #38 | Host `t_swkbd`, `t_textbox`; no Android IME test | Xiaomi #40 |
| 23 | Chinese IME / name rules | Chinese and English text; composing state preserved; character-name rules; 15 UTF-8 bytes, 2–15 bytes, edge `-`/`_` | `dm/ios/Sources/front_ios.m:977-1020`; `hle_swkbd.c` | iOS `UITextField` marked-text / IME; Android InputMethodManager/Compose semantics differ | No Android IME, composing-state or name-rule path | #38 | Host textbox tests; iOS frontend source checks; not Android proof | Xiaomi #40 |
| 24 | Layout / safe area / scale | 667/375 baseline, 812/956/1024/iPad, tall scale, left/right safe insets, bottom system edge | `dm_tc_layout_make`, `set_safe_insets`, `set_edge_bottom`, `scale_for_size`; `dm/ios/Sources/dm_touch_controls.c:22-80`; `dm/ios/Sources/dm_touch_controls.h:144-152` | UIKit points, safe area, density; `dm/ios/Sources/front_ios.m:153-167`; versioned layout specs | No Android density/inset/viewport mapping or HUD layout | #34 | `dm/ios/tests/t_touch_controls.c:layout_*`, `safe_insets`, `hit_edges`; `test_skill_aim_front.py` spec hash | Xiaomi #40 |
| 25 | HUD / art / context | Skill buttons, belt, orbs are display-and-hit targets; HUD context blocks world taps; native art atlas | `enabled_targets`, `dm_tc_nav_mask`, layout targets; `dm_touch_native_art.h`; `TouchArt/dm_touch_joy.png` | Metal/UIKit overlay, native art atlas generated from user game files, HUD/window context | No Vulkan HUD; Android `native.c` only a smoke screen | #34 | Layout/spec tests, `t_native_art`; no Android HUD render test | Xiaomi #40 |
| 26 | Controller handoff | Touch in controller mode switches back to touch, first touch consumed only by mode switch; pad mode hides touch layer | `dm/ios/Sources/front_ios.m:493-504,178`; `dm_gamepad` callback | iOS GameController lifecycle; mode rebuild epoch | No Android controller mode/handoff; only fixed Npad snapshots | #39 | Source checks; `dm/ios/tests/t_touch_controls.c:no_auto_handoff`, `background_no_key`; no Android handoff test | Xiaomi #40 |
| 27 | Focus / system cancel / lifecycle | System cancels a finger → release what it owns, do not treat as tap; focus loss clears; resize cancels all | `dm_tc_cancel_finger`, `dm_tc_cancel_all`, `dm_tc_resize`; `dm/ios/Sources/dm_touch_controls.c:314-333,369-401`; `dm/ios/Sources/dm_touch_controls.h:154-182` | UIKit touch cancel/focus; Android `ACTION_CANCEL`/pointer IDs differ | No Android `ACTION_CANCEL` or focus-cancel adapter | #32 | `dm/ios/tests/t_touch_controls.c:cancel_and_resize`, `invalid_slots_and_repeated_begin`; source front-end sweep | Xiaomi #40 |
| 28 | Multi-touch ownership | One joystick owner; one pointer/aim owner; independent skill fingers; all slots cap 16; no auto handoff | `joy_slot`, `aim_slot`, `pointer_slot`, finger array; `dm/ios/Sources/dm_touch_controls.c:193-239,451-532` | UIKit touch IDs; Android pointer IDs/motion history | Npad snapshot has no pointer IDs or ownership | #32 | `dm/ios/tests/t_touch_controls.c:two_fingers_same_key`, `no_auto_handoff`, `all_slots_hold` | Xiaomi #40 |
| 29 | Touch pulse / cancel | Short press pulse, deferred second-set pulse, pulse queue, aim cancellation, map cancel; no double key merge | `pulse_*`, `defer_*`, `aim_cancelled`, `gesture_epoch`; `dm/ios/Sources/dm_touch_controls.c:335-359,554-684` | HID read sampling/edge latch; `front_ios.m` pulse end; host input receipt | Android Npad latches edges but has no gesture pulse/aim state | #32 | `dm/ios/tests/t_touch_controls.c:pointer_pulse_queue`, `deferred_skill_vs_belt`, `held_skill2_vs_belt`, `key_hold_and_ownership` | Xiaomi #40 |
| 30 | Layout/version/asset identity | Versioned layout profile and native art hash must match the frozen source | `touch_layout_spec_native_binding_v5.json` `profile_id=touch_native_binding_v5`, `input_schema=dm-touch-input-5`, `extends...v4`; `dm/ios/tests/test_skill_aim_front.py:10-16` | Layout spec, generated overlay, game-file-derived atlas; not all source-controlled | No Android profile/version gate or asset generator | #34 | Spec hash/source checker; no Android profile test | Xiaomi #40 |

## Versioned identity and content dependencies

- Frozen layout/input identity: `touch_native_binding_v5`, input schema
  `dm-touch-input-5`, extending `touch_native_binding_v4`; the v5 JSON is
  source-controlled and its hash is checked by `dm/ios/tests/test_skill_aim_front.py`.
- Versioned game behavior: host reads and panel/widget names are tied to the
  inspected product image/update content. This inventory does not publish
  proprietary payloads, raw disassembly, keys or save data. Any Android bridge
  must gate on the same verified product/layout version rather than guessing
  offsets.
- Source-controlled touch assets: layout JSON profiles, `TouchArt/dm_touch_joy.png`
  and its manifest, verification scripts and synthetic fixtures.
- Generated from the user's game files at build time: the native skill atlas
  (`dm_touch_native_art.c` is not kept in the repo), overlay RomFS layouts,
  original textures/fonts and other game-derived content. These are not public
  source assets and are not evidence of Android device behavior.
- User-generated content (saves, character data, custom settings files) remains
  user data and is not part of this matrix or the public repository.

## Portability limits

- UIKit touch timestamps, view coordinates, safe-area insets, gesture
  recognition and `UITextField` IME composition are not available on Android.
  `front_ios.m` is platform glue, not reusable product code.
- Metal layers, Metal textures and the iOS native art/overlay path are not
  Vulkan. #34 must provide an independent Android HUD/art path and safe-area
  layout; passing a host fixture or compiling the existing runtime is not that
  proof.
- `dm_android_pad` is an isolated Npad snapshot: buttons, axes, latched edges,
  connection and sampling. It has no screen coordinates, multi-pointer identity,
  panel/scene state, hit test, gesture timing, pulse, text input or HUD. It can
  feed the #19 HID boundary but cannot substitute for r2d touch parity.
- The existing `dm/verify/touchcases` and Apple/local game logs are historical
  evidence only. They use game assets and platform-specific paths; none is
  Android or Xiaomi evidence.

## Smallest coherent implementation order

1. **#32** — port the portable C gesture state machine and Android MotionEvent
   adapter: pointer identity, focus/cancel, multi-touch ownership, timing/pulse.
2. **#33** — connect Android scene/panel state, game identity and UI/item hit
   queries; this is a prerequisite for almost every inventory, menu and skill
   action.
3. **#34** — build the Vulkan HUD, safe-area/density layout and source-controlled
   art path; do not reuse the Metal overlay as if it were Android.
4. **#35** — movement, aim, skill sets, ZL lead, belt and portal actions on the
   portable core plus HUD/hit bridge.
5. **#36** — inventory, shop, stash, cube, chronicle/vault and locked-slot
   operations, after #33/#34/#35.
6. **#37** — world interaction, panel/tab navigation, scroll/slider, difficulty
   and custom-page navigation, after #33/#34.
7. **#38** — Android IME/text/numeric input and Chinese composing/name rules.
8. **#39** — settings persistence, layout preferences and controller/touch
   handoff, including focus/cancel and repeated layout rebuilds.
9. **#40** — Xiaomi 15 Ultra real-game touch equivalence, latency and full r2d
   operation acceptance. This T0 matrix does not satisfy #40.

## Concrete gaps reported for taskboard

These are bounded gaps already covered by #32–#39; no duplicate issue is needed:

- `dm/android/app/native.c:64-71` consumes the input queue but handles only the
  BACK key; touch `MotionEvent`s are not decoded or queued.
- `dm/android/runtime/input.{h,c}` provides only an Npad snapshot; there is no
  Android touch-to-gesture, scene, hit, HUD or text path.
- No Android equivalent of `dm/host/hle_touch.c`/`hle_hid.c` touch-screen state
  or of the iOS `dm_touch_game_*` identity/hit bridge.
- No Android Vulkan HUD or safe-area/layout profile; #34 owns it.
- No Android IME/text input or persistent touch settings; #38/#39 own them.

## Claim-only / unknown register

These are not promoted to source-backed parity claims:

- “Complete r2d touch equivalence” is the product goal, not a completed result.
- The town-portal control is a product-specific free-town-portal action; the
  layout source explicitly does not claim the original game has a matching
  binding. Actual original-binding equivalence is unknown.
- Apple/local touchcase outputs are historical evidence. They do not prove
  Android `MotionEvent`, Vulkan HUD, IME, controller handoff or Xiaomi timing.
- Android Chinese IME composing, focus/cancel behavior and controller-to-touch
  handoff have no Android implementation or device evidence yet.
- Host reads and UI/widget names are tied to the inspected product content;
  behavior against another game version is unknown.
- Android real-game touch latency, thermal behavior and full operation
  equivalence remain unknown until #40.
- No feature is inferred solely from the iOS README; README claims are kept
  separate from source-backed rows.

## Inventory adapter regression coverage (#101)

`dm/android/tests/touch_adapter.c` now drives the frozen core inventory
contracts through actual adapter `down/move/up/tick/pointer_hit/pointer_release`
entry points: single/double tap (`USE` exactly once; late or other-position
singles and the compare `ZL`), long press (`LONG_PRESS` once, long-then-drag
suppresses it), cancel (no action, stale UP rejected, same ID reusable), drag
pickup/drop `A` pulses, drag-out discard `Y`, drag-back `A` (pad only; cursor
position is not observed), panel/scene change cancel, stash double-tap `Y`,
stacked-cell `STACK_TAKE` and stack drag `SHOP_DROP`. Non-zero core holds
(`0.6 s` stash `Y`, `0.15 s` discard `Y`) stay pressed across repeated reads and
ticks until the standalone first-read hold clock expires; a full pulse queue
still lets an already observed head expire and drain while an unobserved head
keeps its full first-read hold. This is an adapter
consumption boundary, not a guest/cursor-delivery acknowledgement. This is
offline adapter evidence only: there is still no
item hit/model, cursor overlay, host drop query, storage or trade result,
provider, HUD or device claim. Identify (`ident_seq`) and drop-position classes
that require host `dm_ui_*` answers are not exposed through the adapter and are
not fabricated.

## What remains unknown

- Whether every source-backed Apple gesture has an exact game-visible equivalent
  on Android; only Xiaomi #40 real-game testing can decide that.
- Whether current game content still matches every host panel/widget name and
  layout assumption; the freeze is versioned, not a compatibility guarantee.
- Whether the Android renderer can present the HUD at the measured touch latency
  and with acceptable thermal/frame behavior; #34/#40 own that evidence.
- No real SDK teardown, controller handoff, Chinese IME behavior or multi-touch
  device timing was executed in this design round.
