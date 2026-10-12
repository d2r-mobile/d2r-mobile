# uireg — scripted regression of the touch UI

中文：[usage.zh-CN.md](usage.zh-CN.md)

Taps through the touch UI item by item and outputs one "expected → actual result" line per item. What is under test is the iOS package itself, driven only by touch scripts (which take the same path as real fingers, see "Touch scripts" in [../../README.md](../../README.md)); the criteria read the log of that run.

**About saves first**: the whole regression suite is written against one specific multi-character save. This save is not provided with the repository and cannot be distributed. The smoke test save that can be generated from the repository is only enough for the smoke test. See "Which saves are needed" below; without that save, the in-game state and tap positions of most runs do not match.

## Files

| File | Purpose |
|---|---|
| `uireg.py` | All cases and criteria: generates the touch script of each run, calls the runner script, reads the log and produces the result table |
| `run_mac.sh` | Runner script (default): runs the iOS package on a Mac, on this machine |
| `run_dev.sh` | Runner script: runs the already installed package on a real iPhone |
| `uireg_pick.py`, `affected.tsv` | Pick the affected runs according to the changed files |

## Prerequisites

1. Being able to run the iOS package on a Mac: first get `tools/make_fixture.sh` and `tools/smoke_mac.sh 1` working once, following "Smoke test" in [../../README.md](../../README.md). After that this machine has the wrapped package (`.iosdd_mac/DmMacSmoke.app` under the repository root; if the source was not obtained with git clone, it is under the directory the smoke test script was run from), this app's container, and the game assets inside the container.
2. The local configuration `dm/ios/local.env` has `DM_BUNDLE_ID`.
3. Some criteria read screenshots and need `python3` to be able to `import PIL` (Pillow). It still runs without it; those items report `未判` (not judged).

All steps can be done on this machine alone; no second Mac is needed.

## How to run

```bash
# run from the repository root
python3 dm/ios/tools/uireg/uireg.py iphone            # all runs
python3 dm/ios/tools/uireg/uireg.py iphone b          # only run b
python3 dm/ios/tools/uireg/uireg.py iphone a c e      # several runs
```

The first argument is the screen profile:

| Screen profile | Meaning | Which package is used |
|---|---|---|
| `iphone` | iPhone aspect ratio | `UIREG_APP`, default `.iosdd_mac/DmMacSmoke.app` under the repository root (if the source was not obtained with git clone there is no default and you have to give it) |
| `ipadfull` | iPad true full screen (the default picture on iPad: 3D fills the whole screen, and the UI is a 16:9 block anchored to the bottom) | `UIREG_APP_IPAD`, must be given |
| `ipad` | iPad 16:9 with borders | Same as above |

One run = launch the app once, go through one script, exit. Runs are independent of each other and can be run individually. Runs are serial with respect to each other: a given bundle ID can have only one instance on one Mac, and the runner script uses the lock `$DM_ROOT/run/ios/macsmoke.lock` to queue with the Mac smoke test.

### How to read the results

- After each run finishes, one line is printed: wall-clock time, timing segments, number of red lines in the log.
- The last line: `# 共 N 项：通 A、不通 B、未判 C；合计墙钟 … 秒；表 <path>` (N items in total: A passed, B failed, C not judged; total wall-clock … seconds; table `<path>`), followed by every item that did not pass or was not judged.
- The result table: `$UIREG_OUT/tables/<screen profile>_<MMDD_HHMM>/表.md`.
- The artifacts of each run: `$UIREG_OUT/pw/<prefix>_<screen profile>_<run name>/`, containing `boot.txt` (the log of that run), `script.txt` (the touch script actually used), screenshots, and `timing.txt`.

There are two kinds of `未判` (not judged): the item only left a screenshot and has no log criterion; or the criterion has to read a screenshot but cannot read it. Both need a person to look at the picture before a conclusion can be drawn, and are not treated as passed.

The criterion of each item only looks at the part of the log after that item's marker. What counts as a red line is not only lines containing `【红】` (red) but also `FATAL`, the watchdog, GPU command buffer errors and so on; the regular expression is `LOGRED` in `uireg.py`.

## Which saves are needed

### Standard save

