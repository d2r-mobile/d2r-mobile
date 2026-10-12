# dm/ios — iOS shell

中文：[README.zh-CN.md](README.zh-CN.md)

The iOS app of D2R Mobile: it packs the host (`dm/host`), the graphics layer (`dm/gfx`) and the rewritten game program into one app, and is responsible for starting up, locating the game assets, turning touches into input the game understands, and laying touch controls over the original UI.

Read these first; this document does not repeat them:

- What you need and how to build from scratch: [../../README.md](../../README.md), [../../setup.sh](../../setup.sh)
- Dependencies and versions: [../../docs/DEPENDENCIES.md](../../docs/DEPENDENCIES.md)
- How to prepare the game files and where to put them: [../../docs/GAME_FILES.md](../../docs/GAME_FILES.md)
- The rewriter, the host, the graphics layer, and "Generating assets from your game files": [../README.md](../README.md)

In this document `$DM_ROOT` means the workspace root (default `~/dm-work`). Relative paths in commands are relative to `dm/ios` unless stated otherwise.

## How it works

### What gets packed into the app at build time

iOS does not allow an ordinary app to execute code that was written into memory at run time, so the game program cannot be loaded at startup the way it is on the Mac host. Instead it is compiled into the app's executable at build time:

- `tools/gen_embed.py` turns the rewritten module images (`main`, `sdk`) into an embedded assembly file `Gen/embed.S`, which places them into segments of the executable in their original relative layout. On iOS the host's loader only takes the addresses of these segments and checks the layout; it does not copy code and does not change page permissions.
- `tools/gen_build.py` takes the host's source files, header paths and frameworks from the compile commands that `dm/host/Makefile` actually expands to, and generates `Gen/dm.xcconfig` and `Gen/src/`. The host's `main.c` is compiled in with `-Dmain=dm_main` and is called by the app on its own thread.
- The shader libraries (the iOS versions translated by `setup.sh`), the shader mapping table, the relocation table and gate table used for loading, and the overlay RomFS (see below) ship with the app as resources.

The project file is not kept in the repository: `project.yml` is the input to XcodeGen (a tool that generates an Xcode project from a YAML file), and `run.sh build` regenerates `DmIOS.xcodeproj` and `Gen/` every time.

### Startup

The entry point is in `Sources/main.m`. The app uses the launch argument `-mode <name>` to choose what to run; the default is `boot` (start the game). The sequence in `boot` mode:

