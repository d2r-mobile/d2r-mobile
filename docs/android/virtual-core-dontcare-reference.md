# `IdealCoreDontCare` (`core_id = -1`) reference semantics

Issue: .
Status: **bounded pinned-reference code read; not target-kernel or runtime proof.**

Pinned source:
[Atmosphère `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`](https://github.com/Atmosphere-NX/Atmosphere/tree/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8).

This note follows the public pinned Atmosphère implementation at commit
`6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8` for `core_id = -1` only. The raw
files were fetched at that commit and the cited line numbers were checked
against the fetched files. It does not analyze `-2`, `-3` or the
current-thread pseudo handle, and it does not execute any SDK, game, kernel or
phone code.

## 1. Validation before the state transition

`SetThreadCoreMask` (`kern_svc_thread.cpp:161-190`):

- Resolves the thread handle in the current process; a missing, foreign or
  wrong-process handle returns `ResultInvalidHandle` (`:163-167`).
- Rejects a mask with any bit outside the process core mask with
  `ResultInvalidCoreId` (`:175-176`).
- Rejects a zero mask with `ResultInvalidCombination` (`:177`).
- For a valid virtual core, requires that core's bit in the mask (`:180-181`).
- For a non-valid core, accepts only `IdealCoreNoUpdate` or
  `IdealCoreDontCare`; other values return `ResultInvalidCoreId` (`:182-184`).

`IdealCoreDontCare` is the value `-1`
(`svc_types_common.hpp:379-381`). Therefore, for `core_id = -1`, any non-zero
mask that is a subset of the process core mask is accepted without a
core-bit check.

## 2. Post-validation state transition

`KThread::SetCoreMask` (`kern_k_thread.cpp:750-849`) performs the operation
after validation:

1. It takes the thread activity-pause light lock and then the scheduler lock
   (`:754-760`).
2. Because `-1 != IdealCoreNoUpdate`, it writes `-1` to the virtual ideal core
   (`:763-764`). This **replaces any previous ideal core**; it does not
   preserve it.
3. It writes the requested mask to the virtual affinity mask (`:771-772`).
4. Because the core is negative, it does not index the virtual-to-physical
   core map for the ideal core (`:774-777`). It converts every set bit of the
   virtual mask through the board map (`:779-780`).
5. If core migration is not disabled, it sets the guest physical ideal core to `-1`,
   sets the converted guest physical affinity mask, and, if the currently active
   guest physical core is no longer allowed, selects the **highest set guest
   physical core in the converted mask** as the new active core (`:782-798`).
6. If migration is disabled, it stores the converted guest physical ideal/mask
   in the original-restore fields instead (`:799-803`).
7. After the scheduler-lock block it may retry or wait for a pinned/current
   thread to leave the old mask (`:806-846`). It can return
   `ResultTerminationRequested` at `:835` **after the virtual and guest-physical
   fields above have already been changed**; there is no rollback path.
8. Otherwise it returns success (`:848`).

The virtual readback after a successful `-1` call is therefore
`ideal_core = -1` and the requested mask. `KThread::GetCoreMask`
(`kern_k_thread.cpp:720-730`) reads those two virtual fields. In the normal
no-migration-disabled path, the guest physical readback is `-1` plus the
converted guest physical mask (`kern_k_thread.cpp:733-747`).

`KScheduler::OnThreadAffinityMaskChanged` (`kern_k_scheduler.cpp:328-337`)
updates the runnable priority queue's affinity and marks a scheduler update;
this is scheduler state, not merely a virtual field.

### 2.1 Bounded `-1` transition table

| Condition | Result | Virtual state |
|---|---|---|
| valid owned handle; non-zero subset mask; `core_id = -1` | success unless the late termination path applies | ideal `-1`; mask replaced with the request |
| mask contains a bit outside the process core mask | `ResultInvalidCoreId` | unchanged |
| mask is zero | `ResultInvalidCombination` | unchanged |
| invalid or foreign thread handle | `ResultInvalidHandle` | unchanged |
| negative core other than the accepted special values | `ResultInvalidCoreId` | unchanged |
| late termination request in the wait/retry path | `ResultTerminationRequested` | already changed; no rollback |

`DontCare` does **not** preserve the previous ideal; `IdealCoreNoUpdate` is the
branch that preserves it. `NoUpdate` behavior is outside this note.

## 3. Core-mask conversion and active-core selection

`NumVirtualCores` is `BITSIZEOF(u64)` = 64
(`kern_select_cpu.hpp:60-68`). The NX board map has 64 entries
(`kern_cpu_map.hpp:22-31`): virtual cores `0..3` map to guest physical `0..3`,
virtual cores `4..62` map to guest physical `0`, and virtual core `63` maps to
guest physical `3`. `ConvertVirtualCoreMaskToPhysical`
(`kern_select_cpu.hpp:70-78`) ORs the mapped bits. The process-mask subset
check constrains which virtual bits can reach this conversion.

For `-1`, the guest physical ideal is negative, so the active-core fallback is
the highest set bit of the converted guest physical mask, not the process ideal
and not the original virtual ideal (`kern_k_thread.cpp:790-798`). This is
reference guest-board mapping; it does not require or authorize pinning
Android/Linux host threads to physical CPUs.

## 4. Reference vs owned model vs target

- **Pinned reference:** the operation changes virtual ideal/mask, guest
  physical ideal/mask, possibly the guest active core, and runnable scheduler
  queue state; it can block/retry and can return a termination result after
  mutation.
- **Current owned model:** the per-thread fields store only virtual core/mask.
  They do not expose guest physical ideal/mask, the guest active/current core,
  scheduler queue placement, migration-disabled original state, or
  pinned/termination wait/result behavior.
- **Actual target kernel:** not proven. The SDK callsite evidence establishes
  that the recorded `nnMain` call passes `-1`; it does not establish that the
  target kernel has this exact Atmosphère implementation or result behavior.

## 5. Model gap and possible future logical model

The current fields are insufficient to implement the observed `-1` semantics;
that is an implementation gap, not a proof that no owned implementation can
exist. A software emulator can model guest physical/active/scheduler state
without binding host threads to native CPUs. The pinned NX
virtual-to-physical map is guest-board semantics, not a host-affinity mandate.

A fuller software guest-core model would need a reviewed executable contract
covering at least:

- guest physical ideal/mask conversion and active/current-core observations;
- runnable queue placement and migration-disabled original state;
- pinned/current-thread wait/retry behavior;
- the late termination-requested result after mutation, including its
  non-atomic observable state;
- which host/native scheduling effects are explicitly excluded.

A virtual-only projection alone is incomplete reference emulation. A fuller
logical CPU model is possible, but it requires owner review and a new contract;
no runtime extension is approved in this round.

## 6. Source anchors

Pinned raw files:

- [`kern_svc_thread.cpp:161-190`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_thread.cpp#L161-L190)
- [`kern_k_thread.cpp:750-849`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/kern_k_thread.cpp#L750-L849)
- [`kern_select_cpu.hpp:60-78`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/include/mesosphere/kern_select_cpu.hpp#L60-L78)
- [`kern_cpu_map.hpp:22-31`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/include/mesosphere/board/nintendo/nx/kern_cpu_map.hpp#L22-L31)
- [`kern_k_scheduler.cpp:328-337`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/kern_k_scheduler.cpp#L328-L337)
- [`svc_types_common.hpp:379-381`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libvapours/include/vapours/svc/svc_types_common.hpp#L379-L381)

## 7. Unknowns

- Actual target-kernel implementation and result behavior.
- The game process's actual core mask and the numeric current-thread handle.
- Whether the recorded `nnMain` path executes.
- `-2`, `-3` and current-thread handle semantics are outside this note.