Most runs launch with `DM_SAVE_FRESH=warlock`: the app makes a fresh copy of its bundled standard save and uses that, without touching the real save. The standard save is brought into the app from `$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/` when the package is built.

The script assumes that the character select list of this save has four characters, and each run selects by row number:

| Row | The character the script assumes | Examples of runs that use it |
|---|---|---|
| 1 | Level 76 Paladin | `i`, `me1`, `me2`, `sl1`, `rf1` |
| 2 | Level 1 Warlock (this row is selected by default) | Most runs |
| 3 | Another character | `e` (only selects it on the character select page), `wd1` |
| 4 | Level 88 Sorceress, with a mercenary | `f`, `q`, `tp`, `cb`, `pg1`, `bdm`, `rf2`, `me3` |

Many tap positions and criteria also depend on things these characters already have (scrolls in the inventory, equipment worn, stash contents, waypoints already opened).

**This four-character save is not in the repository; you need to provide your own.** The smoke test save generated by `tools/make_fixture.sh` has only two newly created characters and is placed in the same directory; which runs can pass when the regression is run with it has not been verified. To run the whole suite, you have to prepare a save that satisfies the table above yourself, put it in that directory, and rebuild the package.

The items, gold and positions each run needs do not come from the save; they are given on the spot after entering the game by test switches (`DM_TEST_GIVE` and others, written in the definition of each run in `uireg.py`), and the random seed and game time are fixed, so the same save yields the same in-game state on every run.

### Dedicated saves

The runs below do not use the standard save but swap in a dedicated save instead. These are likewise not in the repository; you need to provide your own. The directory names are hard-coded in the script and have no meaning of their own:

| Directory name | Runs that use it | Contents the script assumes |
|---|---|---|
| `save.pw_rune_stack` (the name can be changed with `UIREG_STACK_SAVE`) | `d`, `t`, `u` | Warlock; the rune page of the stash has one stack of 12 runes |
| `save.rg_act4done` | `g1`–`g4` | A character that has completed Act IV |
| `save.rg_act4` | `ex` | A character in Act IV with the waypoint opened |
| `save.rg_joejoe` | `tb`, `dz` | A character in Act II; the script goes from the waypoint in town to the Canyon of the Magi; `dz` also needs the quest staff in the inventory |
| `save.sl_player` | `vf1` | A character with items in both the personal stash and the shared stash |

Where to put them: place the save directory under the `Data` directory of this app's container on the Mac (at the same level as `Documents`); the directory must be writable. Before such a run starts, `run_mac.sh` moves the existing `Documents/save` in the container aside and puts in a copy of the dedicated save, then moves it back when the run is finished; should it fail to move it back, it reports red and states where the original save is.

When the corresponding directory does not exist, that run fails immediately and states in `fail.txt` which save is missing, without affecting other runs.

## Which runs exist

A run name is the first argument of `G("…")` in `uireg.py`; below they are grouped by content. For exactly what each item taps and what it judges, the definition of that run in `uireg.py` is authoritative.

