# Storage writebacks on the Redmi K50 Ultra (2026-10-18)

中文：[2026-10-18-writebacks.zh-CN.md](2026-10-18-writebacks.zh-CN.md)

Measured 12:03–12:32 NZDT on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1, Android 15). Build `android/linux-build` with the Android defaults, including the [quad fix](2026-10-18-combat.md). The change is PR #15 (branch `android/wb-skip`). The diagnostics are on branch `android/wb-census`, which is not for merging.

**Result:** with the scale held at 0.6, the camp goes from 50.99 to 52.72 fps and the CPU spends 1.15 ms less per frame (in-process A/B). The Den fight at scale 0.75 is GPU bound, so it saves 0.8 ms of CPU there with no fps change.

## Where the millisecond went

Round 2 measured 1.3 MB of storage writebacks per camp frame, costing about 1 ms (finding 14 of [round 2](2026-10-17-perf-round2.md)). A census (`gfx-wb-census`) shows where they come from. These are the camp numbers at scale 0.6; the Den fight gives the same:

| Compute shader | Writebacks per frame | Bytes | Changed by the GPU | Read time per frame |
|---|---|---|---|---|
| `b54d192f7c620ded` | 2 | 1,310,720 + 5,120 | 0.1–4 KB | 0.95 ms |
| `4639dd6d3e3f0c1b` | 1 | 65,536 | 18 KB | 0.05 ms |
| `da5e3bfe00b88521` | 1 | 16,384 | 16 KB | 0.01 ms |
| `6a7a308d61928982` | 1 | 5,120 | 4.7 KB | < 0.01 ms |
| two more | 1 each | 32 and 256 | | |

- The biggest one is a compute pass with 8×4 invocations per group that writes short per-invocation lists into two storage buffers. Almost all of the 1.25 MB it writes back is unchanged from the frame before.
- **Pool flags** (`DM_POOL_LOG`): all five large ranges lie in the game's 710 MB pool with flags `0x21`, which is `CPU_NO_ACCESS | GPU_CACHED`. By NVN's rules the game's CPU never reads that memory. The two small ones are in a 1 MB `CPU_CACHED | GPU_UNCACHED` pool and are kept.
- **Readers:** no snapshot, texel, uniform, CPU access or image upload touched those ranges in guest memory. The next frame's dispatches and draws took them from the pending GPU copy (`pending_view`; for the two largest ranges, 2 written binds and 3 storage reads per frame).
- **Order:** from frame 900 the bind and retire order was logged. When a slot retires, the next frame has already recorded a newer writeback of each of those ranges, so each copy into guest memory was overwritten one frame later without being read.

## Faster reads instead?

`gfx-membench` now also measures:

| Memory | CPU read |
|---|---|
| Type 6 (host visible, coherent, cached), 1 thread | 1.30 GB/s |
| Type 6, 2 / 3 / 4 threads | 1.26 / 1.14 / 0.57 GB/s |
| AHardwareBuffer (BLOB) imported into Vulkan, read through `AHardwareBuffer_lock` | 15.6 GB/s, plus 34–46 µs to lock and about 100 µs to unlock per 4 MB |

- The type-6 limit is shared by all cores, so splitting the copy across threads would not help.
- An AHardwareBuffer reads 12× faster, but it imports only into types 1 and 4 (device local, not host visible). The engine's readback arenas would need a lock and unlock around every use. Skipping the copy is simpler and saves more.

## The change

`resolve_slot_writebacks` drops a writeback when all three of these hold:
- a later pending writeback covers its range;
- the range lies in a `CPU_NO_ACCESS` pool;
- no image aliases it. Image uploads read guest memory directly.

Engine reads still get the newest result: through the newer pending writeback until its slot retires, and from guest memory after that. A forced resolve (`cpu_access`, `complete_all`) copies the newer writeback as before.

The HLE now keeps a table of pool flags (`nvn_pool_flags(addr, len)` in `hle_nvn.c`). The engine references it weakly, so hosts without it keep every writeback.

## Measurements

In-process A/B (`gfx-ab-toggle` 10 s, `gfx-ab-target` `wbskip`), battery at 29–32 °C:

| Scene | Skip on | Skip off |
|---|---|---|
| Camp, scale 0.6, X2 pin (45 / 48 windows) | **52.72 fps**, CPU 12.51 ms, GPU 17.67 ms | 50.99 fps, CPU 13.66 ms, GPU 17.64 ms |
| Den fight, `DM_GAME_SEED=12345`, scale 0.75 (30 / 28 windows) | 49.54 fps, CPU 9.64 ms, GPU 18.80 ms | 49.52 fps, CPU 10.45 ms, GPU 18.86 ms |

- 6 writebacks (1.34 MB) are dropped per camp frame, and 5–6 in the fight.
- Screenshots from both phases of the fight look the same, including torch light, fire bolt, floor and units. The log shows no errors.
- With the pin, the camp frame now waits on the GPU for longer (present 1.55 ms instead of 1.17).

