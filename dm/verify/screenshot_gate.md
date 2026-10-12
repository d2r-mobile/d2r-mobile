# Screenshot Gate

中文：[screenshot_gate.zh-CN.md](screenshot_gate.zh-CN.md)

The screenshot gate compares the key screens drawn by the **host** (`dmhost` from `dm/host`, which executes the rewritten game program on a Mac) page by page with the **ground truth** (reference images captured by running the original game in a patched Ryujinx emulator), to judge whether whole blocks are missing, lighting is missing, or the structure is distorted.

Why compare against the ground truth: if you only compare the host's new image with the host's old image, it will report "identical" even when both are black.

The GPUs and shader chains on the two sides are both different, so pixel-for-pixel identity is not required; the criteria below are used instead.

## One command

```
dm/verify/shotgate.sh [build output dir] [page name…]
```

- The build output directory defaults to the current codename's `keypage` build output (`dm/tools/product.sh path keypage`); the page names default to everything under `shotgate_pages/`.
- It starts by running `make` on the host. Each page starts one host (`hostshot.sh`), which starts a game from a fresh copy of the standard save, follows the key sequence in the page spec to that screen, and takes a screenshot at the trigger point; `shotgate.py` then compares it with the ground-truth reference image. The pages are independent of each other and run in parallel.
- Each page prints one line `SHOTGATE <page> OK|RED …`, and the last line is `SHOTGATE_ALL OK|RED → <report path>`; exit code 0 = all green.

Reports and run artifacts:

- When anything is red, the whole set of run artifacts is kept in `$DM_ROOT/run/verify/dm/shotgate/<timestamp>_<worktree name>_<pid>…/`: `index.html` is the summary, each page has a `report.html` (ground-truth image / host image / difference heat map side by side, plus the criterion readings and the expected-elements table), and each page's full host log is `<page>_host.log`.
- When everything is green the run artifacts are deleted, leaving only a short summary `shotgate/green/<directory name>.html`.
- When a host screenshot fails (a crash or the watchdog), the run artifact directory gets an extra `KEEP` file, with a copy of the system's crash report attached.
- `shotgate_gc.sh` cleans up old run artifacts more than 3 days old at the end of every run; those with `KEEP` are not deleted, and for a directory you want to keep long term, put an empty file `PIN` in it.

| Environment variable | Effect |
|---|---|
| `SHOTGATE_JOBS` | How many pages run at once, default 6 |
| `SHOTGATE_KEEP_IMG=1` | Keep the whole set of run artifacts even when all green |
| `SHOTGATE_KEEP_DAYS` | Number of days old run artifacts are kept, default 3 |
| `DM_GATE_SAVE` | The save directory the host uses, default `$DM_ROOT/data/switch/save_ref_warlock/0000000000000001` |
| `DM_HOST` | The host executable, default this worktree's `dm/host/dmhost` |
| `DM_PY` | The Python interpreter, default `$DM_ROOT/build/cache/dm/venv/bin/python` |

## Prerequisites

The repository contains only the page specs and the comparison program; the following you have to prepare yourself (for where the ground truth comes from see [README.md](README.md); the emulator driver scripts used to obtain it are not shipped with the repository, so this gate cannot be run with this repository alone):

- **Ground-truth reference images**: one `<page>.png` per page, with a `<page>.meta.json` beside it. The location is written in the page spec's `truth` field; the existing page specs point to the three directories `truth_shots_v7`, `truth_shots_v8` and `truth_shots_v9` under `$DM_ROOT/data/switch/` (the directory names are written in the page specs; reference images re-made after a save change go into a new directory, hence the numbers).
- **Standard save**: the host and the ground truth must start from the same save. The first entry of the `save` field in `meta.json` is the content fingerprint of the ground-truth run's starting save (computed by `savesha.sh`); when the fingerprint of the save the host uses differs from it, that page is reported red immediately, without comparing.
- **Replay files**: pages whose standing position has to be aligned (see below) reference, in the page spec's `host.env`, command packet files recorded from a ground-truth run, located at `$DM_ROOT/data/switch/truth_runs/<run name>/`.
- **Animation masks**: two pages (`camp_e`, `wild`) reference mask images in `shotgate_masks_v2` and `shotgate_masks_v3` under `$DM_ROOT/data/switch/` (likewise directory names written in the page specs).

## Criteria

`shotgate.py <page spec.json> <ground truth.png> <host.png> <report dir>`. The two images are first scaled down to 320×180 and then compared; if any one criterion is red, the page is red. The default thresholds are in `DEF` in `shotgate.py`, and the page spec's `thresholds` can change them per page.