| Run | Contents |
|---|---|
| `e` | Character select page, character creation, naming (creates a new character and enters the game) |
| `v` | Create a character after ticking the additional mode on the character creation page, enter the game, open the stash and look at the shared page |
| `a` | Swapping gear in equipment slots, bottom bar and panel tabs, minimap, stick, casting skills in the wild, pause menu and settings page |
| `b` | NPC dialogue menu, buying and selling on the merchant page, single tap / drag and drop / double tap / long press in the inventory |
| `c` | Stash drag and drop and tabs, gold amount box, tapping outside the box while several kinds of dialog are open |
| `s`, `sb` | Put a rune into the stash and take it out again; tap every button of the stash one by one |
| `d`, `t`, `u` | Taking items from a stacked slot: the basic gesture, with the Horadric Cube in the inventory, with only one free slot left in the inventory (dedicated save) |
| `g`, `h` | Skill binding; the `综合技能` (general skills) page of the skill tree |
| `i`, `f`, `q`, `bdm` | Mercenary: hiring and reviving, inventory drag and drop with a mercenary, double-tapping a belt potion to feed the mercenary |
| `j`, `k` | Socketing; making one runeword |
| `rw1`, `rw2`, `rwp`, `rwd` | Runeword list: opening by long press, paging and details, one-tap crafting |
| `m`, `n`, `p`, `ra` | Merchant page: dragging onto the merchant panel to sell, long press on the shelf to buy in bulk, long press on a potion to refill the belt, repair all |
| `me1`–`me4` | Two-row / three-row / four-row dialogue menus when different characters face the same NPC |
| `sl1`, `sl2`, `sl3` | Imbuing, adding sockets, personalizing. `sl2` and `sl3` are marked in the script comments as drafts that have not actually been run yet |
| `cb` | With the Horadric Cube page open, double tap to send items into / out of the Horadric Cube |
| `bd`, `ba` | Belt: double tap on an empty slot to fill up, automatic belt refill when opening the inventory |
| `pk`, `g1`–`g4`, `ex`, `tb`, `tp`, `dz` | Tapping things in the world: the tappable range of a chest, the upper, middle and lower sections of a portal, exits, the town portal, the quest pedestal (most need dedicated saves) |
| `wp` | Entering the game lands next to the town waypoint |
| `rf1`–`rf3` | One-tap restart from the pause menu, restarting three times in a row per run |
| `pg1`, `pg2` | Paging through the personal stash |
| `vt1`, `vf1`, `vf2` | The sixth tab of the stash (the collection page added by the shell): deposit, withdraw, search, switching back and forth between tabs |
| `kba`, `kbb` | Kill statistics panel: tap each widget one by one; look at the statistics after kills |
| `wd1`, `wd2` | No-progress watchdog: no false positive when returning to the front end and sitting idle; it still triggers when nothing moves at all after launch |
| `dc` | Death in the wild → tap `继续` (Continue) on the death screen → revive in town |
| `top` | Exists only in the `ipadfull` profile: the minimap and area name at the true top of the screen, and the title bars of the panels |

The scope when no run name is given:

- `iphone`, `ipadfull`: everything in the table above (`top` only in `ipadfull`).
- `ipad` (the bordered profile is not the default picture on iPad): runs only the part related to the screen aspect ratio, automatically skipping `d g h j k t u v m n p ra q pk g1–g4 ex tb tp`, and skipping the casting-in-the-wild part within run `a`. They can still be run when named explicitly.
- The tap positions of `g1`–`g4` have only been measured for the two sets `iphone` and `ipadfull`.

## Environment variables

| Variable | Meaning | Default |
|---|---|---|
| `UIREG_RUNNER` | Runner script | `run_mac.sh` in the same directory |
| `UIREG_OUT` | Output root directory | `$DM_ROOT/run/uireg` |
| `UIREG_APP` | The package for the `iphone` profile (`<name>.app`, containing `Wrapper/DmIOS.app`) | `.iosdd_mac/DmMacSmoke.app` under the repository root |
| `UIREG_APP_IPAD` | The package for the `ipad` / `ipadfull` profiles | None, must be given |
| `UIREG_STACK_SAVE` | Directory name of the dedicated save for runs `d`, `t`, `u` | `save.pw_rune_stack` |
| `UIREG_EXTRA_ENV` | Environment variables added to the app for every run (space-separated `K=V`) | Unset |
| `UIREG_FORBID` | Regular expression: if it appears anywhere in the log of a run, the run counts as failed | Unset |
| `UIREG_REQUIRE` | Regular expression: must appear in the log of the run; the result states the number of occurrences (used to confirm that a certain switch really took effect) | Unset |
| `UIREG_TAG` | Prefix of each run's artifact directory name (keeps two regression passes apart when they queue one after the other) | `ur` |
| `SMOKE_OWNER` | The name shown while queueing for the lock | `未署名` (unsigned) |

## Packages for the iPad profiles

The repository has no separate script for building an iPad package. The procedure is the same as the two packaging steps in `tools/smoke_mac.sh`, with just one more build setting:

1. The same `xcodebuild` command, with its `DM_DEVICE_FAMILY=1` replaced by `DM_DEVICE_FAMILY=1,2` (adds iPad to the device family; `tools/smoke_mac.sh` always builds its package for iPhone only, regardless of the default in `../../project.yml`).
2. Wrap it the same way as `<name>.app/Wrapper/DmIOS.app`, plus the `WrappedBundle` symlink.