## Also: the previous draw's pipeline (PR #16)

After the writebacks, the next single item was pipeline selection. Every draw serialized its `VkGraphicsPipelineCreateInfo` into a key (`xbg_graphics_pipeline_key`, with a module-id lookup per stage) and looked it up in a hash map: about 6% of the game thread.

A draw now copies the inputs that create info is built from and compares them with the previous draw's. The inputs are the shader pointers, the layout and pass definitions, the primitive, the sample count, the targets, and the `State` fields from `attrib` to `ds`, `blend_color` to the stencil reference, the patch size and the bounds. When they are equal, the draw takes the previous pipeline. The memo is dropped wherever `pipeline_fast` drops entries (eviction and the cache clears).

| Scene (in-process A/B, target `pipememo`) | Memo on | Memo off | Draws that hit |
|---|---|---|---|
| Camp, scale 0.6 (48 / 45 windows) | 53.36 fps, CPU 11.62 ms, GPU 17.69 ms | 52.93 fps, CPU 12.60 ms, GPU 17.65 ms | 80% |
| Den fight, `DM_GAME_SEED=12345`, scale 0.75 (24 / 33 windows) | 49.39 fps, CPU 8.97 ms, GPU 19.14 ms | 48.70 fps, CPU 9.47 ms, GPU 19.32 ms | 74% |

With both changes the camp at scale 0.6 is GPU bound: the CPU needs 11.6 ms of an 18.7 ms frame, and the present waits 2.2 ms for the GPU.

## Where the frame goes now

**Production defaults (dynamic scale), camp, 3 minutes:** the scale starts at 0.75 (46–47.6 fps, GPU 20.1 ms) and steps down to 0.70 (48.5 fps), then 0.65 (50–51 fps, GPU 18.6 ms). Each step comes from about 2.5% of frames over 25 ms. Before today's work it settled on the 0.6 floor at about 45.5 fps. GPU savings go to resolution first: the controller only stays at the configured 0.75 once frames there stay under 25 ms.

**GPU per pass, camp at scale 0.6 (17.7 ms, 64 passes):**

| Pass | Size | Draws | µs |
|---|---|---|---|
| Main 3D (B10G11R11 color + depth) | 1200×540 | 547 | 6,778 |
| Shadow map (D16 kept as X8_D24: this device lacks linear filtering for D16) | 1024×1024 | 92 | 2,055 |
| Depth pre-pass | 1200×540 | 547 | 1,754 |
| UI | 2000×900 | 871 | 1,365 |
| Compute (10 dispatches) | — | — | 1,200 |
| Effects (3 RGBA16F targets) | 1200×540 | 19 | 811 |
| Second shadow map | 1024×1024 | 119 | 387 |
| 57 small passes (blur chains, final passes) | — | mostly 1 | 3,676 |

**Tried, no gain:**
- **Clear on load** (branch `android/clear-load`, not pushed): a whole-attachment clear that opens a pass became the attachment's `LOAD_OP_CLEAR` in a compatible render pass, instead of `LOAD` followed by `vkCmdClearAttachments`. 15 clears per frame converted, GPU 17.69 ms in both phases of an in-process A/B. The driver evidently skips the load itself.
- **No occlusion queries** (diagnostic `gfx-no-occlusion`): GPU 17.99 ms against 17.69 ms. The 48 queries per frame cost nothing measurable.
- **Main-pass fragment shader** (`358197ac3db59cbc`, 14K lines of SPIR-V): no function-local arrays (no scratch memory), no other subgroup operation. It is simply a large lighting shader (5 loops, 35 texture operations).

## Markers

| Marker | Without it | With it |
|---|---|---|
| `gfx-wb-skip` = 0 / 1 | on on Android (`DM_GFX_WB_SKIP`), off elsewhere | off / on (A/B target `wbskip`; `NVN PERF wbskip` counts the drops) |
| `gfx-wb-census` (branch `android/wb-census`) | | diagnostic: writebacks per shader, changed bytes, read time, readers, bind and retire order |
| `DM_POOL_LOG` in `env` (branch `android/wb-census`) | | one log line per memory pool with its flags |
| `gfx-pipe-memo` = 0 / 1 | on on Android (`DM_GFX_PIPE_MEMO`), off elsewhere | off / on (A/B target `pipememo`; `NVN PERF pipememo` counts hits and misses) |

## Next

- The camp at scale 0.6 and the Den fight at 0.75 are GPU bound now, so more CPU savings would not show in their fps. The GPU per pass is the next thing to look at.
- On the CPU side, descriptors remain the biggest item (4.1–4.4 ms per frame in the sampled part breakdown, which its own clock reads inflate). Within them, uniform copies and per-image checks dominate.
