# Contributing

中文: [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)

This page covers three things: what you must be able to run before changing anything, the three rules of this repository, and what to attach when reporting a problem. How the parts work is not repeated here; see [dm/README.md](dm/README.md) and [dm/verify/README.md](dm/verify/README.md).

## Before you change anything: get the tests passing

Run everything below that you can on the untouched code first, then again after your change. All commands are run from the repository root.

### No game files needed

These only read source files; `python3` is enough.

| Command | What it checks |
|---|---|
| `python3 dm/i18n/scan_literals.py` | The literal gate: whether any Chinese text the player can see bypasses the Chinese-English string table; see [dm/i18n/README.md](dm/i18n/README.md) |
| `python3 dm/ios/tools/dm_i18n.py --check` | Whether every row of the string table `dm/i18n/ui_text.tsv` follows the rules, and whether the checked-in `dm/host/dm_i18n_tab.h` was generated from the current table |

### Game files and a prepared workspace needed

Prerequisite: your own game files are in place, `./setup.sh` has finished (it creates the Python environment, produces the rewrite output and translates the shaders), and you have entered that Python environment: `source ~/dm-work/build/cache/dm/venv/bin/activate` (if you set `DM_ROOT`, use it in place of `~/dm-work`). Use `./setup.sh check` to see what is missing.

| Command | What it checks | Extra prerequisites |
|---|---|---|
| `make -C dm/host/tests run` | Host unit tests. Also runs the two text checks above and two source-only scans: no writes to the original game's UI widgets (`native_write_scan.py`), and the registration of touch / controller mode behavior (`mode_scan.py`) | Some tests read the game's program files and resource directory directly |
| `make -C dm/gfx/tests run` | Graphics-layer unit tests, run for real on this machine's Metal | Some tests read the shader cache translated by `./setup.sh` |
| `make -C dm/ios/tests test` | Unit tests of the iOS shell: touch handling, skill binding parsing and so on. No device, and the app is not launched | The skill index is not in the repository; when missing it is generated from the game files |
| `dm/verify/regress.sh` | Rewrite-equivalence regression: whether the rewriter's output is structurally correct, whether every rewritten site behaves like the original instruction, and whether the host gates work. Last line `REGRESS OK` or `REGRESS FAIL` | `llvm-objdump` and `llvm-objcopy`; point `LLVM_BIN` at their directory |
| `cd dm/ios && ./run.sh build` | Whether the iOS package builds (build only, no device involved) | Xcode, and the signing configuration in `dm/ios/local.env` |
| `dm/ios/tools/smoke_mac.sh 1` | Smoke test: actually launches the iOS package on the Mac, loads a save and enters the game, with audio playing and no line marked 【红】 (red) in the log. Last line `SMOKE-MAC OK` or `SMOKE-MAC RED` | Run `dm/ios/tools/make_fixture.sh` once first to create the smoke save |
| `dm/ios/tools/check_hud_gen.sh` | Whether anything generated from game files has been added to git; when this machine has game files it also generates the bottom UI once for real | Run inside the git repository |

If you changed the rewriter `dm/tools/patch_nso.py`, use `dm/verify/regress.sh --fresh`: it first regenerates the output with the rewriter in your working tree and then verifies that.

Criteria, output locations and environment variables for each of these are in [dm/verify/README.md](dm/verify/README.md) and [dm/ios/README.md](dm/ios/README.md).

### The part you cannot run

In the full acceptance run `dm/verify/accept.sh`, the save comparison, the screenshot gate and the logic gates need "ground-truth data": recordings of the original game running. That data is not in the repository and never will be, and the emulator driver scripts used to record it are not shipped either. What ground truth is and how it is formatted is described in [dm/verify/README.md](dm/verify/README.md).

When you submit a change, state which checks you actually ran and what they reported. If you did not run something, say so.

## Three rules

### 1. No game content in the repository

The repository contains and distributes no game content. The game program, art, text, shaders and saves, and any image, table or code generated from them, stay out. Everyone produces these on their own machine from their own game files. What the repository does carry is interface information recorded to make the port work (shader interface descriptions, the graphics API function table, addresses inside the program and so on), listed item by item in [docs/DERIVED.md](docs/DERIVED.md); open an issue to discuss before adding anything of that kind.

The following are generated at build time and are already listed in the `.gitignore` files of their directories. Do not force-add them:

