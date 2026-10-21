# NVN entry, object, command and lifetime inventory (B0, issue #11)

中文：[nvn-inventory.zh-CN.md](nvn-inventory.zh-CN.md)

This page inventories the NVN surface the game uses, the host objects behind it, the command records that carry GPU
state, and the lifetime rules between them, so that a second graphics backend (Vulkan, #13/#18) can be built against
evidence instead of guesses. Everything here is **source-level**: it says what this repository implements and how it
is wired. It is **not** proof that the game renders correctly on a new backend, and it is not a compatibility or
playability claim.

No game content is involved: every input below is a file already in the repository, and every number is reproducible
with the command in "Reproduce" at the end.

## What is measured, and what is not

| | |
|---|---|
| Measured | Which NVN names the game looks up and calls (static evidence), which of them the host implements, where each implementation lives, and how objects/commands/lifetimes are represented. |
| Not measured | Whether a given call site is reached at run time; whether the host ABI matches the Switch driver bit-for-bit; whether an NVN call has a correct Vulkan equivalent; any pixel result from the game. |
| Deliberately absent | Game files, shader packages, dumps, addresses beyond the ones already listed in `docs/DERIVED.md`. |

## Reproduce

```
python3 dm/gfx/tools/nvn_inventory.py                     # summary + full TSV on stdout
python3 dm/gfx/tools/nvn_inventory.py --tsv /tmp/nvn.tsv  # per-entry inventory to a file
python3 dm/gfx/tools/nvn_inventory.py --check             # consistency gate (see "Known gaps")
```

Inputs, all in the repository: `dm/gfx/nvn/names.txt` (the names the game looks up), `dm/gfx/nvn/nvn_slots.tsv` (the
static call surface produced by `dm/gfx/tools/nvn_slots.py` from the game image), `dm/gfx/nvn/hle_nvn.c`
(implementations and the no-op list `g_nops`).

## Entry surface

Measured (`nvn_inventory.py --check`, this branch):

| Quantity | Count | Where |
|---|---:|---|
| Names the game looks up via `nvnBootstrapLoader` | 523 | `dm/gfx/nvn/names.txt`, `nvn_count.h` (`NVN_NFUNC`) |
| Names with at least one static call site | 216 | `nvn_slots.tsv` (column `nuses` > 0) |
| Source-defined implementation entries (`NVN(...)` in `hle_nvn.c`); semantics and completeness not proven | 204 | `hle_nvn.c` (203 of them have call sites, 1 has no prototype) |
| Documented no-ops | 9 | `hle_nvn.c` `g_nops[]`, line 935 |
| Red-reporting stubs (`nvn_unimpl_entry`) | 310 | everything else; prints `【红】NVN 未实现被调用：<name>` once per name |

Note on documentation drift: `dm/gfx/README.md` states "203 implemented + 13 empty + 307 red stubs". The 13/307 numbers
no longer match the source (today: 9 no-ops, 310 stubs); the runtime banner printed by `nvn_nops_register()`
(`hle_nvn.c:973`) already reports the measured values. The root README is out of scope for this change and was not
edited; this is recorded here so that a future documentation fix starts from measured numbers.

Per-group counts and call-site totals (from the tool's summary) identify where the work is concentrated:
`CommandBuffer` 68 required entries / 196 call sites, `Texture` 36 / 128, `Device` 13 / 26, `Window` 10 / 10,
`Queue` 17 / 19, `Program` 3 / 21. The `CommandBuffer` group is where the recording surface sits; the `Texture` group
is where formats, layouts and uploads sit.

### Required entries that are still red stubs

This is the one place where "the game calls it" and "the host implements it" disagree:

| Entry | Static call sites | Current behaviour | Risk |
|---|---:|---|---|
| `nvnCommandBufferSetPatchSize` | 1 | red stub | If reached, tessellation patch size is dropped; draw state would be wrong, not loud |
| `nvnCommandBufferMultiDrawArraysIndirectCount` | 2 | red stub | If reached, those draws are silently skipped |
| `nvnCommandBufferMultiDrawElementsIndirectCount` | 2 | red stub | Same, indexed variant |
| `nvnCommandBufferDownsampleTextureView` | 2 | red stub | A downsample pass would be missing, i.e. wrong pixels rather than a crash |

What is known: the static call sites exist in `main` (evidence: `nvn_slots.tsv`, produced by `nvn_slots.py`).
What is **unknown**: whether those sites execute at run time. Nothing in this repository establishes that; deciding it
needs an instrumented run, which is outside this task. Until then these entries are "static call site present,
implementation absent" — an inventory gap, not a finding about the game's behaviour.

## Objects and lifetime

Every NVN object is stored **in place** in the memory the game allocated for it; `nvn_obj.h` fixes the layouts and
static-asserts that each fits NVN's reserved size. Recorded commands must therefore not hold pointers into objects that
may change or die: at record time the host takes an immutable **snapshot** (`snap_tex` `hle_nvn.c:283`, `snap_prog`
`:293`), keyed by object serial; a snapshot is retired `SNAP_KEEP` (8, `:253`) submits after the object is finalized or
changes serial.

| Object | Create / init | Mutable after init | Recorded as | Teardown | Replay representation |
|---|---|---|---|---|---|
| `NVNdevice` | `nvnDeviceInitialize` `hle_nvn.c:122` | flags, window origin, depth mode | — | `nvnDeviceFinalize` `:128` | global `g_dev` |
| `NVNmemoryPool` | `nvnMemoryPoolInitialize` `:177` | — | — | `nvnMemoryPoolFinalize` `:185` | zero-copy `MTLBuffer`, GPU address = CPU address |
| `NVNtexturePool` / `NVNsamplerPool` | `:191` / `:196` | registrations | `OP_SET_TEX_POOL` / `OP_SET_SAMP_POOL` | `:194` / `:199` | slot table `mtl_reg_tex` / `mtl_reg_samp` |
| `NVNtexture` | `nvnTextureInitialize` `:314` | — (builder-owned) | by snapshot in every copy/clear/draw | `nvnTextureFinalize` `:326` | cached `MTLTexture` per serial (`tex_mtl` `nvn_mtl.m:1384`), plus optional block-linear CPU view |
| `NVNsampler` | `nvnSamplerInitialize` `:399` | builder only | bound handle | `:402` | packed `samp_par` |
| `NVNprogram` | `nvnProgramInitialize` `:406` | `nvnProgramSetShaders` `:410` | `OP_BIND_PROGRAM` | `:409` | `NvShader`: `MTLFunction` + reflection arrays |
| `NVNsync` | `nvnSyncInitialize` `:420` | `nvnQueueFenceSync` `:424` | not recorded | `:421` | submit fence value; `nvnSyncWait` `:427` blocks on the GPU |
| `NVNevent` | `nvnEventInitialize` `:435` | GPU/CPU signal `:443` | `OP_SIGNAL_EVENT` | `:440` | value written only after the Metal command buffer completes |
| `NVNqueue` | `nvnQueueInitialize` `:465` | — | submits handles | `:469` | single Metal queue; `nvnQueueFinish` `:474` drains |
| `NVNcommandBuffer` / handle | `nvnCommandBufferInitialize` `:481`, memory via `:538` | recorded state | `OP_*` (see below) | `nvnCommandBufferFinalize` `:484` | replay walks the record stream (`mtl_exec` `nvn_mtl.m:5463`) |
| `NVNwindow` | `nvnWindowInitialize` `:988` | crop `:998`, acquire `:1013` | present `:1266` | `nvnWindowFinalize` (no-op, `g_nops`) | swapchain textures + present queue + pacer |

Failure and rollback semantics that a new backend must keep: invalid copy pitches are rejected before any memory is
addressed (`nvn_copy_pitch.h:8`), invalid crops are refused rather than stretched (`t_present_crop.m:61-66`), and
resource lifetime is guarded by the serial/snapshot mechanism above rather than by "it usually works".

## Commands

The command stream is a byte stream of `op_hdr {op,size}` records (`nvn_obj.h:88`) written into command memory the game
provides; `nvnCommandBufferEndRecording` returns an opaque handle (`hle_nvn.c:564`). There are **40 record operations**;
the list below and the one in the tool are a **manual transcription**, and the tool parses the op enum of `nvn_obj.h` and
fails when the two disagree, so the list does not certify itself:

| Class | Ops | CPU-visible effect |
|---|---|---|
| Control | `OP_END OP_JUMP OP_BEGIN OP_CALL` | none (nested handles re-execute) |
| Sync / counters | `OP_SIGNAL_EVENT OP_REPORT_COUNTER OP_RESET_COUNTER` | after GPU completion only |
| Memory | `OP_COPY_B2B OP_UPDATE_UB` | after GPU completion only |
| Render targets | `OP_SET_RT OP_CLEAR_COLOR OP_CLEAR_DS OP_CLEAR_TEX` | none until replay |
| Fixed state | `OP_VIEWPORTS OP_SCISSOR OP_DEPTH_RANGE OP_STENCIL OP_POLY_OFFSET OP_BLEND_COLOR OP_BARRIER` | none |
| Bindings | `OP_BIND_PROGRAM OP_BIND_STATE OP_BIND_VATTR OP_BIND_VSTREAM OP_BIND_VBUF OP_BIND_UBO OP_BIND_SSBO OP_BIND_TEX OP_BIND_SAMP OP_BIND_IMG OP_SET_TEX_POOL OP_SET_SAMP_POOL` | none |
| Draw / dispatch | `OP_DRAW OP_DRAW_IDX OP_DISPATCH` | none |
| Copies | `OP_COPY_B2T OP_COPY_T2B OP_COPY_T2T OP_COPY_STRIDE OP_COPY_IMAGE_STRIDE` | none |

Two properties matter for #18: (a) the recorded state is *not* a Metal/Vulkan object graph, it is a flat record
stream, so a Vulkan backend can replay the same stream without changing the interface layer; (b) everything the CPU can
observe (event values, counters, buffer contents) is deferred until after GPU completion — the readback and event
expectations in the acceptance scene depend on exactly that.

## Presentation

`nvnWindowAcquireTexture` (`hle_nvn.c:1013`) hands out the next swapchain index and a sync object; `nvnQueuePresentTexture`
(`:1266`) enqueues a copy of that frame's crop and samples the swapchain texture into the drawable. Crop is copied at
enqueue time, so changing the window afterwards cannot alter an already queued frame (`t_present_crop.m:67-80`).
Sampling goes through `present_src` (`nvn_mtl.m:5903`), which builds a non-sRGB view for an sRGB swapchain so stored
bytes are read unchanged. Cadence is the pure function `nvn_pace`/`nvn_pace_hz` (`nvn_pace.h`), so a present-time
policy can be tested without a device.

## Shaders

Offline chain (no run-time translation, see `dm/gfx/README.md`): Maxwell machine code → `dmsh_xlate` (C#, Ryujinx
translation library, `dm/gfx/tools/dmsh_xlate/Program.cs`) → SPIR-V → spirv-cross → MSL → metallib. Shaders are keyed
by the first 16 hex digits of sha256 over the 0x880-byte control section; reflection is converted once per program by
`nvn_refl_from_json` (`nvn_refl.m:22`) into fixed-size arrays (`nvn_refl.h`), and answers that cannot be read from
machine code live in `nvn/shader_facts.tsv`. The interface-layer side of this contract is now covered by
`t_accept_shader.m` (see below); the translator side is #12's scope.

## Acceptance scenes delivered here

`dm/gfx/tests/t_accept_scene.m` and `dm/gfx/tests/t_accept_shader.m` run on the current Metal backend, use only data
they generate themselves, and compute every expectation from a formula. They are **executable now** and are wired into
`make -C dm/gfx/tests run`.

| Scene | Check | Expected output |
|---|---|---|
| Resources / state | 8×4 RGBA8 linear texture uploaded from a buffer at row stride 64 (tight is 32) | all 32 texels equal `PX(x,y)`, i.e. the padding is skipped |
| Resources / state | Texture→buffer writeback at row stride 48 | 32 texels equal `PX(x,y)`, 16 trailing bytes per row untouched |
| Submission | Event signalled in the command buffer | 0 before submit, 42 after the fence wait returns 1 |
| Lifetime | Finalize, recreate at the same pool offset, clear | 32 texels equal the clear value (no stale contents) |
| Presentation | Two whole-frame presents into the same 8×4 target; the second uses a different source texture cleared to a different value | frame 1: 32 pixels equal `{0,255,128,64}` (clear 0/1/0.5/0.25 in RGBA8); frame 2: all 32 equal `{255,64,0,128}` (clear 1/0.25/0/0.5) and none still holds frame 1's value |
| Shader interface | Self-written route-B reflection json, 10 binding kinds | kind/reg/arg_id/dummy-type values as asserted; c1 bytes `0x0A 0x0B`; 5 negative inputs rejected |

Current results (this branch, macOS 27.0.1, Apple M4): `t_accept_scene` 26/26, `t_accept_shader` 28/28, both exit 0.
The scene finalizes the textures, event, syncs, command buffer and queue it created before the backing pool and the
device, so nothing is intentionally left to process exit.

These scenes are **host Metal fixtures, not a Vulkan or Android acceptance result**. They run against the macOS Metal
backend (`dm/gfx/nvn/nvn_mtl.m`); this base commit has no `dm/android` tree at all, so nothing here says anything about
Android behaviour, Vulkan validation layers or the Adreno target, and no Android compatibility is claimed. What the
scenes do establish is that the NVN entry semantics and the expected numbers are well-defined on the host. Porting them
(same formulas, same inputs) to a Vulkan backend is part of #13 (resources) and #18 (submit/present), and requires the
A0 baseline (#2).

## Proposed tests (not implemented here, on purpose)

| Proposed test | Why it is not executable today | Follow-up |
|---|---|---|
| Same three scenes on the Android/Vulkan backend, pixels read back from the swapchain | No public Vulkan backend in `origin/main`; needs A0 baseline acceptance | #13, #18 |
| Translated-shader numerical comparison (SPIR-V/MSL output vs reference values) | Needs the shader package and the translator output; both are game-derived and stay outside the repository | #12 |
| Multi-frame present: resize, pause/resume, surface destroy/recreate | Needs the Android app shell and a device | #18 (with #14) |
| Instrumented run deciding whether the four red-stub entries are reached | Needs a recorded run of the real game; forbidden in this task | #13 (draw state), #18 (present) |

## Known gaps, in priority order

1. **P1 — four required entries are red stubs** (table above). Either implement them or prove they are unreachable.
2. **P1 — documentation drift** on the implemented/no-op/stub counts (root README says 203/13/307; measured 204/9/310).
3. **P1 — no Vulkan backend in the public tree**, so no scene above has a Vulkan counterpart yet; #13/#18 depend on A0
   (#2) publishing the baseline.
4. **P2 — reflection limits** are fixed (`NVN_REFL_MAXB` 64 bindings, `NVN_REFL_MAXV` 32 vertex inputs, 4 KiB c1);
   exceeding them is rejected loudly, but a Vulkan descriptor layout may need a different bound. Revisit inside #12.
5. **P2 — single-queue assumption** in the NVN command layer (`g_nops` rationale at `hle_nvn.c:938-946`): the
   WaitSync/WaitEvent no-ops are correct only while everything is on one queue. A Vulkan backend with more than one
   queue must turn them into real waits. This is a design constraint for #18, not a bug today.
