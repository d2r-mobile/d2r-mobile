# Image Quality Metric

中文：[image_quality.zh-CN.md](image_quality.zh-CN.md)

This directory holds two rulers for measuring the picture, both measured on a Mac from offline screenshots taken with the host (`dmhost` from `dm/host`):

- **G1** (`shoot.sh` + `g1.py`): how far the picture at a given internal resolution is from "the best picture achievable on this phone screen". Used to choose the internal resolution.
- **Visual-equivalence ruler** (`ruler.sh`): whether, after a given rendering optimization is turned on, the picture is equivalent to the picture without it. Used to gate optimizations in the graphics layer.

Both use NVIDIA FLIP to measure the difference between two images. FLIP is an image difference metric that models human viewing, 0 = no visible difference; the source is in `dm/third_party/flip/` (BSD-3); first run `make -C dm/third_party/flip` to build `dm/third_party/flip/flip`.

## G1: how far from native resolution

### Definitions

- **Reference frame**: the same frozen frame, drawn at the screen's native resolution 2868x1320, without upscaling.
- **Frame under test**: the same frame drawn at some internal resolution (`DM_GAME_RES`), then upscaled to 2868x1320 through the real presentation path (the graphics layer's `present_encode`). The screenshot size is fixed by `DM_NVN_SHOT_OUT=2868x1320`, which `shoot.sh` already sets.
- **Switch baseline**: 900 high, bilinear upscaling. The width is taken as 1952 by the screen's aspect ratio (1952x900), not the 16:9 1600x900 — 16:9 output would be centered with black bars and the UI layout would differ as well, so it could not be matched pixel for pixel with the reference frame.
- **G1** = the FLIP (LDR) mean, `-ppd 95`, averaged over all frames. `ppd` is the number of pixels per degree of visual angle: the iPhone 17 Pro Max is about 460 ppi at a viewing distance of 30 cm, 0.30 × tan 1° ÷ 0.0254 × 460 ≈ 95.
- **Target**: the phone configuration's G1 ≤ half of the Switch baseline's G1.

What is measured is sharpness: the internal resolution changes only sharpness, not the field of view or the layout.

### Frame list

`frames.tsv` lists 20 frames, each line being "number, name, source":

- 01–16 take the trigger, key sequence and environment variables directly from the [screenshot gate](../screenshot_gate.md) page specs (`shotgate_pages/*.json`).
- 17–20 are four combat frames: they replay the command packets recorded from a ground-truth run (ground truth = reference data obtained by running the original game in a patched Ryujinx emulator) and use `DM_TICK_HOLD=<tick>:SHOT` to stop at the specified tick and take the screenshot.

Prerequisites:

- The standard save `$DM_ROOT/data/switch/save_ref_warlock/0000000000000001` (`DM_GATE_SAVE` overrides it).
- The ground-truth replay files referenced in the page specs; the replay file path for the combat frames is hard-coded in `shoot.sh` and `ruler.sh`, pointing to a ground-truth run directory under `$DM_ROOT/run/verify/ryu/`. None of these are in the repository, and the emulator driver scripts used to record them are not shipped either, so the frames that rely on ground-truth replay cannot be run with this repository alone; see [../README.md](../README.md).

### Commands

```
make -C dm/third_party/flip
bash dm/verify/gfxq/shoot.sh dm/host/dmhost <build output dir> <WxH> <output dir> [parallel jobs=4] [number or name…]
python3 dm/verify/gfxq/g1.py <reference dir> <test dir> [--flip <flip path>]
```

- `shoot.sh` starts one host per frame and outputs `<output dir>/<number>.png` and `<number>.log`; with no numbers given it runs all 20 frames. When frames fail it lists the failed frames, exit code 1.
- `g1.py` compares the PNGs with the same name in the two directories: it prints the FLIP mean frame by frame, and the last line gives the G1 mean and the 3 worst frames. When the test directory is missing frames or the sizes on the two sides differ it reports red, exit code 1. `--flip` defaults to `dm/third_party/flip/flip`.

One complete measurement: first produce the reference directory at `2868x1320`, then produce one test directory for each candidate resolution, and compute G1 for each against the reference directory.

### About the video memory budget

The game's own video memory allocator has a budget constant (`main + 0x8524c4`), which is 260 MB in the original. Under this budget, as soon as the internal resolution is high the render targets no longer fit and the game crashes at startup, so a reference frame at native resolution cannot be drawn.

The rewriter has already registered this constant as a cut (`CUTS` in `dm/tools/patch_nso.py`), and it is 512 MB in all build outputs, so reference frames can simply be produced with an ordinary build output. `truth_out.sh` is the approach from before this cut went into the rewriter: it copies a build output that is still at 260 MB to `<source>-truth` and changes this one instruction. It first checks whether the source build output still has the original instruction at that location; for current build outputs it reports red and exits, and it no longer needs to be used.

### Rules

- **The reference frames and the frames under test must be produced with the same host binary.** Once the host's replay pacing changes, the two binaries are not drawing the same instant, and the difference that comes out is a systematic offset rather than a change in image quality. So every time readings are produced, first reproduce the reference frames with the current host, then produce the frames under test.
- **Run the frames at product pacing; do not set `DM_DETCLOCK`.** (`DM_DETCLOCK` is the deterministic clock for logic-type tests.) With it set, game ticks run ahead of asynchronous loading and the UI, and the behavior of panels and the loading screen is neither like the product nor deterministic.
- **Measure the noise floor every time.** Frozen frames cannot be made byte-for-byte reproducible: things the client itself animates, such as flames and animations, will not stop at the same instant in two runs. Run the same binary with the same configuration twice, and the FLIP between the two runs is the noise floor (record both the per-frame values and the mean); only more than 2 times the noise floor counts as a difference. The noise floor is not carried over from old numbers.
- **The width of the native setting must be exactly equal to the screen width.** If it is off by even a few columns, the output still has to be resampled across the whole screen, and every pixel gets interpolated once.

## Visual-equivalence ruler: `ruler.sh`

G1 has to be compared across runs, its noise floor is on the high side, and it cannot measure small differences. To judge "whether the picture is still the same one after a given optimization is turned on", use this ruler: it takes three shots in a row within **the same process and the same frozen tick**.

```
bash dm/verify/gfxq/ruler.sh <dmhost> <build output dir> <WxH> <output dir> [frame number…]
```

- One host is started per frame. `DM_NVN_RULER=1` makes the host first take the two shots A and B with the "reference settings" (the batch of rendering optimizations the graphics layer makes for Apple GPUs all turned off), then switch to the settings under test (everything on by default, plus the environment variables passed through by the caller) and take C. The three shots are 8 frames apart.
- **Noise** = FLIP(A, B), **difference** = FLIP(B, C), both with `-ppd 95`.
- With no frame numbers given it runs 4 frames: `02` camp, `16` wilderness, `18` combat, `06` inventory.
- Output is `<output dir>/<frame number>_{A,B,C}.png` and `<frame number>.log`; each frame prints one line of readings and the verdict, and the last line is 「汇总：n/m 帧等价」 (summary: n/m frames equivalent); if any frame is not equivalent the exit code is 1.

There are two modes of judgment, selected with the environment variable `RULER_MODE`:

| Mode | Applies to | Condition for judging equivalent |
|---|---|---|
| `exact` (default) | Optimizations such as merging and skipping, which ought to produce the same image | FLIP difference ≤ 0.01 and ≤ 2 × noise + 0.002; and the count of "large-difference pixels" (any channel differing by > 30/255): B–C ≤ 2 × A–B + 0.005% of the picture's pixels |
| `approx` | Approximations such as lowering resolution or changing the filter | FLIP difference ≤ 0.02; and the number of pixels differing by > 60/255: B–C ≤ 2 × A–B + 0.05% of the picture's pixels |

Why count large-difference pixels: the FLIP mean is insensitive to local defects; when a small patch is not drawn, the mean hardly moves.

Negative control: `DM_NVN_RULER_NEG=<fragment shader key>` makes the settings under test drop the draws that use that shader, and the ruler must report non-equivalent. After adding a new criterion or changing the switching timing, run a negative control once first to confirm the ruler is not running idle.

Limitation: this ruler can only measure settings that can be switched at run time.
