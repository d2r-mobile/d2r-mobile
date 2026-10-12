# D2R Mobile

**An open-source iOS compatibility layer for Diablo II: Resurrected.** It brings the Nintendo Switch release you bought to iPhone and iPad in full: the original game code runs natively, every control is rebuilt for touch, the frame rate holds at 60 fps, and you are in a game one second after the main menu.

This is not a tech demo that "sort of runs". It is a mobile version polished for daily play, from your first character all the way through the endgame.

Platforms: iPhone / iPad (iOS 26 and later) · Game version: Switch release 3.3.94012 · English / 中文 · License: MIT · **Bring your own game files**

中文: [README.zh-CN.md](README.zh-CN.md)

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/hero_en.jpg" width="100%" alt="The game filling the whole edge-to-edge screen of an iPhone: Harrogath on Hell, with the red portal to the Greater Rift">
</p>
<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_boss.jpg" width="49%" alt="Touch combat with the boss health bar pinned to the top">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/ipad.jpg" width="49%" alt="True full screen on iPad">
</p>

| 100% | 60 fps | about 1 s | about 3.8 W |
|:---:|:---:|:---:|:---:|
| The original gameplay, because it is the original program | Steady, with no drops once the device warms up | From the main menu into a game | Whole-device power draw at 60 fps |

## What it is, and what it is not

**It is:**

- The game's own aarch64 program, **executing directly on the device**, with no emulated CPU. iPhone and Switch use the same class of processor; only the handful of instructions that cannot run unchanged under Apple's kernel are rewritten, and every other byte of the program is left exactly as shipped. Every class, every Runeword, Terror Zones, bosses, drop rules and quest flow are identical to the original.
- The platform services the game expects — system calls, the file system, the graphics API, audio, input — provided by a small host written for Apple platforms. The graphics API is reimplemented entirely on Apple's Metal with no emulation layer in between, and the shaders are translated offline from the game's originals.
- A port built for phones, and taken all the way: a complete touch control scheme, edge-to-edge iPhone and true full-screen iPad display, controller support, a power saver mode, save sync, English and Chinese, and a whole set of endgame modes and quality-of-life features the original never had.

**It is not:**

- An emulator, cloud gaming, or streaming.
- **A source of game content. It contains none and distributes none.** There is no game program, art, text, data table or shader in this repository; all of that is generated on your own machine from your own files (see [docs/GAME_FILES.md](docs/GAME_FILES.md)). What the repository does carry is the interface information we recorded to make the port work — shader interface descriptions, the graphics API function table, addresses inside the program and the like — listed item by item in [docs/DERIVED.md](docs/DERIVED.md).
- Online. There is no multiplayer; the game runs fully offline and needs no online account.

## Battle-tested

All **44** iPhones and iPads in the closed beta — 35 devices registered for direct install (25 iPhones and 10 iPads), plus 9 TestFlight accounts with one device each — **completed Normal, Nightmare and Hell with multiple characters**, and the experience was excellent. From an iPhone 14 Pro Max with 6 GB of memory to the latest iPad Pro, not one device fell behind.

| | Models |
|---|---|
| iPhone | 14 Pro Max, 15, 15 Pro, 15 Pro Max, 16 Plus, 16 Pro, 16 Pro Max, 17 Pro Max and more |
| iPad | iPad Air, iPad mini, iPad Pro 11-inch and 12.9 / 13-inch (including M2 and M4) and more |
| Lowest model tested | **iPhone 14 Pro Max (A16, 6 GB): a flawless 60 fps** |

iPhone and iPad share a single install package.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/act1.jpg" width="19%" alt="Act I">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/act2.jpg" width="19%" alt="Act II">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/act3.jpg" width="19%" alt="Act III">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/act4.jpg" width="19%" alt="Act IV">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/act5.jpg" width="19%" alt="Act V">
</p>

## Fast, smooth, and easy on the battery

Measured on a real device (iPhone 17 Pro Max):

