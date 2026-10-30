# Frame overlap on the Redmi K50 Ultra (2026-10-17)

中文：[2026-10-17-frame-overlap.zh-CN.md](2026-10-17-frame-overlap.zh-CN.md)

Measured from 08:43 to 09:50 NZDT (UTC+13) on the Redmi K50 Ultra (Adreno 730, Vulkan 1.1.128, Android 15); device setup in [2026-10-16-redmi-k50-vulkan11.md](2026-10-16-redmi-k50-vulkan11.md). Build experimental build.
- **Same scene for every row:** cold start, Play on the saved Sorceress, stand at the Rogue Encampment arrival point for 75 s. The numbers are the last four 60-frame `NVN PERF v=1` windows.
- **Markers:** files in `files/dm/`, read once at start. Without them the behaviour is unchanged.

## Result

| Run | Markers | fps | GPU ms | CPU submit ms | present ms | submits per frame | blocked on a busy fence, ms per frame |
|---|---|---|---|---|---|---|---|
| D0 | none (default) | 7.6–7.8 | 60.4–62.2 | 112.4–114.8 | 1.6–1.8 | 3 | 60.6 (2 × event `complete_all`) |
| D1 | `gfx-defer-events`=1 | 7.7–8.0 | 58.8–63.6 | 109.6–113.9 | 1.6–1.8 | 3 | 57.1 (1 × slot-ring wrap) |
| E2 | `gfx-defer-events`=2 | 7.7–7.9 | 60.2–60.7 | 47.4–48.5 | **62.1–63.6** | 1 | 0 (the presenter's queue drain waits instead) |
| P0 | `gfx-present-overlap`=1 | 7.6–7.7 | 60.8–62.5 | 113.9–115.7 | 1.6–1.7 | 3 | 61.2 (2 × event) |
| P1 | defer=1 + overlap | 7.7–8.0 | 60.1–62.3 | 110.1–114.0 | 1.5–1.6 | 3 | 60.7 (1 × ring wrap) |
| **P2** | **defer=2 + overlap** | **17.7–18.1** | **41.8–41.9** | 45.8–46.4 | 2.1–2.2 | 1 | **0** |
| Z0 | none, final build | 7.6–7.9 | 59.4–61.8 | 112.6–115.5 | 1.7–1.8 | 3 | 60.2 (2 × event) |
| **Z2** | **defer=2 + overlap, final build** | **19.3–19.7** | 41.7–42.0 | 41.8–42.3 | 2.1–2.2 | 1 | **0** |

Z0 and Z2 use the final build (`recorded build`), which includes the epoch fix below; the other rows came before it.

The two changes only help together: **7.7 → 19.5 fps (2.5×)** in the camp. Either one alone moves the wait somewhere else.

Other scenes with both markers:
- **Character select (3D):** 24.7–27.0 fps, was 8.7–9.6 at acceptance.
- **Camp after background → resume:** 20.9–21.0 fps, at a spot with about 1,875 draws.

**GPU time:** with the GPU kept busy, its time per frame also drops, from about 61 to 42 ms. The likely cause is GPU DVFS holding a higher clock; this is not verified, because the kgsl sysfs files need root. The frame is now CPU bound: 42–46 ms of translation, 2 ms present, and about 8 ms of game code.

## Where the time went

**What the old counter missed.** `NVN PERF waits` counts every slot retire, including fences that have already signalled, so it doesn't show where the CPU stalls.

**New counter.** source snapshot` adds an `NVN PERF block_ms` line. It counts only waits on a fence that is still busy, split by call site:
- ring wrap, present, event, arena, `gpu_sync`, `complete_all`, poll, guest sync wait;

It also records which site triggered each submit.

What it shows in the camp:
- **The game never waits on its own syncs:** `legacy_wait_total=0`. It also never reads an event that is still pending: `cpu_reads_pending=0`. All the blocking is the backend waiting on itself.
- **Default:** the two command-buffer SignalEvents per frame each run `complete_all()`, so the CPU waits for all GPU work twice per frame.
- **`gfx-defer-events`=1** (submit at the signal, publish at retire) removes those waits. The frame then becomes 3 submissions, which wraps the 2-slot ring inside the frame, and the wrap waits for the first submission: about 57 ms.
- **The presenter waits for the blit fence and then `vkQueueWaitIdle` after every present.** Even with no wait inside the frame, frame N+1 can't start translating until frame N's GPU work has finished. Run E2 shows this: present takes 62 ms.

## The changes (branch experimental build, opt-in)

- **`gfx-defer-events` = `2`:** SignalEvent doesn't submit.
  - The value waits in the slot being recorded and is published when that slot retires, after its writebacks.
  - A camp frame stays one submission, so the ring doesn't wrap inside it.
  - `nvnEventGetValue` on a pending event still forces completion first (`xbg_game_event_read`), and the read is counted.
- **`gfx-present-overlap` = `1`:** the presenter keeps up to two blits in flight.
  - Each in-flight frame has its own command pool, acquire semaphore and fence. It waits only for the fence of the set it is about to reuse, instead of the fence plus the whole queue.
  - A failed present drains before swapchain teardown, as before.
  - **Why the source image is safe:** the engine's later barriers on the presented image are `ALL_COMMANDS → ALL_COMMANDS`. A released image is destroyed only when a later slot retires, and that fence covers the blit in queue order.

## Checks with both markers (P2)

| Check | Result |
|---|---|
| Picture | Same as the default build in the same scene; only the chickens and the torch, which are animated, differ |
| Walk with the stick | Pass |
| Menu → inventory, Back | Pass |
| Home, 12 s, back | Pass. Hot start, graphics suspend ("in-flight GPU work drained") → resume, swapchain rebuilt, picture back |
| Options → Save and Exit | Pass. Atomic save commits #3–#6, then character select |
| Play again → camp | Pass, 19.3 fps |
| Soak in the camp | **First try: failed.** fps fell from 19.3 to 16.2 in two minutes, from a leak (next section). **After the fix: pass.** 6 min, epochs steady at 15, PSS flat at about 1.93 GB, 17–22 fps |

## A leak that deferral exposed

`resolve_known_reports()` erased old occlusion-counter epochs only when no counter report was pending at all.
- **Why it never fired:** with deferred events a report is always in flight (8 in the camp), so the epochs were never erased. D2R resets the counter about 13 times per frame, and after 2.5 minutes the map held 30,462 epochs.
- **Cost:** every slot retire walks the map, so cleanup grew by about 1 ms per frame per minute.
- **Why the default build never showed it:** the `complete_all()` at each event clears everything.
- **Fix (source snapshot`):** erase every older epoch that no pending report names. A report only names the epoch current when it was recorded, and retire already skips query refs to an erased epoch.