Use a new directory name each time you build a package, then give the path to `UIREG_APP_IPAD`.

## Rules for swapping packages

When a Mac runs an iOS package, the system mounts `<package>/Wrapper` in order to run it. If this directory is deleted or replaced wholesale while it is mounted, a stale mount point that cannot be unmounted is left behind; that path can no longer be opened from then on, and only a reboot clears it. So:

- Do not touch the package while it is running.
- To swap packages, use a different directory name; or only do incremental updates of same-named files, without deleting or wholesale replacing `Wrapper`.

`run_mac.sh` itself does not copy the package; it launches directly from the given path.

## Running on a real device

```bash
UIREG_RUNNER=dm/ios/tools/uireg/run_dev.sh python3 dm/ios/tools/uireg/uireg.py iphone a c
```

`run_dev.sh` runs **the package that is already installed** on the phone through `../touch_e2e.sh`; the prerequisites are the same as for the real-device smoke test (see "Real-device smoke test" in [../../README.md](../../README.md)). Differences from running on a Mac:

- Only the `iphone` profile.
- Runs that need dedicated saves are not run.
- Each run is collected only after running for the full limit in seconds; the log is cut off at the marker for the end of the script, and the full text is saved separately as `boot_full.txt`.
- By default only the log is pulled back, not the screenshots, so items that need screenshots are all `未判` (not judged) on a real device. To pull certain screenshots, name them with `UIREG_PULL="<file name> <file name>"`.
- It is not responsible for installing the package. To guarantee that what is under test is the package you just installed yourself, take the device lock in an outer layer first and wrap "install + run" together (the device lock is reentrant, `../devlock.sh`).

## Picking runs by change

```bash
python3 dm/ios/tools/uireg/uireg_pick.py <commit or branch under test> --main <main branch>
```

It compares which files have changed since the fork point between the commit under test and the main branch, lists which runs should be run according to `affected.tsv` (path glob → affected runs), and the last line, `局：…` (runs: …), gives the names of the runs to run. `*` means the whole table has to be run; source files under `dm/` that are not registered are treated as the whole table. With `--core`, or when there are no affected runs at all, a set of core runs defined in the script is merged in. When adding a new run, remember to register it in `affected.tsv`.

## The run attached after the Mac smoke test

When `tools/smoke_mac.sh` is run with `SMOKE_UIREG=1`, one pass of `iphone b` is additionally run after the smoke test passes:

- It runs in the background and does not affect the smoke test's own result or exit code.
- It uses a cloned copy of the package of this smoke test, so smoke-testing again or re-wrapping right afterwards cannot touch the copy it has open.
- When it finishes, it adds one line to the smoke test record `$DM_ROOT/run/ios/smoke_ledger.tsv`, where the result column is `Mac:界面回归b:OK` or `Mac:界面回归b:RED` (`界面回归` = UI regression). The condition for passing: all items that have a criterion pass, and the log has 0 red lines; `未判` (not judged) items are not counted.
- If it is red, the artifacts are left under `uireg/` in the result directory of that smoke test, and the next smoke test gives a one-line reminder at its start.

Note that run `b` likewise depends on the standard save described above.

## Conventions for writing your own runner script

If you want the same set of cases to run in another environment, you can write another runner script and point `UIREG_RUNNER` at it:

- Invocation: `bash $UIREG_RUNNER <tag> <limit seconds>`, with the touch script given on standard input.
- Environment variables passed in: `PW_ENV` (space-separated `K=V`, added as is to the app's launch environment), `PW_SAVE` (directory name of the dedicated save to swap in; empty for most runs), `UIREG_MODE` (screen profile; pick the package according to it), `UIREG_OUT`.
- Put the outputs in `$UIREG_OUT/pw/<tag>/`: `boot.txt` (required), screenshots (using the original names given by `shot` in the script), `timing.txt` (one line of text starting with `计时：` (timing:), which is copied as is into the result table), `c2s.jsonl` (a record of the command packets the game sends; may be absent).
- The last line of the touch script is `<seconds> note 结束` (`结束` = end): the run can be collected as soon as `[script] note 结束` appears in the log, and the limit in seconds is only a fallback.