| Item | Reading |
|---|---|
| In-game frame rate | **60 fps**, steady, with no drops once the device warms up |
| App launch | 0.15 s |
| To the main menu | about 1 s |
| From the main menu into a game | **about 1 s** |
| Quick Restart, back in a game | about 1.2 s |
| Longest frame when changing acts | about 0.1 s |
| Whole-device power, 60 fps | **about 3.8 W** (in camp, 50% brightness): nearly 5 hours of continuous play on a full charge |
| Whole-device power, power saver mode at 30 fps | **about 1.85 W**, about half the 60 fps figure, for about twice the battery life |

<sub>Measurement conditions: power is whole-device power measured on 2026-10-10 on an iPhone 17 Pro Max, standing in the Act I camp at 50% brightness, with 60 fps and power saver mode measured in the same scene; battery life is calculated from the measured power and the battery capacity, not by playing from full to empty. Launch and game-entry times were measured on the same model in 2026-10; frame rate and act-change readings were taken on the same model on 2026-10-12 while moving through areas and changing acts after entering a game.</sub>

The install package is only a little over 100 MB. The game resources (about 5.6 GB) are placed on the phone separately, and updating the app touches neither your saves nor the resources.

Why it is this fast: the program executes natively, and not a single instruction goes through emulation; graphics go straight to Metal; every shader is translated offline and compiled ahead of time, so nothing is compiled when you enter a new scene. Older chips such as the A16 do not support the game's original compressed texture format, so we built a supplementary texture pack for them, and they run at full frame rate too.

## Redesigned for fingers

This is not a gamepad pasted onto the screen. Every panel and every action was rebuilt for fingers under one design principle: **keep the original interface language, and use gestures instead of adding buttons to the screen**. Text sizes and tap targets are sized for fingers. You know how to play the moment you pick it up, and it stays comfortable over long sessions.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/touch_game.jpg" width="49%" alt="The touch interface: stick and skill buttons">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_belt.jpg" width="49%" alt="In camp: belt with potions, mercenary portrait and minimap">
</p>

- **Move**: a virtual stick with precise diagonals; tap an NPC or the ground to path there automatically; doors, portals and cave entrances are tappable across their whole surface.
- **Fight**: press a skill button to cast, hold to keep casting, **drag to aim at any angle**; nearby monsters are targeted automatically. Two skill sets. Skills on your secondary weapons cast with one tap — the game swaps weapons, casts, and swaps back — and the button shows the charges remaining.
- **Boss health bar**: pinned to the top of the screen, showing the percentage remaining.
- **Items**: tap, double-tap, long-press and drag and drop all track your finger. Double-tap gear to equip or unequip it; drag an item anywhere onto the vendor panel to sell it; on a stack, double-tap takes 1 and long-press takes 3; long-press an empty inventory slot to sort everything.
- **Text**: typed with the system keyboard. Character names can be in Chinese; leave the name blank for a random one.
- **Screen**: on iPhone the 19.5:9 display is filled edge to edge, clear of the notch and rounded corners; on iPad the game fills the entire screen, and the interface centers itself when you open a panel; the 3D picture can be brightened.

### Controller

Connect a controller and press any button: the interface switches to the original controller layout, with button prompts at the bottom of every panel. Touch the screen and you are back in the touch interface instantly. Inventory, stash, skill tree, Chronicle and settings are fully controller-navigable, with the same button mapping as on Switch.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_pad_combat.jpg" width="49%" alt="Combat with a controller connected">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_pad_inventory.jpg" width="49%" alt="Navigating the stash with a controller, button prompts at the bottom">
</p>

## New ways to play, on top of the original

Finishing the original game is only the beginning. Without touching the original rules, we added a full set of endgame content that keeps you farming.

### Greater Rift

Enter through the red portal in Harrogath after completing Hell. There are 100 levels, and the monsters grow stronger with each one; fill the progress bar and slay the Rift Guardian within 15 minutes to clear a level. Every 2 levels cleared grants +1 Skill Point and +5 Stat Points, and the skill level cap rises from 20 to 40. Every 10 levels has a first-clear reward, and clearing level 10 unlocks six new equipment slots for your mercenary.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_panel.jpg" width="40%" alt="The Greater Rift level select panel">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/rift_game.jpg" width="58%" alt="Combat inside a Greater Rift">
</p>

