# Playing on a Mac (macOS App)

简体中文: [MAC.zh-CN.md](MAC.zh-CN.md)

On an Apple silicon Mac this project can be built into a native macOS App, `D2R Mobile.app`: double-click to start, with keyboard, mouse and gamepad all working. It runs the same game and the same features we added (rifts, the trove, the chronicle, the Custom page and so on), using the original's gamepad interface.

**Up front: these five things have not been operated by a person yet** — keyboard keys, a physical gamepad, the resource-folder dialog, ⌘Q and menu clicks, and how it feels in the hand. Where this page says "verified", it was run unattended: keys injected by a test script, the mouse through a test entry, quitting, full screen and resizing through test switches. If you try it, please open an Issue and tell us how it went.

## Requirements

Apple silicon. Two tiers by system:

| Your system | How to build | Output |
|---|---|---|
| macOS 26 or later | Default: `dm/tools/mac_app.sh` | `D2R Mobile.app` |
| macOS 15 – 25 | Older-API build: `DM_MAC_M3=1 dm/tools/mac_app.sh` | `D2R Mobile M3.app` |

The default build puts the graphics side on a graphics interface that only exists from macOS 26 on (Metal 4); on earlier systems it does not open (the system says the App cannot be used with this version of macOS). The older-API build is the same source and the same game with the graphics side using interfaces that older systems also have; its minimum is macOS 15.0. Details: [OLDER_SYSTEMS.md](OLDER_SYSTEMS.md).

## Build

The prerequisites are the same as for iOS: game files in place and `./setup.sh` finished (see "Getting started" in the README). Then:

```bash
source ~/dm-work/build/cache/dm/venv/bin/activate
dm/tools/mac_app.sh                  # macOS 26 or later
DM_MAC_M3=1 dm/tools/mac_app.sh      # macOS 15 – 25
```

The default build takes ten-odd seconds and produces `~/dm-work/out/macapp/D2R Mobile.app` (about 3 MB). The older-API build recompiles the shader libraries for macOS 15 the first time, which adds about 3–4 minutes, and produces `D2R Mobile M3.app`; it has its own save and log directories (with M3 in the name), so it can sit next to the default App. To put the output elsewhere, add an output directory after the command.

The App contains only our own program. The rewritten game program, the shader libraries and the UI overlay are all made from your game files; they stay in your working directory (`~/dm-work`) and the App only records where they are — so **the App runs only on the Mac that built it**. Please do not copy it to anyone, and do not delete or move the working directory (the shader libraries recompiled for the older-API build are in the output directory). Signing is local ad-hoc signing: no developer account and no device registration.

Version and build number can be set: `DM_MAC_VERSION` (default 1.0) and `DM_MAC_BUILD` (default: the current month-day-hour-minute).

## First launch

Double-click the App. The first launch asks you to choose the **game resource folder**: pick `~/dm-work/data/switch/3.3.94012/Update` (or the `RomFS` inside it); it must contain a `data` folder. A wrong choice is explained and you are asked again; a correct one is remembered.

A key reference is then shown once; afterwards it is under Help in the menu bar.

Language: the first launch follows the system language (Chinese systems start in Chinese, everything else in English). To change it: Options → Custom, the language switch in the first row → Accept All → close and reopen the App.

## Keyboard ↔ gamepad

