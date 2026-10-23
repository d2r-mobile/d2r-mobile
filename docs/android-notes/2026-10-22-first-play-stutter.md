# First-play stutter: background pipeline builds and the warm-up seed (2026-10-22)

[中文](2026-10-22-first-play-stutter.zh-CN.md)

The first time a device meets a pipeline, the draw waits while the driver builds it. On a new install that means stutter
in every new area. Two ways around it were measured: building in the background and skipping the draw until the pipeline
is ready (not kept), and shipping the warm-up seed with the shader bundles (recommended).

## How it was measured

- Redmi K50 Ultra (Snapdragon 8+ Gen 1, Adreno 730), charging, each run started at 34.5 °C or below.
- Cold caches: the warm-up list `files/dm/vk-pipeline-list.bin` and the driver's pipeline cache were removed before each run
  and restored afterwards.
- Den of Evil fight with a fixed world seed (`DM_TEST_WARP_LV=250:8`, `DM_GAME_SEED=12345`), 120 s, per-frame times from
  `DM_NVN_TSV`. Each run contains about 9,700 frames, including the area load.
- Production APK from `ea047f3` (SHA-256 `86f9449d3cff2f69…`) for the production and seed runs. The background-build runs
  used experiment builds of the same tree.

## Results

| Run | Frames over 33 / 50 / 100 ms | Time lost | Pipelines built in the frame | Objects drawn late |
|---|---|---|---|---|
| Production | 31 / 19 / 8 | 8.0 s | 215 (3,294 ms, slowest 119 ms) | none |
| Background builds, no wait limit | 10 / 6 / 2 | 4.9 s | 49 (157 ms) | 165: mean 132 ms, longest 542 ms; 63 over 100 ms |
| Background builds, at most 50 ms of waiting | 28 / 13 / 6 | 6.9 s | 49 (160 ms) | 166: mean 88 ms, longest 333 ms; 53 over 100 ms |
| Seed in the shader folder | 10 / 4 / 2 | 5.4 s | 0 | none |

"Time lost" adds up every frame's time beyond 16.7 ms, so it includes the area load and the game's own one-time work.

## Background builds: not kept

An opaque draw into a pass whose targets are redrawn every frame was skipped while its pipeline built on one of three
background threads. That removes most of the stutter, but objects appear late: on average 132 ms after they should, and
up to half a second. Making a draw wait at most 50 ms brings the stutter back (36 waits in the run) without making
objects appear much sooner.

The time goes to the queue, not to the build. New pipelines arrive in bursts, and with three threads a build waited 87
ms on average before it started, then took 42 ms, against 15 ms for a build in the frame. Running the threads at normal
priority on the A710 cores only changed neither number (87 ms queued, 39 ms building). The experiment is not in this
branch.

## The warm-up seed: recommended

On a first launch, when there is no `files/dm/vk-pipeline-list.bin` yet, the engine builds the pipelines listed in
`files/dm/shaders/vk-pipeline-list.seed` in the background during start-up. The seed used here merges the Redmi's and
the OnePlus 13's lists (`dm/android/tools/merge_warm_lists.py`). The warm-up built its 436 pipelines within about 6 s of
launch, long before the game was started (10.6 s of build time across its threads), with none failing. The fight then
built nothing in the frame and drew nothing late.

A seed only covers what the devices that recorded it have seen: here mostly the camp, the Den of Evil and the Cow Level.
A first visit anywhere else still builds in the frame, and lists from more of the game make a better seed. The seed
names the bundles' shaders by key and only works with them, so it ships with the bundles and, like them, stays out of
Git and the APK.