### Endgame Boss Rush, Elysium mode, and the Kill Board

- **Endgame Boss Rush**: unlocks after completing Hell, with 12 stages; stage 11 is Diablo Clone, and stage 12 is the three Uber bosses.
- **Elysium mode** (chosen at character creation): Unique ×5, Set ×3, Runes ×5; the "no drop" chance is cut to a tenth, and bosses always drop as on a first kill.
- **Kill Board**: for every boss, the number of kills, the time of the first kill, how many Unique and Set items it has dropped, and your best and average kill times.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_killboard.jpg" width="49%" alt="The Kill Board">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_mode_select.jpg" width="49%" alt="Choosing Elysium mode at character creation">
</p>

### Storage: a collector's paradise

- **Chronicle stash**: every identified Unique and Set item has its own fixed slot, shared by all characters, searchable, with deposit and withdraw; the Deposit button sends the Unique and Set items in your inventory and Horadric Cube there in one tap.
- **Stash**: widened to 14 columns, with 5 personal pages and 5 shared pages.
- **Trove**: the gem, rune and material pages merged into one, with the Horadric Cube's crafting grid in the middle, so finding things and crafting no longer means flipping between pages. Runes, gems and materials you put into the personal or shared pages are filed here automatically.
- **Pouch**: an extended inventory. Runes and gems found in the field go into it automatically and take up no inventory space.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/trove.jpg" width="49%" alt="The Trove: gems, runes, materials and the crafting grid on one page">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_chronicle.jpg" width="49%" alt="The Chronicle stash">
</p>

### Effortless farming

| | |
|---|---|
| Auto-recast buffs | Buff skills you have learned do not need a slot on the skill bar: when a buff runs out, it is recast the moment you stand still; buffs that come from your secondary weapons (such as Battle Orders from Call to Arms) swap weapons, recast and swap back on their own |
| Potions and belt | Auto-drink has two tiers: a healing potion below 50% Life, a rejuvenation potion below 30%; **your mercenary is fed automatically by the same rule**. Potions on the ground are picked up automatically when the belt has room; double-tap an empty belt slot to refill the whole belt |
| Loot filter | From Nightmare on, the original loot filter is switched on automatically to hide clutter: light filtering in Nightmare, heavy in Hell |
| Auto-pickup | Low runes and gems go straight into the Pouch; the high ones are left for you to pick up yourself |
| Valuable drop alerts | High runes and Unique items raise a light beam and land with a different drop sound; the phone vibrates at one of three strengths — light, medium or heavy — according to value |
| Runeword helper | Long-press a socketed base to list the Runewords it can make and their stats; once you have the runes, socket them all in one tap |
| Minimap guidance | Every map marks its Waypoint and exits; every quest goal is on the map; when a target is off screen, its name is written along the screen edge |
| Chores removed | Free identification, free Town Portal with no scrolls needed, spawning right beside the Waypoint, Quick Restart, refreshable vendor stock |
| Saves | Saved immediately when you switch apps, swipe the app away or quit; iCloud sync across devices (requires signing with a paid developer account) |

Most features can be toggled one by one in game under Options → Custom.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_runeword.jpg" width="32%" alt="The Runeword helper">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_map_goal.jpg" width="33%" alt="The quest goal named under the area name, next to the minimap">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_map.jpg" width="33%" alt="The full-screen map">
</p>

### English and Chinese

The whole game switches between English and Chinese under Options → Custom; after a restart, both the interface and the voice acting are in the language you chose. In English, items, skills and monsters use the original Diablo II wording, and everything we added — Greater Rift, Chronicle, the settings page, controller prompts — is in English too.

<p align="center">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/en_game.jpg" width="58%" alt="Gameplay with the English interface">
  <img src="https://github.com/d2r-mobile/d2r-mobile/releases/download/assets/rift_panel_zh.jpg" width="40%" alt="The same Greater Rift panel in Chinese">