Button prompts on screen show gamepad letters. Where the screen says "A", "B", "X" or "Y", the key to press is **not** the letter of the same name on the keyboard (the keyboard's A walks left) but the one in the right-hand column:

| What you want / gamepad button | Keyboard |
|---|---|
| Confirm (A) | Space or J |
| Back (B) | Esc or K (in game with no panel open, Esc opens the inventory group of pages) |
| Return | confirm; acts as + when the bottom of the page shows a "+" hint (such as "Accept" after naming); on the main menu Return = enter the game |
| + (menu / new character) | the = key |
| − (map) | Tab |
| Left stick (move) | W A S D |
| D-pad | Arrow keys |
| X / Y | U / I |
| L / R | Q / E |
| ZL / ZR | Z / C |
| Left stick press / right stick press | F / G |

**Creating a character, start to finish**: on the main menu press `=` (or click "New Character") → choose a class with the arrow keys and press Return (or click that class's figure) → on the mode page press Return or Space → on the naming page press Return to open the input box, type, Return → press Return once more to create.

A gamepad works as soon as it is connected, alongside the keyboard. The menu bar has View → Full Screen (⌃⌘F) and Quit (⌘Q). The window can be resized freely; the game picture stays 16:9 and centred.

## Mouse

What it can do now:

- **Click a target in the world** (monster, NPC, chest, waypoint, item on the ground): the character walks over and interacts.
- **Click the bottom row**: a skill icon = use that skill (the same as pressing the key shown under it), a potion cell = drink it, the middle button = open the inventory group of pages, the run / walk button = toggle.
- **Click the interface**: buttons, checkboxes and tabs in panels (including the in-page tabs of the skill tree), the pause menu, the settings pages, "Start" on the rift level-selection page.
- **Click a key hint at the bottom**: clicking an entry presses that key.
- **Click a figure when creating a character**: the class you click is selected.
- **Top-right minimap**: opens automatically on entering a game; Tab toggles it; clicking the area name at its top switches to the full-screen map, and another click returns.
- Right click = back (B); the wheel scrolls lists.

What it cannot do yet:

- **Clicking empty ground does not walk.** Move with W A S D or a gamepad stick.
- **Key caps on screen are still gamepad letters**; translate them with the table above.
- **Long press / double click / drag / buying on items do not take the mouse.** Use the keyboard or gamepad cursor: move the cursor onto the item with W A S D and press the key shown in the bottom bar. For example, buying from a vendor: cursor onto the goods (the bottom bar shows "A Buy") → press Space (or J) → press it again in the confirmation.
- "Hold …" key hints need longer than a click, so hold the key on the keyboard; a hint with two key caps is not recognised.
- The life orb, mana orb and experience bar do nothing when clicked.

## Saves, logs and iCloud

- Saves: `~/Library/Application Support/D2R Mobile/save`
- Log: `~/Library/Logs/D2R Mobile/host.log` (the previous run is `host.prev.log`). Please attach it when reporting a problem.
- The older-API build (`D2R Mobile M3.app`) uses a separate pair of directories with M3 in the name.

**iCloud save sync: not available in an App you build from this repository.** The program has the same sync as on iPhone / iPad wired in, but it needs the App to carry the iCloud signing entitlement; an App you build yourself is signed locally without it, so saves stay on this Mac, the log records one line saying iCloud is unavailable and only local saves are used, and the game runs as usual. To bring your phone's progress over, import it by hand as below.

### Importing saves from an iPhone / iPad

Importing is a one-time manual copy: afterwards each side goes its own way and progress diverges; to bring them together later you copy the whole thing again and let one side overwrite the other.

**1. Find the save on the iPhone / iPad**

- Quit the game completely on the phone / tablet first (swipe it away in the app switcher).
- Open the built-in Files app → "On My iPhone" (or "On My iPad") → "D2R Mobile" → the `save` folder inside is the save.

**2. Copy the whole `save` folder, do not pick files**

It holds one folder per character (folder name = character name), a few shared-stash files and a `Settings` folder (game settings including the interface language — after copying, the Mac's game settings become the phone's). The data of the features we added (the extra personal stash pages, the pouch, rift records, the chronicle vault and so on) is in this folder too. The switches on the Custom page are not in it; each device keeps its own.

AirDrop, a cable (the Files tab in Finder) or iCloud Drive all work for getting it to the Mac.

**3. Put it on the Mac**

1. Quit the game on the Mac completely (⌘Q).
2. In Finder press ⇧⌘G, enter `~/Library/Application Support/D2R Mobile` and press Return. (This folder exists only after the game has been started once.)
3. **Back up first**: rename the existing `save` folder there (for example `save backup`); do not delete it.
4. Put the `save` folder from the phone here. Correct looks like this: directly under `D2R Mobile/save/` are the character folders, `Settings` and a few `.d2i` files — no extra level (not `save/save/…`).
5. If `save.work` or `save.new` sit next to it, leave them; the game discards and rebuilds them at launch.
6. Open the game; the phone's characters should appear on the main menu.

Got it wrong or want to go back: quit the game, take the `save` you just put in away, and rename the one from step 3 back to `save`.

The other direction (Mac → iPhone / iPad) uses the same files and mirrors the steps: quit the game on the Mac, copy the Mac's `save` into the game's folder in Files on the phone, replacing the one there (rename the phone's original first as a backup). Keep the App on both sides as current as you can.

Verified: on a Mac, a save placed in the save directory laid out as in step 4 shows its characters on the main menu after launch and can enter a game (one unattended run). The whole route from a real iPhone / iPad through the Files app onto the Mac, the other direction, and hardcore characters have not been operated by a person.

## Quitting and saving

Closing the window, ⌘Q and Quit in the menu all save before quitting, in any state: in game, with the pause menu open, with a panel open, on the loading screen and with the rift panel open were each verified with one run. It waits at most 3 seconds; if saving has not finished by then, the log records a line saying it quit without saving.

After a forced quit or a crash, the system's "reopen windows?" dialog no longer comes up to block the game window on the next launch.

## Known shortcomings

- The five things from the top have not been operated by a person: keyboard keys, a physical gamepad, the resource-folder dialog, ⌘Q and menu clicks, how it feels in the hand. The key mapping was written from the key-code table.
- Clicking empty ground does not walk; key caps on screen are still gamepad letters; long press / double click / drag / buying on items do not take the mouse (see "Mouse" above).
- The free town portal on ZR + Space of a gamepad is not there.
- The quest bar on the left in rifts is not there.
- An App you build yourself has no iCloud save sync; there are no update checks and no sharing of the diagnostic log.
- The older-API build was played on macOS 15 by one player in our test group (Mac mini M4, macOS 15.7, using a package built elsewhere); building it from this repository on a Mac running macOS 15 has not been operated by a person.