| Path | What it is |
|---|---|
| `dm/host/dm_vault_tab.h`, `dm/host/dm_hud_exp.h`, `dm/host/dm_bright_levels.inc` | Headers generated from the game's data tables (by `dm/host/Makefile` at build time) |
| `dm/ios/Gen/` | Generated parts of the iOS package: UI images made from the game files, the embedded game program, the overlay |
| `dm/ios/Sources/TouchArt/dm_native_*.png`, `dm/ios/Sources/dm_touch_native_art.c` | The skill atlas and skill index |
| `dm/ios/DmIOS.xcodeproj/`, `dm/ios/Sources/Info.plist`, `dm/ios/Sources/DmIOS.entitlements`, and `Info.plist` and `RomfsDownloader.entitlements` under `dm/ios/RomfsDownloader/` | The project and configuration generated from `dm/ios/project.yml` |
| `dm/ios/local.env` | Your own signing configuration |
| `dm/host/dmhost`, `*.dSYM/`, `*.bin`, `*.pkl`, `__pycache__/`, the test programs built under each `tests/` | Build output and intermediate files |
| `.iosdd/`, `.iosdd_mac/`, `.iosdd_*/` | Xcode derived-data directories |

Three more files are in the repository but get rewritten by `./setup.sh` to match the tools on your machine: `dm/gfx/tools/dmsh_chain.lock`, `dm/gfx/nvn/dmsh_chain.h` and `dm/gfx/nvn/c1_full.tsv`. That is expected; do not commit those changes.

Everything under the workspace `$DM_ROOT` (default `~/dm-work`) — game files, rewrite output, caches, run logs — lives outside the repository and never enters it.

The same applies to issues and pull requests: no game files, no keys, no packages you built.

### 2. Comments are bilingual

Every comment in the code exists in both Chinese and English, saying the same thing. There are two existing forms; follow them:

- File headers and block comments: the Chinese lines first, immediately followed by the English lines.

  ```c
  // 导入解析（已实现接宿主、其余接记名桩）与宿主门分派。
  // Import resolution (implemented ones go to the host, the rest go to named stubs) and host gate dispatch.
  ```

- Trailing comments, and single lines in Python docstrings: Chinese first, then English, on the same line, separated by ` | `.

  ```c
  int dm_lang_zh(void);   // 1 = 我们的字用中文 | 1 = our text uses Chinese
  ```

Every source file opens with a comment saying what it does and what that is based on; the feature files in `dm/host` also name the addresses in the game program they rely on (all offsets into the original 3.3.94012 image). New files do the same.

Documentation is bilingual too, with matching content: `README.md` is English, `README.zh-CN.md` is Chinese.

`dm/third_party/` holds other people's code, kept as upstream; this rule does not apply there.

### 3. New on-screen text goes into the string table

Any text of ours that the player can see is registered in `dm/i18n/ui_text.tsv` (key / Chinese / English / where used); code refers to the key, never to a literal. After changing the table, regenerate and commit `dm/host/dm_i18n_tab.h`:

```
python3 dm/ios/tools/dm_i18n.py --header > dm/host/dm_i18n_tab.h
```

The literal gate `python3 dm/i18n/scan_literals.py` stops Chinese literals that are not in the table. It only recognizes literals containing Chinese, so it cannot catch hard-coded English; that belongs in the table all the same. Chinese that only goes to logs and never reaches the screen does not need an entry. The table's rules, the full steps for adding a string, and how to write a whitelist row are in [dm/i18n/README.md](dm/i18n/README.md).

## Submitting a change

- Say what you changed and why.
- Say which of the checks above you ran and what they reported.
- If anything visible on screen changed, attach screenshots taken after the change.
- Confirm that no game content is included.

A template lists these items when you open a pull request.

## Reporting a problem

Use the issue template. What to attach:

- Which step fails: preparing game files, `./setup.sh`, `./run.sh build`, the no-Xcode tool, installing, launching, or in game.
- Your setup: computer and OS, Xcode version, phone model and iOS version, the kind of signing (paid account / free account / sideloading tool), and the commit of this repository.
- The full output of `./setup.sh check`.
- What happened: the exact error text, what the screen showed, and steps to reproduce.
- The log: in game, pause menu → share diagnostic log. Look it over before posting and remove anything you do not want public (device name, user names in file paths).

Do not post game files or keys, or links to them, and do not upload the package you built: it contains game content. Issues containing any of these are deleted without discussion. We cannot help with obtaining game files; that takes your own console and the game you bought.