</p>

## How it works

```mermaid
flowchart LR
    G["Your game files<br/>(program + resources)"] --> P["Rewriter<br/>changes only a few instructions"]
    G --> S["Shader translation<br/>original → Metal"]
    P --> H["Host<br/>system services"]
    S --> X["Graphics layer<br/>the graphics API on Metal"]
    H --> A["iOS app<br/>touch and interface"]
    X --> A
```

| Part | Where | What it does |
|---|---|---|
| Program rewriter | `dm/tools/patch_nso.py` | Rewrites only the few instructions that cannot run unchanged under Apple's kernel (thread-pointer registers, system calls, timers) and leaves the rest of the program byte-for-byte as shipped |
| Host | `dm/host` | Loads the rewritten program and provides the platform services it calls |
| Graphics | `dm/gfx` | Implements the game's graphics API on Metal; shaders are translated offline from the game's originals |
| iOS app | `dm/ios` | The app shell, touch controls and interface |
| Verification | `dm/verify` | Checks that the rewritten code behaves exactly like the original, plus the regression gates every change goes through |

**Why you can trust it:** the stability is not luck. The test and verification tooling is part of the project — unit tests for the host and graphics layers, an equivalence check for every rewritten instruction, scripted walkthrough regression for the touch interface, and on-device smoke tests. Start with `make -C dm/host/tests run`, `make -C dm/gfx/tests run` and `dm/verify/regress.sh` (run them inside the Python environment prepared in step 2 below; `regress.sh` also needs `brew install llvm`). How to contribute is in [CONTRIBUTING.md](CONTRIBUTING.md).

## Getting started

Both routes start with **the files of the game you bought** (Switch release 3.3.94012). How to prepare them is in [docs/GAME_FILES.md](docs/GAME_FILES.md).

| | With a Mac | Without a Mac |
|---|---|---|
| Computer | An Apple silicon Mac with Xcode installed | Windows / Linux / a Mac without Xcode |
| Method | A full build from source | Download a "shell" with no game content, use the packaging tool to add your own game content, then install it with a sideloading tool |
| Status | The main route; everything above comes from it | Works end to end on Windows, from making the package to playing on a device, and the game program produced on the two systems is byte-for-byte identical; the one step nobody has tried yet is signing with a sideloading tool — try it and tell us |
| Instructions | The three steps below | [docs/NO_XCODE.md](docs/NO_XCODE.md) |

