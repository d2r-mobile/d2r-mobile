# Long frames, texture uploads, shader loads and the OnePlus 13 black tiles (2026-10-21, afternoon)

中文：[2026-10-21-long-frames.zh-CN.md](2026-10-21-long-frames.zh-CN.md)

Measured on 2026-10-21 between 11:52 and 15:08 (NZDT) on the Redmi K50 Ultra (Adreno 730) and the OnePlus 13 (Adreno
830), production defaults (3D scale ceiling 0.85, dynamic) unless noted, saves restored after each session.

## The long-frame line on Android (`0f2f384`)

The HUD long-frame line (`[长帧]`, `hle_nvn.c`) took its GPU, pipeline, shader-library and upload fields from the Metal
backend, so on Android it always read `新建管线 0 … GPU -1.0 ms`, even in frames that compiled pipelines. The Vulkan
adapter now fills them from the engine: pipelines created in the frame, shader loads, whole texture uploads (first
uploads, re-uploads, MB, time) and the GPU time of each HLE frame (the timestamp spans of the submissions made while it
was current). On the Redmi the GPU field matches `NVN PERF` (15.4 against 15.32 ms).

With GPU time known, frames that were slow only on the GPU (over 16.7 ms and 30% over the recent average) are logged too;
those lines end in `整帧不长，GPU 长`, so the untagged lines follow the old rule. Nothing else on Android reads the GPU
time: the dynamic 3D scale takes the present intervals (`dm_3d_scale_feed`), and the GPU ring (`nvn_gpu_recent`) is
read only by the iOS heat log.

## Where the long frames are

| Run | lines | start-up menus | loading screens | play |
|---|---|---|---|---|
| H1, camp 180 s | 76 | 46 | 21 | 9 |
| H2, Den of Evil fight 150 s | 85 | 42 | 29 | 14 |

The long-frame counts of the earlier notes were mostly the start-up menus and loading screens. Steady play had 4-7
single frames of 24-30 ms in about 2.5 minutes. Two kinds of frames are visible hitches:

- Entering the camp: the first camp frame (still behind the loading screen) uploads 21 new textures and re-uploads 402
  (29 MB), loads 12 shaders and took 106-131 ms of GPU time; the translation thread then waits for the GPU, the game
  thread waits for the full translation queue, and the next visible frame takes 97-115 ms.
- Streaming in the fight: one frame uploaded 36 MB (16 new, 164 re-uploaded) and loaded 2 shaders (11 ms): 53 ms.

A per-frame TSV (`DM_NVN_TSV`) adds hitches of its own (26 long frames in play in 120 s against 9 in 180 s without).
`DM_SLOWFRAME=1` records the same fields only for dropped frames and writes no per-frame file.

## Texture uploads ahead of the submission (`5dc14f0`, Android default since `ed70553`)

A whole upload ran as a non-draw `Commands` block: it ended the open render pass and put full barriers before and after
it, so on a tiling GPU each upload in the middle of a frame stored the pass's attachments and loaded them again.
`gfx-upload-prologue` uploads an image no command of the submission has used yet (`get_image` stamps each image with
`slot_serial`) in the prologue command buffer submitted ahead of it: one barrier to transfer destinations, the copies,
one barrier back to `GENERAL`.

| Run | prologue | uploads in the prologue | uploads that ended an open pass | GPU of the first camp frame | visible frame after it |
|---|---|---|---|---|---|
| U0 | off | 0 | 382 | 106.2 ms | 94.4 ms |
| U1 | on | 1,246 | 0 | 85.2 ms | 85.4 ms |
| U2 | off | 0 | 384 | 106.6 ms | 96.4 ms |
| U3 | on | 1,248 | 0 | 94.6 ms | 96.3 ms |
| F0, fight | off | 0 | 558 | 114.5 ms | 108.6 ms |
| F1, fight | on | 1,784 | 0 | 84.7 ms | 85.8 ms |

Over all of today's camp entries the first frame took 85-107 ms of GPU time with the prologue (9 runs: these, T1 and the
shader-reuse runs below; mean about 95) against 106-132 ms without (6 runs: these and H1-H3; mean about 115); the three
pairs of this session alone give means of about 88 and 109. Its upload CPU dropped from about 20 to 12 ms. Screenshots of the camp match, and under the Khronos validation layer (core and sync) both settings report
the same 10 messages of one existing check (`VUID-RuntimeSpirv-maxCombinedClipAndCullDistances-00710`) and no layout or
hazard messages.

