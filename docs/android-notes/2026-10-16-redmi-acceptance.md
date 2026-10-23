# Redmi K50 Ultra acceptance run (2026-10-16)

中文：[2026-10-16-redmi-acceptance.zh-CN.md](2026-10-16-redmi-acceptance.zh-CN.md)

Run from 18:37 to 19:16 NZDT (UTC+13) on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1.128, Android 15). The device setup is in [2026-10-16-redmi-k50-vulkan11.md](2026-10-16-redmi-k50-vulkan11.md).
- **Build:** `android/linux-build`. It started at `1449bf0`; from 19:01 it was `cea32fd`, which adds PR #4; from 19:08, `92f8886`, which also adds PR #5.
- **How it was driven:** the game was driven over adb (`input tap`, `input motionevent` for presses and the stick, `input text`) and checked with screenshots, `uiautomator dump` and logs. Nobody played it by hand.
- **Overlay:** from 18:59 the touch overlay was installed: `files/dm/overlay`, generated from the same RomFS; see Issue #6 for the Linux generation workaround.

## Acceptance list

| Item | Result |
|---|---|
| Resume after background | **Pass.** The app had been behind another app for about 15 min. Hot start, same process; surface and swapchain rebuilt; graphics suspend → resume; audio resumed; focus restored |
| Hero class → game modes → name | **Pass with a bug.** Without the overlay, the controller pages work with OK. The name dialog kept OK disabled after the IME committed a Latin name ("Finish choosing characters…") until the name was edited. Fixed in **PR #4**, verified on the device with a second character |
| Create and enter the world | **Pass.** Rogue Encampment, level 1, life 55/55, belt full; first atomic save commit 19 ms |
| Move, release stops | **Pass.** Stick hold 2.5 s walks; after release the world area changed by 0.13% of pixels (torch flicker only) |
| NPC | **Pass.** Tapping Charsi routed him there ("route 1 segment … → interact 1"), showed her greeting and the Talk menu; Back left it |
| Inventory, skill tree | **Pass.** Menu opens the panels: gear, belt and backpack shown, item tooltip docked; Skill Tree tab with sub-tabs |
| Sound | **Pipeline passes; audibility unverified.** AAudio track active (48 kHz stereo, usage GAME, 0 underruns, 53 ms latency, frames advancing). The phone's speaker media volume is 0 (`streamVolume:0`), left unchanged |
| Save and Exit | **Pass.** Options → Save and Exit: atomic commits #5–#9 (14–29 ms each), back to character select |
| Cold start → continue saved character | **Without the overlay: fails.** OK collapses the list and the front-end automation reopens it (Issue #6, point 1). **With the overlay and PR #5: passes.** PC-style character select → Play → world |
| Second character through the PC front end | **Pass.** Create New → tap the Sorceress → Create → Standard plaque → name dialog (PR #4) → world (level 1, life 40/40, mana 35/35) |
| Outdoor combat, black blocks outdoors | **Not reached.** Leaving camp with the stick at about 8 fps took many tries without finding the exit before time ran out |

## Performance (`NVN PERF v=1`, 60-frame windows)

| Scene | fps | GPU ms | CPU submit ms | draws per frame |
|---|---|---|---|---|
| Front-end menus | 46.6–60.3 | 4.5–5.1 | 10–14 | about 132 |
| Hero select | 7.0–8.5 | 71–75 | 111–126 | about 1,490 |
| Character select (3D) | 8.7–9.6 | 52–57 | 92–100 | about 1,160 |
| Rogue Encampment | 7.6–9.6 | 50–61 | 93–116 | 1,770–2,200 |

Battery 29.3 → 32.1 °C over the run, thermal status 0. In the camp the CPU side, synchronous translation with `DM_NVN_SYNC=1`, is larger than the GPU side.

## Other observations

- **Start-up timeline (PR #1):** with the fix, log times count from the actual launch. One process had been pre-forked 1,130 s earlier (comment on PR #1).
- **Short taps can be missed at about 8 fps.** One OK tap of default length was lost at hero select, while 200–500 ms presses always registered. Not investigated: whether a press shorter than a game frame is dropped by the touch sampling.
- **Android draws its five navigation buttons over in-game panels;** iOS hides them (Issue #6, point 3).
- **Map toggles the minimap;** there's no full-screen map step.

## Changes from this run

- PR #4: name dialog rechecks when the IME finishes composing.
- PR #5: front-end taps as clicks with the overlay.
- Issue #6: overlay required in practice, generator fails on Linux, nav buttons over panels.
