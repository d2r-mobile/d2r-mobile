# Playing on a Mac (macOS App, first version)

中文：[MAC.zh-CN.md](MAC.zh-CN.md)

On an Apple silicon Mac running **macOS 26 or later**, this project can be built into a native macOS App, `D2R Mobile.app`: double-click to start, with keyboard, mouse and gamepad all working. It runs the same game and the same features we added (rifts, the trove, the chronicle, the Custom page and so on), using the original game's gamepad UI.

**Up front: nobody has actually sat down and played this version yet.** Keyboard keys, a physical gamepad, the resource-folder dialog, ⌘Q and menu clicks, and how it feels in the hand — none of these five has been operated by a person. Everything described as "verified" below was run unattended: keys injected by a test script, the mouse through a test entry point, quit / full screen / window resizing through test switches. If you are among the first to try it, please open an issue and tell us how it went.

## Requirements

Apple silicon and macOS 26 or later. The graphics side is built entirely on a graphics interface that only exists from macOS 26 on (Metal 4); on earlier systems it neither compiles nor opens (the system says the App cannot be used with this version of macOS). The iPhone / iPad version requires iOS 26 for the same reason.

## Build

The prerequisites are the same as for the iOS route: your game files in place and `./setup.sh` finished (see "Getting started" on the front page). Then:

```bash
source ~/dm-work/build/cache/dm/venv/bin/activate
dm/tools/mac_app.sh
```

It takes a dozen seconds or so and produces `~/dm-work/out/macapp/D2R Mobile.app` (about 3 MB). To put it elsewhere: `dm/tools/mac_app.sh <output directory>`.

The App contains only our own program. The rewritten game program, the shader library and the UI overlay are made from your game files; they stay in your workspace (`~/dm-work`) and the App only records where they are — so **this App runs only on the Mac that built it**. Please do not copy it to anyone else, and do not delete the workspace. It is signed with a local ad-hoc signature: no developer account and no device registration are needed.

## First launch

Double-click `D2R Mobile.app`. The first launch asks you to choose the **game resource folder**: pick `~/dm-work/data/switch/3.3.94012/Update` (or the `RomFS` inside it); it must contain a `data` folder. A wrong choice is explained and you can pick again; a right one is remembered and not asked for again.

The key mapping is then shown once; afterwards it is always available under Help in the menu bar.

## Where things are

- Saves: `~/Library/Application Support/D2R Mobile/save`
- Log: `~/Library/Logs/D2R Mobile/host.log` (the previous run is `host.prev.log`). Please attach it when reporting a problem.

## Keyboard ↔ gamepad

Button prompts on screen show gamepad letters; the keyboard keys are:

| Gamepad | Keyboard |
|---|---|
| Left stick (move) | W A S D |
| D-pad | Arrow keys |
| A confirm / B back / X / Y | J / K / U / I |
| L / R | Q / E |
| ZL / ZR | Z / C |
| Left stick press / right stick press | F / G |
| + (menu) / − (map) | Return / Tab |

Mouse: left click on a target or on the UI, right click = B (back / close panel), wheel scrolls lists. A gamepad works as soon as it is connected, alongside the keyboard.

The menu bar has View → Full Screen (⌃⌘F) and Quit (⌘Q). The window can be resized freely; the game picture stays 16:9 and centred.

## Quitting and saving

Closing the window, ⌘Q and Quit in the menu all save first and then quit, in any state: in game, with the pause menu open, with a panel open, in a loading screen and with the rift panel open were each verified once. It waits at most 3 seconds; if the save has not completed by then, the log records "quit without saving".

## How this version is operated

- **Move** with W A S D or the gamepad stick. A mouse click on a monster, NPC or object walks over and interacts; a click on empty ground does not move.
- **Items** — sorting, equipping, buying and selling — are handled with the keyboard or gamepad cursor: move the cursor onto the item with W A S D and press the key shown in the bottom bar. For example, buying from a vendor: cursor onto the goods (the bottom bar shows "A Buy") → press J → press J again in the confirmation.
- **The UI** is the original gamepad UI, and the pages we added (the Custom page, the trove, the chronicle, rift floor selection and so on) are all in it.

## Not in this version yet

- Mouse click-to-move on the ground, and long-press / double-click / drag-and-drop on items with the mouse.
- The free town portal on the empty ZR slot of the gamepad.
- The rift task list on the left.
- iCloud save sync, update checks, sharing the diagnostic log.