## Shader loads (`da088b6`, Android default since `ad8eb2a`)

The `NVN shader … loaded` line now times each part of a load. In a camp entry (110 loads) the SPIR-V rewrites took
105-110 ms of 135-140 ms: fragment shaders 1.53 ms each on average, the largest (37k-50k words) 6-9 ms; reading the
bundle 5-6 ms in all, the analysis 24-25 ms, module creation nothing. The 21 loads around the first camp frame spent
35-40 ms in rewrites on the translation thread.

The warm-up threads already run the same rewrites on the same bundles for every shader of the warm-up list.
`gfx-shader-reuse` keeps their result for `load_shader`; value 2 rewrites as well and compares.

| Run | reuse | loads reused | rewrites in the run | pipeline and shader creation in the first camp frame | its translation time | visible frame after it |
|---|---|---|---|---|---|---|
| R2 | verify | 97 of 110, 0 mismatches | 105 ms | 40.9 ms | 95.8 ms | 84.7 ms |
| R0 | off | 0 | 107 ms | 39.9 ms | 99.1 ms | 114.0 ms |
| R1 | on | 86 of 99 | 6 ms | 7.4 ms | 77.0 ms | 106.2 ms |
| R0b | off | 0 | 110 ms | 43.6 ms | 99.5 ms | 107.6 ms |
| R1b | on | 97 of 110 | 6 ms | 7.8 ms | 66.5 ms | 107.6 ms |

The visible frame after entering does not move: it waits for the first camp frame's GPU time (85-107 ms in these runs).
The kept words of shaders a session does not load stay until shutdown; the OnePlus 13's list names 222 shaders, 12.7 MB
of bundles, of which a camp session loads 86-97.

## The OnePlus 13 black tiles are not the slot race

The new build was installed by hand (ColorOS refuses `adb install`). Camp walks recorded with `op13_move.sh`, scanned with
`blackstream.py` (transient dark 8 × 8 blocks) and `flashes.py` (lights appearing):

| Walk | readback guard | guarded buffers a frame | tile events at a new light |
|---|---|---|---|
| OW1 | on | 6 | 1, 22 blocks |
| OW5 | on | 6 | 2, 7 blocks each |
| OG0b | off | 0 | 3, 5 blocks each |
| OW6 | off | 0 | 2, 7 blocks each |
| OW9 | off | 0 | 6, 48 blocks each |

The tiles appear with the guard on, so the race between the frame slots does not cause them. They come with a light
that appears at the top edge of the screen, the same signature as the 2026-10-20 recordings (mv1, na1); Charsi's regular
hammer flashes never show them. That light appeared in 5 of today's 13 walks; the other 8 (6 with the guard on) never got
it.

## What the camp's first frame does on the GPU

A pass profile of frames 2115-2174 (marker `gfx-pass-profile` holding `2115 1 60`, P3) caught it: 100.3 ms of work in a
100.8 ms span (no idle gaps), 261 passes and dispatches against 64 in an ordinary frame (14.9 ms).

- Compute 64.3 ms, of which one shader, `0f715b1a5e03f11f`, takes 58.6 ms in 196 dispatches of 16 × 16 groups (8 × 8
  threads, so 128 × 128 a dispatch, about 0.3 ms each). It reads 9 textures with 35 explicit-LOD samples, partly in
  loops, writes 4 storage images (formatless), and is 43k words long; it runs only in this frame and is not one of the
  two warp32 shaders. It looks like the game baking the area's ground materials once.
- Passes 36.0 ms: the main 3D pass took 20.1 ms (618 draws; about 5-6 ms in an ordinary frame), the pre-pass 4.1 ms, the
  shadow maps 2.7 and 1.2 ms.

So the hitch after entering is the game's own one-time work; making it shorter would mean making that shader faster on
Adreno.

## Still open

- The camp's first frame: the bake shader `0f715b1a5e03f11f` (58.6 ms) and the first frame's slow main pass.
- The black tiles at a new light (OnePlus 13).