1. Raise the process's soft limit on file descriptors to 4096 (once in a game, the number of files the game has open at the same time comes close to the iOS default limit).
2. Open the log: `Documents/boot.txt`, flushed to disk line by line; the log of the previous launch is renamed to `Documents/boot.previous.txt`. What the host writes to standard error also goes into this file.
3. Check whether the GPU supports BC-compressed textures. Models that do not support them need the matching ASTC supplement pack (produced by `dm/tools/astc_pack.py`); if it cannot be found, a dialog explains this and the game is not started. How to make the supplement pack and get it onto the phone is in "Models without BC texture support" below.
4. Locate the game assets (next section). If found, prepare the environment: `boot_prepare` sets the environment variables the host reads, such as `DM_ROMFS` (asset root), `DM_ROMFS_OVERLAY` (overlay), `DM_SAVE` (save directory `Documents/save`), `DM_TEMP` and `DM_MSL_DIR` (shader libraries), changes the working directory to `Documents`, and calls `dm_host_preload_start` so that the host starts loading on a background thread, in parallel with UIKit's startup.
5. Once the scene is set up, the root view is replaced with the Metal view returned by `front_ios_view` (`Sources/front_ios.m`, the iOS implementation of the host's front-end interface `dm/host/front.h`).
6. `boot_thread`, on a thread with a 64 MB stack, first does the iCloud save sync (`Sources/cloud_ios.m`) and then calls `dm_main` to enter the host; from then on the game is running.

Lifecycle events such as going to the background, returning to the foreground and scene disconnection are forwarded by the `SceneDelegate` in `main.m` to `front_ios.m`; when the app terminates, `dm_host_shutdown` is called so the host can shut down in order.

The remaining modes are for diagnostics: the three probes `anon`, `mapjit` and `file` try "execute an anonymous page after writing to it", "MAP_JIT mapping" and "map a file inside the sandbox as executable" respectively, to confirm the restriction described above; the `tsd` probe tries the fixed slots of thread-local storage; `msl` loads the shader libraries inside the app one by one; `blank` runs only a blank Metal clear-screen page (a heat baseline); `thermal` reads the system thermal level once and exits.

### Locating the game assets

`romfs_locate` in `main.m` searches in the order below, and the first match wins; every location is checked with `dm_romfs_validate` (`dm/host/romfs_validate.c`) to confirm that all 18 archive files are present and that their byte counts match:

1. `Library/Application Support/romfs` in the app container. This is the location used during development; push the files in from the Mac:

   ```bash
   xcrun devicectl device copy to --device <device identifier> \
     --domain-type appDataContainer --domain-identifier <bundle ID> \
     --source $DM_ROOT/data/switch/3.3.94012/Update/RomFS \
     --destination 'Library/Application Support/romfs'
   ```

2. A folder inside the app's `Documents` (visible in the Files app) that directly contains the archive files. The folder can have any name and can be nested a few levels deep (up to 4); if it is incomplete, the missing files are listed on screen.
3. An Apple-hosted asset pack (Background Assets, large file packs downloaded along with the app install), with the pack name `romfs`. `RomfsDownloader/` is the download extension it requires.
4. None of the above: the host is not started, and the asset screen (`AssetVC` in `main.m`) is shown with the progress or the reason; once the assets are in place, tapping retry enters the game on the spot.

Only those 18 archive files are checked: the configuration file used is the one in the overlay bundled with the app, and the game does not read the keys or the archive list, so none of them need to be copied.

`Sources/asset_dl.m` implements a path that downloads the assets directly from an HTTP address (the manifest is generated by `tools/asset_manifest.py`). The `asset_pointers.txt` in the repository registers no address, so unless `DM_ASSET_BASE` is also passed at build time, this path does not take effect.

### Turning touches into input

The Switch version of the game only recognizes two kinds of input, the gamepad and the touch screen. The shell translates finger actions into these two, plus direct clicks on the game UI.

- **Collection**: `DmMetalView` in `front_ios.m` receives all `UITouch` objects in one place.
- **Classification**: `Sources/dm_touch_controls.c` is a pure C state machine with no dependency on UIKit (`dm_tc_*`): which finger belongs to the stick, a skill button, a belt slot or the UI, what a tap / double tap / long press / drag and drop each count as, and as output the gamepad buttons that should currently be pressed and the values of the left and right sticks. It can be unit-tested without a device (see "Unit tests").
- **Publication**: on every state change, `pushFrom:phase:` of `DmMetalView` hands the results to the host together: `dm_touch_set` (raw touch points, for the game to read as the touch screen), `dm_cursor_set_screen` (UI cursor position), `dm_input_set` (gamepad buttons and sticks). The host applies them when the game reads the gamepad (`dm/host/hle_hid.c`, `dm/host/dm_cursor.c`).

There are three cases depending on which UI the game is currently in. Switching between them is driven by the UI messages the game itself emits (`dm_tc_navigation`), not by guessing:

| Case | How the shell handles touches |
|---|---|
| Front-end pages (main menu, character select, character creation) | The stick and skill buttons are not shown; touch points become mouse clicks on the game UI through `dm_ui_touch` |
| In game, no panel open | The stick on the left, the skill buttons on the right and the bottom bar buttons each become gamepad buttons and stick values; a tap in the world (a monster, an item name, an NPC, a chest) is handed to the host through `dm_ui_world_tap` to resolve the target, and a tap on empty ground sends no input at all |
| In game, with a panel or dialogue open | The finger taps the UI directly: on an item slot, single tap to see the description, double tap to use, long press, drag and drop; tabs, buttons, and tapping outside a panel to close it. What was hit is answered by the host according to the game's own panel tree, and the front end decides from that whether to send a mouse click or a gamepad button |

### How the touch controls relate to the original UI

The principle is to use the game's own UI widgets as far as possible; the shell only adds the layer the game does not have:

- The life orb, mana orb, belt, town portal and feature entry of the **bottom bar** are the game's original HUD widgets; only their positions and scale are changed, through a layout file in the overlay. The experience bar and the numbers on the slots are drawn by the shell. See [NativeHUD/README.md](NativeHUD/README.md) for details.
- The **stick and skill buttons** are drawn on top of the game picture with UIKit by `Sources/DmTouchOverlay.m`. This layer only draws and does not receive touches. The icons on the skill buttons come from the game's skill atlas, and which skill is currently bound is read, read-only, from the running game by `Sources/dm_touch_game_bridge.c`.
- The **panels** are all the game's original panels. The overlay moves the gamepad button hints of the original layout off screen, adds a close button to the panels, and replaces gamepad wording in hint texts with touch wording (`tools/touch_layout.py`, `tools/touch_strings.py`).

### Overlay RomFS

The overlay is a small RomFS that ships with the app: when the host reads a game file it looks in the overlay first, and only if the file is not there does it read the full game assets (`DM_ROMFS_OVERLAY`, `dm/host/hle_fs.c`, `dm/host/hle_tact.c`). This way UI layouts and textures can be changed without touching the player's game files.

`run.sh build` generates it by calling `tools/gen_romfs_overlay.py <original RomFS> Gen/romfs_overlay`: it reads the original layouts and strings from your game files, applies the changes kept in the repository, and writes a new `.build.config` plus one new data archive.

- The repository stores only "what was changed", not the original content: layouts use structural patches (`tools/layout_patch.py`), and string tables are replaced by key name, with the checksum of the original text used to verify the version.
- New textures are written to a few old texture paths the game no longer reads, registered in `dm/data/casc_overlay/borrowed_paths.tsv`.
- Some feature switches only take effect with a different overlay: the script produces one overlay for each combination of these switches under `Gen/romfs_overlay_variants/`, and at launch the app picks the one matching the switches the player has turned on.
- `Gen/romfs_overlay_astc*` is additionally produced only when `DM_ASTC_PACK` (the ASTC supplement pack directory) is set at build time, for models that do not support BC textures.

### Models without BC texture support (A16 and earlier chips)

The GPUs of these models do not support the BC compression formats the game's textures use. They need a separate ASTC supplement pack, the matching overlay included when the app is built, and the pack copied onto the phone. The pack is generated on a Mac from your own game files and is not in the repository. The full procedure is in the header of `dm/tools/astc_pack.py`; these are its steps (run from the repository root; `D` is an output directory of your choice):

1. Get the encoder astcenc (ARM's open-source encoder, built from source) and give its path with `--astcenc` or the environment variable `ASTCENC`.
2. Transcode: `python3 dm/tools/astc_pack.py <Update/RomFS directory> D/work`.
3. Write the data archive: `python3 dm/tools/casc_overlay.py pack <Update/RomFS directory> D/pack D/work/repl.tsv 0-8`.
4. Finalize: `python3 dm/tools/astc_pack.py finalize D/pack`. It computes sizes and checksums and creates, under `D/pack`, the folder meant for the phone (containing `manifest.json` and `data/`).
5. Build the app: set `DM_ASTC_PACK` to the absolute path of `D/pack`, then run `cd dm/ios && ./run.sh build` as usual.
6. Copy the whole folder created in step 4 into this app's folder on the phone (with the Files app or Finder, next to the game asset folder).

## Directory layout

| Path | Contents |
|---|---|
| `run.sh` | Build, install to device, launch by mode (see below) |
| `project.yml` | XcodeGen project definition: three targets (the app, the asset download extension, the UI tests) and the signing capabilities |
| `local.env.example` | Sample local configuration |
| `Sources/main.m` | App entry point: modes, logging, locating assets, starting the host, lifecycle |
| `Sources/front_ios.m` | iOS implementation of the host front end: Metal view, touch collection and publication, touch scripts, test switches such as screenshots |
| `Sources/dm_touch_controls.[ch]` | Touch classification state machine (pure C) |
| `Sources/DmTouchOverlay.[hm]` | Self-drawn controls such as the stick and skill buttons |
| `Sources/dm_touch_game_*.c` | Reads the skill bindings from the running game (read-only) |
| `Sources/asset_dl.[hm]`, `cloud_ios.m`, `dm_update.m`, `dm_sharelog.m` | Asset download, iCloud save sync, version notice, sharing the diagnostic log |
| `Sources/TouchArt/` | Art for the self-drawn controls. The repository holds only the stick image and the source manifest of the skill atlas (original paths and checksums); the skill atlas itself is generated from the game files at build time |
| `NativeHUD/` | Bottom bar documentation and the self-drawn close button image |
| `RomfsDownloader/` | Download extension for the Apple-hosted asset pack |
| `TouchSmokeTests/` | XCTest UI tests (real touches, lifecycle) |
| `docs/` | Specification files (JSON) for the touch layout and skill bindings. The generation scripts and unit tests reference them by checksum; do not modify them |
| `tests/` | Unit tests (see below) |
| `tools/` | Build-time generation scripts (`gen_*.py`), smoke test, regression and measurement tools |
| `tools/uireg/` | Scripted regression of the touch UI, see [tools/uireg/usage.md](tools/uireg/usage.md) |
| `Gen/`, `DmIOS.xcodeproj/` | Generated at build time, not kept in the repository |

## Local configuration: local.env

Signing information and device IDs are not kept in the repository; they go in a local configuration file:

```bash
cp local.env.example local.env
```

| Variable | Meaning | When it is needed |
|---|---|---|
| `DM_TEAM_ID` | Developer team ID | Building the app |
| `DM_BUNDLE_ID` | The app's bundle ID, which you choose yourself (reverse-domain style). The app group and iCloud container are derived from it: `group.<bundle ID>`, `iCloud.<bundle ID>` | Building the app, all smoke test and regression scripts |
| `DM_DEVICE` | The real device's devicectl device identifier (the Identifier column of `xcrun devicectl list devices`) | Installing to the device and the real-device scripts |
| `DM_IOS_UDID` | The real device's hardware UDID | The battery check of the real-device smoke test, `tools/lifecycle.sh`, `tools/xtrace.sh` |
| `MINI_SSH` | The ssh target of a second Mac dedicated to running tests | Only used by `dm/tools/l3run.sh` and `dm/tools/mini_sync.sh`; leave it empty if you have no second Mac |

The scripts read the configuration through `tools/local_env.sh`, searching in this order and **reading only the first file that exists, without merging**: the file pointed to by the environment variable `DM_LOCAL_ENV` → `dm/ios/local.env` → `~/.config/dm-ios/local.env`. The last location is convenient for sharing between several worktrees on the same machine; note that once a worktree has a `local.env`, the shared one is not read at all.

## run.sh

```bash
./run.sh build              # generate Gen/ and the project, compile, sign; does not touch the device
./run.sh install            # install the app from the last build to DM_DEVICE
./run.sh <mode> [limit_seconds=60]  # launch on the real device with -mode <mode>, pull back Documents/<mode>.txt and print it
```

Prerequisites: `../../setup.sh` has finished (the rewritten program and the iOS shader libraries are ready), and `DM_TEAM_ID` and `DM_BUNDLE_ID` are filled in in the local configuration.

`build` does the following in order: generate the assets derived from the original from the game files (`tools/gen_assets.py`, skipped if the inputs have not changed) → generate the host's three table files (`dm_vault_tab.h`, `dm_hud_exp.h`, `dm_bright_levels.inc`) → `gen_build.py`, `gen_embed.py` → copy the shader libraries and the various manifests into `Gen/` → generate the overlay → `xcodegen generate` → `xcodebuild` (Release, automatic signing). These steps call `python3` and must be run inside the Python environment created by `setup.sh` (Pillow, numpy and the rest are installed there): run `source ~/dm-work/build/cache/dm/venv/bin/activate` first (if you set `DM_ROOT`, use it in place of `~/dm-work`). The build log is at `$DM_ROOT/run/ios/build.log`, and the app is at `<derived data directory>/Build/Products/Release-iphoneos/DmIOS.app`.

`install` and launching by mode first take the device lock (`tools/devlock.sh`, lock directory `$DM_ROOT/run/ios/device.lock`), which guarantees that only one script is operating the phone at any moment.

Commonly used environment variables:

| Variable | Purpose | Default |
|---|---|---|
| `DM_OUT` | Directory of the rewrite output | The output of `../tools/product.sh path touch` |
| `DM_IOS_DD` | Xcode derived data directory | `$DM_ROOT/run/ios/dd` |
| `DM_ROMFS_SRC` | Original RomFS (`Update/RomFS`) | `$DM_ROOT/data/switch/3.3.94012/Update/RomFS` |
| `DM_GEN_ONLY=1` | Only generate `Gen/` and the project, do not compile | Unset |
| `DM_ASTC_PACK` | ASTC supplement pack directory; the matching overlay is produced only when it is set | Unset |

The test save inside the app is also brought in at `build` time: if `$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/` exists, it is copied into the app. When launched with the environment variable `DM_SAVE_FRESH=warlock`, that run uses a fresh copy of it, placed in `Documents/save.test`, and does not touch the real save in `Documents/save`. `DM_SAVE_FRESH=empty` starts from an empty save; `DM_SAVE_TEST=1` keeps using the `Documents/save.test` left by the previous run.

## Smoke test

A smoke test really launches the app and goes through a short stretch, to confirm that it starts, can enter a game, and has sound. The repository has two sets with the same criteria: one runs the iOS package directly on a Mac, the other runs on a real device.

### Step one: generate the smoke test save

```bash
tools/make_fixture.sh
```

The first stage of the smoke test is "load a save and enter the game", which needs a save. This script launches the iOS package on the Mac from an empty save, uses touch scripts to create two characters one after the other, enters the camp once with each and saves and exits, then collects the result into `$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/`. After that, apps built by `run.sh build` include it.

- It only needs to be done once; if the target directory already exists it is not overwritten, so delete that directory first if you want to redo it.
- Along the way it clones your game assets into this app's container on the Mac (an APFS clone, taking no extra space), so the Mac smoke test afterwards can use them directly.
- The save exists only in your workspace and is not kept in the repository.

### Running the iOS package on a Mac

```bash
tools/smoke_mac.sh 1        # the argument is how many times "load a save and enter the game" is repeated
```

An Apple silicon Mac can run an iOS arm64 package as is. What the script does:

1. Build: `run.sh build` only does the generation, then it compiles with "Designed for iPad" as the destination, with the derived data placed in `.iosdd_mac/` at the repository root (if the source was not obtained with git clone, in the directory the script is run from).
2. Wrap: opening the build product directly reports a wrong format, so it has to be wrapped as `.iosdd_mac/DmMacSmoke.app/Wrapper/DmIOS.app`, plus a `WrappedBundle` symlink.
3. Load a save and enter the game N times: each time it launches with `DM_SAVE_FRESH=warlock` and a key script (main menu → character select → enter the game), and through the app's test log port (`DM_LOG_PORT`, read by `tools/logport.py`) waits in turn for the main menu, entering the game, and the loading screen being taken down.
4. One player-path run: without `-mode`, without the watchdog and the save variables, equivalent to the player tapping the icon; after entering the game it opens the pause menu and then sits idle. It requires that, in the log, the audio output is really producing sound, the player state can be read, and there are no lines containing `【红】` (red).
5. One touch player-path run: starting from an empty save, using only the touch script `tools/smoke_touch.txt` to create a character and enter the game; tapping empty ground three times does not move the character, and tapping the waypoint underfoot opens the waypoint panel.

The last line is `SMOKE-MAC OK <passes>/<count> 用时 … 秒` (`用时 … 秒` = took … seconds) or `SMOKE-MAC RED …`. The result directory is `$DM_ROOT/run/ios/smoke_mac/<time>_<pid>/`, and every run also appends one record line to `$DM_ROOT/run/ios/smoke_ledger.tsv`.

Key points:

- The game assets are read from this app's container on the Mac (`Library/Application Support/romfs`). When the container has none, the script clones them from the directory `SMOKE_MAC_ROMFS` points to; this variable must point to the `data` directory of the RomFS, for example `SMOKE_MAC_ROMFS=$DM_ROOT/data/switch/3.3.94012/Update/RomFS/data`. If you have run `make_fixture.sh` first, you can ignore this item.
- A given bundle ID can have only one instance on one Mac. If the script finds an existing instance before starting, it reports red and does not terminate it for you. On the same Mac, the Mac smoke test, the touch-screen cases and the UI regression share one lock, `$DM_ROOT/run/ios/macsmoke.lock`, and queue automatically.
- By default a set of "real-device profile" parameters is applied (screen resolution and left/right safe areas), so that layout and touch-point conversion match the phone; `DM_MAC_DEVICE_PROFILE=0` switches back to running as is in the Mac window.
- While the package is running, the system mounts its `Wrapper` directory. Deleting this directory or replacing it wholesale at that time leaves a stale mount point that cannot be unmounted; that path can no longer be opened from then on, and only a reboot clears it. So do not touch the package while it is running.

Tunables:

| Variable | Purpose |
|---|---|
| `SMOKE_MAC_HOST` | Default `local`, everything is done on this machine. When set to an ssh target (`user@host`), this machine builds and wraps, transfers the package to that Mac to run, and pulls the results back. This is intended for a second Mac dedicated to running tests; without one there is no need to set it |
| `SMOKE_MAC_PKG_ONLY=1` | Only build and wrap, do not run; the last line prints the path of the package |
| `SMOKE_PLAYER=0`, `SMOKE_TOUCH=0` | Skip the player path / the touch player path |
| `SMOKE_KEYS` | Use a different key script. Such a run is marked `反例` (counterexample) in the record, and the last two stages are not run |
| `SMOKE_MAIN_MENU_TIMEOUT`, `SMOKE_GAME_SCREEN_TIMEOUT` | Limit in seconds for waiting for the main menu / waiting for the loading screen to be taken down, default 40 / 30 |
| `SMOKE_UIREG=1` | After the smoke test passes, additionally run one UI regression run in the background, see [tools/uireg/usage.md](tools/uireg/usage.md) |
| `SMOKE_OWNER` | The name shown in the queue and in the record |

What the Mac smoke test can verify: the shell starts, asset location, entering a game, lifecycle, sound output. What it cannot verify: installation and signing, real fingers, the phone's heat and GPU time.

### Real-device smoke test

```bash
# run from the repository root
dm/ios/tools/smoke.sh 3
```

Prerequisites, in addition to those of the Mac smoke test:

- The local configuration has `DM_DEVICE` and `DM_IOS_UDID`, and the phone is connected by cable and has trusted this Mac.
- The command-line tools `timeout` (GNU coreutils) and `iproxy`, `ideviceinfo` (libimobiledevice) are in `PATH`.
- The game assets have already been pushed to the phone (the command in case 1 of "Locating the game assets").
- A smoke test save already exists (`make_fixture.sh`).

The script first reads the phone's battery level (below 30% it refuses to start and leaves a pause marker, and only lets runs through again once charged to 60%), takes the device lock, then builds (derived data in `.iosdd/` at the repository root; if the source was not obtained with git clone, in the directory the script is run from), installs to the device, and launches the given number of times. The log likewise goes through the app's test log port and is read back through `iproxy`'s USB port mapping. The criteria are the same as the first two stages on the Mac (load a save and enter the game N times + one player-path run), and the last line is `SMOKE OK <passes>/<count> 用时 … 秒` (`用时 … 秒` = took … seconds) or `SMOKE RED …`. The result directory is `$DM_ROOT/run/ios/smoke/<time>/`, and the record is written to the same `smoke_ledger.tsv`.

## Touch scripts

The "touch script" used in the smoke tests and the regression is a text file in which every line is "seconds action arguments", executed by the app according to time. Scripted fingers go through the same `touchesBegan / Moved / Ended` path as real fingers, and the classification and publication afterwards are exactly the same.

- Usage: put the script into the app's `Documents` and launch with `DM_TOUCH_SCRIPT=<file name>`. By default timing starts from first entering a game; with `DM_TOUCH_SCRIPT_START=menu` timing starts from when the main menu appears.
- The actions include `down` / `move` / `up` (finger), `tap_ui` (tap by UI widget name), `tap_unit` (tap a certain kind of unit in the world), `walk_to`, `type`, `shot` (screenshot), `note`, `pos`, `await` and others; for the full format see `touch_script_start` in `Sources/front_ios.m`.
- A position can be written as point coordinates or as `@widget name`. With `DM_TOUCH_REF=<width>x<height>`, bare coordinates are converted from that reference screen to the current screen.

To run a single touch script on its own: on a Mac use `tools/touch_e2e_mac.sh <script> <result directory> [seconds]`, on a real device use `tools/touch_e2e.sh` (same arguments).

## Unit tests

```bash
make -C dm/ios/tests test
```

No device is needed, and the app is not launched. Contents:

- The behaviours of the touch classification state machine `dm_touch_controls.c` (`t_touch_controls.c`, `t_skill_aim.c`, `t_joy_travel.c`, `t_portal_sequence.c`).
- Parsing and reading of the skill bindings (`t_native_binding.c`, `t_game_bridge.c`, `t_binding_*.c`, `t_native_hud_*.c`), using the two fixtures in [tests/fixtures/native_binding/](tests/fixtures/native_binding/README.md). The public repository does not ship these two fixtures: without them, the three tests that read them are skipped automatically and the rest run as usual.
- The skill atlas index (`t_native_art.c`, `test_native_art_capture.py`). This item needs the game files on your machine: the skill index `Sources/dm_touch_native_art.c` is not kept in the repository, and when it is missing it is generated on the spot from `DM_ROMFS_SRC` (default `$DM_ROOT/data/switch/3.3.94012/Update/RomFS`).
- A few Python scripts that check the wiring of the touch path in `front_ios.m` and the format of the diagnostic log.

Under `tools/` there are also self-tests that come with a few of the scripts: `devlock_test.sh`, `devlock_max_test.sh`, `devlock_locale_test.sh` (device lock), `devbattery_test.sh` (battery check).

## Other scripts in tools/

| Script | Purpose |
|---|---|
| `gen_assets.py` and the `gen_*.py` and `native_art/generate.py` it calls | Generate the assets derived from the original from the game files, described in [../README.md](../README.md) |
| `check_hud_gen.sh` | Checks that generated files have not been committed to the repository; when the game files are present on your machine, actually generates the bottom bar once |
| `devlock.sh`, `devbattery.sh`, `local_env.sh`, `logport.py` | Common parts referenced by other scripts: device lock, battery check, local configuration, log port |
| `touch_e2e.sh`, `touch_e2e_mac.sh` | Run one touch script and collect the log and screenshots |
| `charsel_retap.sh` | Touch regression of the character select page (iOS package on a Mac) |
| `lifecycle.sh` | One round of lifecycle testing on a real device: go to the background and come back to the foreground |
| `screen_check.py`, `fullscreen_check.py` | Check orientation, black borders and stretching from whole-window screenshots |
| `bootprof.sh`, `heat_ab.sh`, `gpu_read.sh`, `xtrace.sh` | Real-device measurements: startup timeline, heat comparison, GPU time, Instruments command-line recording |
| `asset_manifest.py` | Generates the manifest for the direct asset download path |
