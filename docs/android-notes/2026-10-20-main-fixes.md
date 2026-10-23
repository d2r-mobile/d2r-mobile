# Fixes from `main` and upstream on `linux-build`, and what was left for the full merge (2026-10-20)

中文：[2026-10-20-main-fixes.zh-CN.md](2026-10-20-main-fixes.zh-CN.md)

`linux-build` had not merged `main` since its base `9125cec` (2026-10-16), and `main` had 70 more commits. Most of them are the maintainer's own versions of PRs that `linux-build` already carries in their original form. A trial merge conflicted in 13 files, 74 hunks of them in `xbg_game.cpp`. `main` also reads the game data from `Download/S2X/D2R Mobile/data` and checks the touch assets at start, both of which would change the phone setup. As decided with the user, the full merge waits until `linux-build` is in a state we are happy with. Meanwhile, `main`'s real bug fixes are ported as ordinary commits (`9a8647a`..`526f03f`).

## Ported

| Commit | From | What it fixes |
|---|---|---|
| `9a8647a`, `7591919`, `a39fb17` | `main` `a222bc2`, `97dab02`, `e392533` (upstream #68) | `nvnCommandBufferSetCopyImageStride` was missing: buffer↔texture copies ignored the layer (image) stride. Tests `copy_stride_test.py` and `t_copy_pitch` pass. |
| `8cada09` | `main` `07ea121` | Frame overlap (on by default here): a command-buffer wait on an event whose signal is still deferred now completes the GPU work first. Before, it spun until the fence timeout. The host's CPU event initialize, finalize and signal now land a pending value first, so a later GPU value cannot overwrite them. The presenter adds a read-to-write barrier after its blit and waits for the queue before teardown. The test keeps only the gates that apply here. |
| `f6b3b23` | `main` `50dce1a` | The manifest asked for Vulkan 1.3; the engine's Vulkan 1.1 path is what the Redmi runs. It now asks for 1.1, and startup logs `NVN compatibility format_feature_flags2=0 depth16_as_d24=1` on the Redmi. The depth-bias part of `50dce1a` was already here. |
| `b1a2485` | `main` `65721a2` | Renderer shutdown restores the write-tracked guest pages and frees the tracked copies, so a restarted renderer does not inherit protected pages. Unlike `main`, tracking stays enabled for the next start. |
| `526f03f` | upstream `source snapshot` (#66) and `source snapshot`, via `main` `6a03a8a` | An unresolved weak import stays 0, so the guest can test for an optional function (test `t_loader_weak` passes on the phone). The guest address-space reservation steps down from 5120 MiB to 4864 MiB on ENOMEM instead of failing. |

**Device check** (`526f03f`, APK SHA-256 `7b8ccc12f56b2053184c516f425c9433b87ddbdd4601065359f29d2f2d487280`, camp at fixed 0.75, cold start from 30.2 °C):

| | L1 (`6a6982d`) | V1 (`526f03f`) |
|---|---|---|
| GPU per frame, `NVN PERF` | 14.12 ms | 14.11 ms |
| Main pass / pre-pass / shadow map | 5,308 / 1,028 / 674 µs | 5,246 / 1,003 / 674 µs |
| fps | 60.0 | 60.0 |

The seeded Den of Evil fight rendered the Fallen, the shaman, the zombies and the Sorceress correctly.

## Not ported, and why

- **`2244f28`, pipeline-memo lifecycle generations.** `linux-build`'s memo is cleared on every pipeline eviction and every cache clear, and evicted pipelines are destroyed through the retire queue. The objects whose pointers it keys on are freed only by those clears.
- **`f1f96d0`, texel residency bounds.** Here a GPU write to a resident texel range bumps `texel_write_epoch`, and the next use lands the result with `cpu_access` first. That is correct, at the cost of a sync, and the camp shows no write-back syncs (`sync_wb=0`).
- **`cc91eb1`, image CPU reads before the write-back alias lookup.** It depends on `main`'s preflight design. **A known difference:** here a texture upload does not land a pending GPU write-back that overlaps the texture's memory first. In a rare aliasing case the image could stay stale for a while (stable textures are rechecked every 16th submission). Adding the sync inside `get_image` without the preflight could free references in the middle of a draw.
- **`a88ad58`, native BC gating.** It keeps BC1/2/3/7 decoded for the iOS ASTC pack's copies; Android has no ASTC pack. `linux-build` samples all BC formats natively on the Redmi.
- **`1e6a743`, quad sources with integer consumers.** It fixes `main`'s quad guard; `linux-build` uses its own implementation (derivatives inside the helper).
- **The rest of `6a03a8a` (touch semantics, release-to-switch, minimap lift, i18n):** these go with `main`'s newer Android touch code, so they are left for the full merge.

## Upstream since the team's last sync

Upstream `d2r-mobile` has two commits with shared code that `main` does not have yet: `source snapshot` (community fixes, mostly Mac and iOS; shared auto-buff, cursor and main-button aim dead zone changes) and `source snapshot` (the Spanish language switch). The upstream fix `source snapshot` (#67) was withdrawn by `source snapshot` (#73) because it aborted loading.