| # | Criterion | Default threshold | What it catches |
|---|---|---|---|
| ① | Structural similarity (SSIM): grayscale, 7×7 box window, masked areas not counted | ≥ 0.60 | Overall distortion of the picture |
| ② | Proportion of non-black pixels (luminance > 0.06) in the 3D viewport | host ≥ ground truth × 0.90 | Whole blocks missing |
| ③ | 8×8 grid, cell by cell: mean luminance difference, mean color distance | ≤ 0.10, ≤ 0.16 | Local omissions and color casts; red cells are drawn into the heat map |
| ④ | Named regions (character, campfire, menu…): region luminance ratio host/ground truth, region structural similarity | luminance ratio ∈ [0.75, 1/0.75], structure ≥ 0.45 | Missing lighting, located to a specific region |
| ⑤ | Fine grid (enabled only when the page spec has `fine_grid`): 640×360 grayscale divided into cells by "columns × rows", mean structural similarity per cell, looking only inside the viewport | The page spec gives the lower bound; number of red cells ≤ `fine_red_max` (default 0) | One small element not drawn |

A few more:

- **Expected-elements table** (the page spec's `elements`): each entry points to a named region and is ticked according to that region's verdict under ④; those pointing to `viewport` go by ②. An element written as 「无」 (none) means that this position must match the ground truth (there is nothing there in the ground truth to begin with).
- **Named regions can have their own thresholds**: `{"rect": […], "ratio_min": …, "ssim_min": …}`. A `ratio_min` of 0 means only structure is compared, not luminance; this is for animated regions.
- **Animation mask** (the page spec's `dynmask`): a mask image in which white marks pixels the client itself animates (flames, flags, etc.); it is merged into the mask and takes no part in the comparison. When the mask covers more than 30% of the viewport, the report marks it 「弱判」 (weak verdict). For fine-grid cells more than half covered by the mask, the structure lower bound becomes `thresholds.dyn_ssim_min` (default 0.5).
- **Host log**: if the screenshot is green but the log contains errors, it is still judged red. The keyword table is in `logred.sh` (lines carrying 【红】 (red), FATAL, the watchdog, Metal command buffer errors, GPU hang or fault, pipeline creation failure).
- **`host.require_log`**: the lines listed in the page spec must appear in the host log; one missing line means red.
- **Screenshot precondition verdict**: every time the host takes a screenshot it writes a line 「SHOT 前置：读取画面已撤 / 本局无读取画面 / 不适用」 (SHOT precondition: loading screen dismissed / no loading screen in this run / not applicable); if this line is missing, it is red.

Why ⑤ is needed: the average over a large region hides "one small element not drawn". The static pages of the UI layer (skill tree, options, stash, character stats, inventory, quest log, etc.) all carry a fine grid.

When the gate runs the host it always sets `DM_BRIGHT_K=1`, turning off "3D scene brightening", a post-processing effect that deliberately deviates from the original, so that only the picture translation itself is checked.

## Page spec

`shotgate_pages/<page>.json`:

| Field | Meaning |
|---|---|
| `page` | The page's Chinese name |
| `truth` | Path of the ground-truth reference image |
| `host.trigger` | When the host takes the screenshot: `<UI message substring>+<frame offset>`, i.e. how many frames after that message appears |
| `host.keys` | The host's key script (`DM_KEYS`); empty string = no key presses |
| `host.env` | Extra environment variables passed to the host (replay file, seed, fixed time, frozen tick, standing-position check, etc.) |
| `host.require_log` | Lines that must appear in the host log |
| `viewport` | The 3D viewport rectangle |
| `mask` | List of rectangles that take no part in the comparison |
| `regions` / `elements` | Named regions and the expected-elements table |
| `thresholds`, `fine_grid`, `fine_red_max`, `dynmask` | See the previous section |
| `note`, `trigger` | Explanatory text, not read by the program |

Rectangles are always written as `[x0, y0, x1, y1]`, with values 0–1, relative to the whole picture.

The syntax of `DM_KEYS` (defined in `dm/host/hle_hid.c`): `key@precondition message[%delay seconds][+hold seconds][>confirmation message]`, with multiple steps separated by commas — the key is pressed after the precondition message appears, and it counts as successful only when the confirmation message is seen. The precondition message can be written as `DM:Tick:<tick number>#`, meaning press when the server reaches that tick.

## Existing pages

| Page | Screen | Screenshot timing |
|---|---|---|
| `charselect` | Character select page | UI message + frame offset |
| `newchar` | Class selection page for a new character | UI message + frame offset |
| `loading` | Game-entry loading screen | UI message + frame offset |
| `warlock_camp` | The camp after a Warlock character enters the game | UI message + frame offset |
| `inventory` | Inventory | UI message + frame offset |
| `char_stats` | Character stats panel | UI message + frame offset |
| `skilltree` | Skill tree | UI message + frame offset |
| `warlock_skilltree` | Warlock skill tree | UI message + frame offset |
| `quest_log` | Quest log | UI message + frame offset |
| `automap` | Automap overlay | UI message + frame offset |
| `options` | Options page (pause menu) | UI message + frame offset |
| `cube` | Horadric Cube | UI message + frame offset |
| `stash` | Stash | Replay walks up to the chest and checks standing position, UI message + frame offset |
| `waypoint` | Waypoint panel | Replay walks to the waypoint and checks standing position, UI message + frame offset |
| `camp_e` | First screen of the camp | Replay + standing-position check + frozen tick |
| `npc` | NPC dialogue menu | Replay + standing-position check + frozen tick |
| `wild` | First screen of the wilderness after teleporting | Replay + standing-position check + frozen tick |

There are currently no pages for combat and death screens.

## How the two sides stop on the same screen

What has to be shown is: the two sides produce the same frame under "the same tick, the same server state, the same client UI".

**Static UI pages** are easy: the ground truth and the host both take the screenshot the same number of frames after the same UI message. On the ground-truth side the driver script captures by `<trigger substring>+<frame offset>:<name>`, on the host side it is `DM_NVN_SHOT_UI`, with the same syntax (the ground truth converts the frame count to wall clock at 30 frames per second).

**Pages with a 3D scene that require walking** (stash, waypoint, NPC, camp, wilderness) are hard because of standing position: if each side pushes the stick and walks on its own, the stopping positions do not line up, and NPCs wander too. The approach is to have the host not walk on its own, and instead replay the ground truth's input:

1. **Ground-truth walking**: the driver script pushes the left stick in a closed loop by "target coordinates − player coordinates", and the movement command packets and system-class messages the client emits while walking are recorded into `c2s.jsonl` as usual. The coordinates are taken from the player table at the server tick entry (read the same way as in [tick_state_format.md](tick_state_format.md)).
2. **Host replay**: `DM_C2S_REPLAY` replays the ground truth's command packets up to the tick of arrival (called tick E below). The server logic is equivalent tick by tick, so the positions of the player, monsters and NPCs are the same on both sides at tick E.
3. **Standing-position check**: `DM_POSCHK=E:x:y:xf:yf` makes the host read the player coordinates at the entry of tick E and compare them with the ground truth's coordinates at that tick. If they differ, the screenshot is withdrawn and the page reports 「站位不一致」 (standing position mismatch); if they are the same, it writes a line 「一致」 (match), and the page spec uses `require_log` to require that this line appear.
4. **Opening the panel**: injecting an interaction packet into the server does not make the client open a panel, so after arrival both sides have the client press the key itself (the ground truth sends an input command, the host uses `DM_KEYS` to press by tick number), and a UI message confirms that the panel is open.
5. **Frozen-tick screenshot**: for pages whose picture changes with the tick, both sides stop at the same tick before capturing. On the host, `DM_TICK_HOLD=E:SHOT` freezes the game clock on entering tick E and unfreezes it after the screenshot is written to disk, with the trigger written as `DM:Tick:E#+N`; on the ground-truth side the screenshot is taken on the first frame after tick E begins. This way, which tick gets captured is independent of frame rate and the machine's load.

Things the client itself animates (flames, ambient critters) are not in the server state and cannot be lined up even with a frozen tick; they are handled by animation masks or by loosening the thresholds of individual regions. How a mask is made: the ground-truth side captures two shots in a row at the same frozen tick, and then `dynmask.py <a.png> <b.png> <output mask.png> [threshold=8] [dilation pixels=3]` takes the difference of the two.

## How to produce ground-truth reference images

Ground-truth reference images are produced by the ground-truth emulator and are not included in the repository; the emulator driver scripts that produce them are not shipped with the repository either.

- Static UI pages are captured a number of frames after the trigger message; pages that require walking are captured at the arrival tick (see the previous section). Each page is one `.png` plus one `.meta.json`.
- Put the finished images together with `meta.json` into the directory the page spec's `truth` field points to. If the standard save is changed, all reference images have to be produced again, put into a new directory with the page specs updated, without overwriting the old directory.

## Adding a page

1. On the ground-truth side, settle the trigger message and frame offset (or the arrival tick), and produce the reference image.
2. Write `shotgate_pages/<page>.json`: following the reference image, mark out the viewport, the named regions and the expected elements; for static UI-layer pages add `fine_grid`.
3. Do a negative control: erase some element from the reference image and compare again; the gate must report red. An omission the criteria fail to catch means the regions or thresholds are not set correctly.