The Apple account used for signing: a paid developer account gives you everything; a free Apple ID works too (no iCloud save sync, and the app has to be re-signed and reinstalled every 7 days). The full list of dependencies is in [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

### With a Mac: three steps

1. Put your game files in the workspace (default `~/dm-work`, or set `DM_ROOT`):

   ```
   ~/dm-work/data/switch/3.3.94012/Update/ExeFS/    main, sdk, subsdk0
   ~/dm-work/data/switch/3.3.94012/Update/RomFS/    data/…
   ```

2. Prepare the workspace. This checks your tools, rewrites the game program, and translates the game's shaders to Metal. The first run takes a while; later runs skip what is already done.

   ```
   ./setup.sh
   source ~/dm-work/build/cache/dm/venv/bin/activate     # run everything after this inside the Python environment setup prepared; if you changed DM_ROOT, use it in place of ~/dm-work
   ```

3. Fill in your signing details and build the app:

   ```
   cp dm/ios/local.env.example dm/ios/local.env     # set DM_TEAM_ID and DM_BUNDLE_ID; with a free Apple ID also set DM_FREE_ACCOUNT=1 and DM_IOS_UDID, and keep the phone connected while building
   cd dm/ios && DM_DEVICE_FAMILY=1,2 ./run.sh build    # 1,2 = build for both iPhone and iPad; leave it out for iPhone only
   ```

The first time you open the app on your phone, it asks for the "game resource folder" — the 18 resource archives from your game files. Where to put them is in the section "The "game resource folder" that goes on the phone" of [docs/GAME_FILES.md](docs/GAME_FILES.md).

`./setup.sh check` is a health check: it reports the state of this computer, the tools, the game files and the signing configuration item by item, and for everything missing it says where to get it and which command to run next; it changes nothing. If what you have is a whole game package (`.xci` / `.nsp`), `./setup.sh <game package> <prod.keys>` first extracts the game files into place and then carries on (install hactool first). Run `./setup.sh` again after every update to a new commit: it rebuilds the patched binaries and the shader library for the current sources (steps that are already up to date are skipped). It also rewrites three files in the repository that change with your local tool fingerprints (`dm/gfx/tools/dmsh_chain.lock`, `dm/gfx/nvn/dmsh_chain.h`, `dm/gfx/nvn/c1_full.tsv`); that is expected, do not commit them.

### Without a Mac: the shell plus the packaging tool

Download our prebuilt "shell" package (`D2RMobile-shell.ipa`), which contains no game content, from this repository's [Releases page](https://github.com/d2r-mobile/d2r-mobile/releases), add your own game files, make the install package on Windows or Linux, then sign and install it with a sideloading tool. On this route only, because the computer making the package has no Apple shader compiler, the **first launch** on the phone spends about 7 minutes compiling shaders (once, with a progress bar); a package built with Xcode has no such step. The steps, and how far each one has been verified, are in [docs/NO_XCODE.md](docs/NO_XCODE.md).

## Known limitations

| Item | Status |
|---|---|
| Online play | Out of scope; fully offline |
| Game packages in `.nsp` format | Not verified (only `.xci` was tested) |
| Signing and installing with a sideloading tool (Sideloadly, AltStore and the like) | Not verified; open an issue and tell us how it went |
| Making the package on Linux | Not verified (verified on Windows and Mac) |
| The "one-line verdict" and "legacy Chinese name" entries in item notes | These two content tables are not shipped with the repository; you can supply your own, see `dm/ios/tools/annot/README.md` |

## Contributing and feedback

- If you hit a problem, open an issue (there is a template); attaching the file exported by Share Diagnostics in the pause menu helps the most.
- For the items marked not verified, tell us whether they worked or not. Either answer is valuable.
- To change the code: get the test entry points above passing first. Every change has to pass the regression gates.

**Taking it to other platforms:** this approach is not limited to iPhone and iPad. It ports directly to Apple silicon Macs — our daily regression tests run the very same package on a Mac. Android phones use the same class of processor, so the game program executes natively there as well, and the route is fully open: the only parts to replace are the sides of the host and the graphics layer that face the platform. The author has no Android phone at hand, and would love to see someone bring it to Android.

## Documentation by part

| Document | Covers |
|---|---|
| [dm/README.md](dm/README.md) | How the whole thing works, directory layout, common commands, the host's source files |
| [dm/gfx/README.md](dm/gfx/README.md) | The graphics layer and the shader translation chain |
| [dm/ios/README.md](dm/ios/README.md) | The iOS shell, building and smoke testing |
| [dm/verify/README.md](dm/verify/README.md) | What each verification tool checks and what it needs |
| [docs/GAME_FILES.md](docs/GAME_FILES.md) | Game files: which ones you need, how to get them from your own console, where to put them |
| [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) | Dependencies and third-party licenses |
| [docs/NO_XCODE.md](docs/NO_XCODE.md) | Building without Xcode and without a Mac |
| [docs/DERIVED.md](docs/DERIVED.md) | Game-related material in this repository: what is here, what is not, and why |

## Legal

**You must own the game.** This is an independent, non-commercial project. It is not affiliated with, endorsed by, or sponsored by Blizzard Entertainment or Nintendo. Diablo and Blizzard Entertainment are trademarks of Blizzard Entertainment, Inc.; Nintendo Switch is a trademark of Nintendo. Use it only with a copy of the game that you own.

## License

MIT, see [LICENSE](LICENSE). Third-party code keeps its own license in its own directory; see [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