## Thermals

After about an hour of runs the battery was at 38.5 °C. The CPU caps had dropped to:
- cpu4–6: 2.00 GHz, from 2.57;
- cpu7: 1.88 GHz, from 3.19.

With both markers the main thread used about 97% of one core. Every part of the frame scaled by the same ~25% between 17 fps and 22 fps windows, so in that soak the swing was CPU speed. Sustained fps now depends on CPU work per frame and on the phone's temperature.

## Submitting every N render passes (`gfx-submit-passes`), for the record

Measured earlier the same morning, with defer=1:

| N | fps | GPU ms | submits per frame |
|---|---|---|---|
| none (B1) | 7.8–8.0 | 58.8–61.6 | 3 |
| 4 | 8.1–8.5 | 73.5–76.8 | 17 |
| 8 | 7.5–7.8 | 76.3–82.2 | 10 |
| 16 | 7.9–8.4 | 64.3–66.9 | 6 |

Splitting a frame into more submissions lets the GPU start earlier but costs GPU time, probably tile store and load at the extra boundaries. Frame overlap supersedes it, and the knob has been removed.

## Next

- **Run P2 outside camp**, including combat, and on the vivo X200 Ultra (Adreno 830). Then decide whether both markers should become the default. *Update 18 Oct:* both are now the Android default ([round 2](2026-10-17-perf-round2.md), finding 19); a marker holding `0` turns either off.
- **Remaining CPU per frame in P2:** descriptor about 10 ms, draw about 9, snapshot about 7 (about 15,200 per frame), other about 7, pass about 6.
- **What deferral changes:** an event value now appears up to one frame later. The game can't tell, because a CPU read of a pending event completes it first.
