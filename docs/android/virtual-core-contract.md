# Virtual core mask contract: GetInfo ID 0 and SetThreadCoreMask

Issue:  (A5.1), phase 1.
Status: **phase-1 evidence plus bounded opt-in owned models for GetInfo 0 and
the narrow SVC `0x0f` state subset. No target inference, no physical-affinity
mapping and no production SDK/game admission.**

Repository anchors use accepted main `source snapshot` for the SDK/static observations;
the runtime base for this slice is the owned-policy merge `source snapshot`. Private SDK observations are
aggregate static metadata only: no game/SDK bytes, raw logs, saves, keys or
private paths are published.

Evidence levels:

- **P — public ABI/reference**: libnx and Atmosphère at the pinned commits
  below. Atmosphère is a reference mechanism, not the proprietary kernel.
- **S — static SDK metadata**: direct wrapper/callsite evidence; it does not
  prove that the call executes or returns.
- **R — repository**: current code at `source snapshot`.
- **U — unknown**: no protocol, reachability or value proof.

## 1. Public ABI layers

The C wrapper, the raw 64-bit SVC, and the SDK C++ wrapper are separate
layers. Do not use the libnx C declaration as the kernel mask width.

| Layer | Operation | Inputs | Outputs | Limit |
|---|---|---|---|---|
| libnx C wrapper | GetInfo | `x0` out pointer, `w1` id, `w2` handle, `x3` sub-id | `w0` Result; `x1` stored into the caller's `u64` | The wrapper saves/restores `x0` and performs an unconditional `str x1` after the SVC, including on failure |
| raw 64-bit SVC | GetInfo `0x29` | `x0` out pointer, `w1` id, `w2` handle, `x3` sub-id | `w0` Result, `x1` value | The kernel writes a 64-bit value on the success path; the failure value of `x1` is **U** here |
| libnx C wrapper | SetThreadCoreMask | `x0` handle, `w1` s32 core, `x2` u32 mask | `w0` Result | C API cannot express a mask above 32 bits; this is not a kernel-width proof |
| raw 64-bit SVC | SetThreadCoreMask `0x0f` | `x0` handle, `w1` s32 core, `x2` `uint64_t` mask | `w0` Result | No output pointer; the raw 64 ABI uses a 64-bit affinity mask |
| libnx C wrapper | CreateThread | `x0` out pointer, entry/arg/stack, `w4` priority, `w5` s32 core | `w0` Result; `w1` handle | The wrapper stores `w1` unconditionally after the SVC |
| raw 64-bit SVC | CreateThread `0x08` | `x0` out pointer, entry/arg/stack, `w4` priority, `w5` s32 core | `w0` Result, `w1` handle | One ideal core id, not a mask; failure out-pointer behavior is **U** unless separately proved |

`CUR_PROCESS_HANDLE` is `0xFFFF8001`
([libnx `svc.h:10-11`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L10-L11)).
The GetInfo C wrapper saves the out pointer and unconditionally stores `x1`
after the SVC
([libnx `svc.s:248-254`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/source/kernel/svc.s#L248-L254)).
The SetThreadCoreMask C declaration takes a `u32` mask
([libnx `svc.h:587-592`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L587-L592)),
but the raw 64 ABI takes `uint64_t` and forwards the full value
([Atmosphère `kern_svc_thread.cpp:302-304`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_thread.cpp#L302-L304)).
The `svc.s` thunk itself only executes `svc 0x0F`; it does not define a
32-bit kernel mask contract
([libnx `svc.s:100-102`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/source/kernel/svc.s#L100-L102)).
The SDK C++ symbol `_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim`
uses `unsigned long`, which is 64-bit on the LP64 SDK ABI; its exact
callsite arguments and upper-bit policy remain **U**.

GetInfo ID 0's selector is `InfoType_CoreMask = 0`, documented as a bitmask
of allowed core IDs
([libnx `svc.h:186`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L186)).
The current-process handle is a pseudo handle, not a normal owned process
handle; the static SDK path uses `0xffff8001` as recorded below.

### 1.1 GetInfo ID 0 rules

The public reference implementation:

```text
InfoType_CoreMask -> *out = process->GetCoreMask()
```

([Atmosphère `kern_svc_info.cpp:43-44`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_info.cpp#L43-L44)).

The selected handler:

- requires `info_subtype == 0`; otherwise `ResultInvalidCombination`
  ([Atmosphère `kern_svc_info.cpp:155`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_info.cpp#L155));
- resolves the supplied handle to a `KProcess`; a missing object is
  `ResultInvalidHandle`
  ([Atmosphère `kern_svc_info.cpp:182-185`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_info.cpp#L182-L185));
- returns the process's allowed mask, not the current thread's affinity mask.

For this contract the supported handle is the current-process pseudo handle.
Using an arbitrary process handle, a non-zero sub-id, or an unowned process
handle is **U** for this runtime and must fail rather than be guessed.

**Failure output is not an unchanged-output guarantee.** The libnx C wrapper
performs `str x1` after every SVC return, so a failed call can overwrite the
caller's `u64` with an unspecified `x1`. The current owned gate happens to
assign `r[1]` only on success, but that is a local runtime policy, not a
wrapper or Horizon ABI guarantee. A raw-gate test may assert that local policy
only if it labels it as owned behavior; a wrapper-level test must not assume
the output storage is unchanged.

### 1.2 SetThreadCoreMask rules

The raw 64 ABI implementation takes a thread handle owned by the current
process and validates the preferred core and **full 64-bit affinity mask**
before setting state
([Atmosphère `kern_svc_thread.cpp:161-190`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_thread.cpp#L161-L190)):

- invalid/foreign handle: `ResultInvalidHandle`;
- `affinity_mask` must be non-zero and a subset of the 64-bit process core
  mask; a mask outside it is `ResultInvalidCoreId`, zero is
  `ResultInvalidCombination`;
- when `core_id` is a valid virtual core (`0 <= core_id <
  cpu::NumVirtualCores`), its bit must be present in `affinity_mask`;
  otherwise `ResultInvalidCombination`;
- when `core_id` is not a valid virtual core, only the public special values
  below are accepted; other values are `ResultInvalidCoreId`.

The public special values at the pinned reference are:

| Value | Name | Acceptance rule |
|---|---|---|
| `-1` | `IdealCoreDontCare` | accepted only in the non-valid-core branch with a valid non-zero subset mask |
| `-2` | `IdealCoreUseProcessValue` | selects the process ideal core and a mask of that one core; the ordinary subset check is bypassed by the reference implementation |
| `-3` | `IdealCoreNoUpdate` | accepted only in the non-valid-core branch with a valid non-zero subset mask |

Source: [Atmosphère `svc_types_common.hpp:379-381`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libvapours/include/vapours/svc/svc_types_common.hpp#L379-L381).
The exact kernel-side selection behavior of `DontCare` and `NoUpdate` after
acceptance remains **U** for this contract; only the validation and state
ownership rules are claimed.

The libnx C wrapper exposes `u32`, while the raw 64 ABI and the SDK C++
wrapper use a 64-bit mask. Do not discard bits 32..63 silently. A raw mask
with those bits set is subject to the full process-mask subset rule; whether
the SDK ever passes them is **U**. The C wrapper simply cannot express them.

### 1.3 CreateThread relation

`CreateThread` accepts one `s32` core id and validates it against the process
core mask; `IdealCoreUseProcessValue` selects the process default. A core
outside the valid range or outside the process mask is `ResultInvalidCoreId`
([Atmosphère `kern_svc_thread.cpp:28-37`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_thread.cpp#L28-L37)).
It has no affinity-mask argument
([libnx `svc.h:539-544`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L539-L544)).
The C wrapper stores `w1` unconditionally after the SVC
([libnx `svc.s:55-60`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/source/kernel/svc.s#L55-L60));
a failed call's caller-visible handle must not be assumed unchanged or valid.

Therefore the coherent model is:

- `GetInfo ID 0` reports the process-level allowed mask;
- `CreateThread` chooses one ideal core that must be in that mask;
- `SetThreadCoreMask` may later set a per-thread subset mask and preferred
  core, subject to the same process mask;
- none of these imply host/physical CPU affinity. Mapping a guest virtual
  mask to host scheduling is a separate, unapproved contract.

## 2. Static SDK evidence

The following are direct static references from the bounded SDK metadata. They
are **S**, not execution or reachability proof.

| Static observation | Evidence |
|---|---|
| `nn::os::detail::ThreadManagerImplByHorizon::GetThreadAvailableCoreMask` callsite `0x350764` | resolved wrapper `0x39fbec`, SVC `0x29`; straight-line constants `w1=0`, `w2=0xffff8001`, `x3=0` |
| `nn::os::detail::ThreadManagerImplByHorizon::SetThreadCoreMask(ThreadType*, int, unsigned long)` callsite `0x350698` | resolved wrapper `0x39fae4`, SVC `0x0f`; no suffix constant set is available for the arguments |
| main `nnMain` references | `0x1355d0` → `GetThreadAvailableCoreMask`; `0x1355e0` → `SetThreadCoreMask`; exact argument values are not in the bounded metadata |
| SDK wrapper symbols | `_ZN2nn2os26GetThreadAvailableCoreMaskEv`; `_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim`; `_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii` |
| Current process handle constant | `0xffff8001` in the selected GetInfo path, matching `CUR_PROCESS_HANDLE` |

The static path is sufficient to prove the selector (`ID 0`), the SVC selection
(`0x29`/`0x0f`), the wrapper argument types, and the current-process handle
value. It does **not** prove:

- that `nnMain` executes either call;
- the actual preferred-core or mask arguments at `0x1355e0`;
- whether the wrapper's abort path is reached on any error;
- the process's actual mask value or virtual-core count.

The existing SDK metadata reports no execution of a real initializer or game.

## 3. Current runtime state at `source snapshot`

| Item | Current state | Consequence |
|---|---|---|
| GetInfo ID 0 | `dm/android/runtime/memory.c` now returns `0x4201` when unconfigured; a configured host returns its stored 64-bit mask (section 7) | Success remains conditional on explicit host configuration |
| SVC `0x0f` | `dm/android/runtime/sync.c` implements only the owned-state subset in section 8; unconfigured/special/pseudo forms return `0x4201` | No physical affinity or broad public behavior |
| CreateThread | `dm/android/runtime/sync.c` validates configured cores/masks and initializes per-thread virtual state (section 7/8) | Unconfigured path preserves the pre-existing synthetic behavior |
| Legacy host | `dm/host/hle_svc.c:225-228` hardcodes GetInfo 0 to `7`; `dm/host/hle_thread.c:481-486` returns success for `0x0f` with loose updates | Legacy macOS host behavior only; not Android production evidence and must not be reused as the contract |

The hardcoded legacy `7` is not adopted as a mask here. No initial mask value
is proven by public ABI or current static metadata.

## 4. Unknowns and precise blocker

The protocol and ownership shape are proven at the public/reference and static
edge levels. The following are still **U** and block a runtime implementation
until resolved or explicitly chosen by the owner:

1. **Initial allowed mask.** No value is proven for the game process. The
   current host's `7` is legacy bookkeeping, not evidence. A host policy must
   define a non-zero mask without claiming a physical core set.
2. **Virtual core count / process ideal core.** The reference exposes
   `cpu::NumVirtualCores` and a process ideal core, but the SDK's actual values
   are not established by this inventory. Do not hardcode four cores merely
   because the reference has a bounded range.
3. **Main callsite arguments.** `0x1355e0` is a direct reference; the exact
   core and mask arguments, and whether the path executes, are unknown.
4. **`DontCare`/`NoUpdate` post-validation behavior.** Acceptance rules are
   public; exact selection/update behavior in the target kernel is not proven.
5. **Result mapping.** Public names are known (`ResultInvalidHandle`,
   `ResultInvalidCoreId`, `ResultInvalidCombination`); the current runtime
   uses local numeric results. Do not equate the current default `0x4201`
   with the final Horizon result until the mapping is reviewed.
6. **Host affinity.** No permission to call host affinity APIs or claim a
   physical-core mapping is implied by this contract.

This is the single phase-1 blocker list; it is not evidence of implementation
or a request to broaden the audit.

## 5. Ready owned negative-acceptance plan (phase 2, after root review)

One owned test target, using actual rewritten SVC gates and a host-owned
virtual mask `M` selected by the reviewed policy:

1. **GetInfo 0 positive:** call with `id=0`, current-process handle and
   `subid=0`; expect `w0=0`, 64-bit `x1=M`, and no host affinity call.
2. **GetInfo 0 negatives at the raw gate:** wrong process handle → explicit
   invalid-handle result; non-zero subid → invalid-combination. Assert the
   Result and owned transaction state. Do not assert the caller's output
   storage is unchanged: the libnx C wrapper performs an unconditional
   `str x1`, so a wrapper-level failure can overwrite it. If the owned gate
   preserves `x1` on failure, label that as a local policy test, not an ABI
   guarantee.
3. **SetThreadCoreMask positive:** valid thread handle, valid core in `M`,
   non-zero subset mask; expect Result 0 and visible per-thread state only.
4. **SetThreadCoreMask negatives:** zero mask → invalid-combination; mask
   outside `M` → invalid-core; valid core absent from mask → invalid-combination;
   invalid core values → invalid-core; wrong handle → invalid-handle.
5. **Raw high-bit mask:** at the raw 64-bit SVC, set a bit above 31 that is
   outside `M` and expect invalid-core with no state change. Do not route this
   through the libnx `u32` C wrapper, and do not silently truncate the mask.
6. **Special values:** `-1` and `-3` accepted only with a valid non-zero
   subset mask; `-2` selects the process default; no unsupported special value
   silently succeeds.
7. **CreateThread relation:** core outside `M` fails; a valid core in `M`
   succeeds; `-2` uses the process default; no physical affinity is observed.
   A failed wrapper call must not be asserted to leave the caller's handle
   storage unchanged; only the Result and owned state are contractual.
8. **Failure atomicity:** every negative case leaves the previous virtual
   state and host scheduling unchanged; a failed call allocates/retains no
   handle or mask record.
9. **Sanitizer/build:** ASAN/UBSAN test through the real gate; Android
   cross-build for changed runtime units; no device/game claim.

This plan is ready for review but is **not implemented** in this phase.
The bounded implemented subsets are section 7 and section 8; they do not
close the unknowns above.

## 6. Source anchors

Repository:

- `source snapshot:dm/android/runtime/memory.c:130-161` — current GetInfo switch.
- `source snapshot:dm/android/runtime/svc.c:15-49` — current SVC dispatch.
- `source snapshot:dm/android/runtime/sync.c:204-211` — current CreateThread core check.
- `source snapshot:dm/host/hle_svc.c:225-228` and `dm/host/hle_thread.c:481-486` —
  legacy host behavior, not Android production.
- `source snapshot:docs/android/startup-services-inventory.md:143` — G1 ownership.

Public reference (pinned):

- libnx `20cbcfd90567f7bf84df7725f27e345e2164fa6b`:
  `nx/include/switch/kernel/svc.h:10-11` (`CUR_PROCESS_HANDLE`),
  `:186` (`InfoType_CoreMask`), `:539-544` (`svcCreateThread`),
  `:580-592` (`GetThreadCoreMask`/`SetThreadCoreMask` C declarations),
  `:845-855` (`svcGetInfo`);
  `nx/source/kernel/svc.s:55-60` (`svcCreateThread` thunk),
  `:100-102` (`svcSetThreadCoreMask` thunk),
  `:248-254` (`svcGetInfo` thunk).
- Atmosphère `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`:
  `libraries/libmesosphere/source/svc/kern_svc_info.cpp:41-44,127-185`;
  `libraries/libmesosphere/source/svc/kern_svc_thread.cpp:24-37,150-190`;
  `libraries/libmesosphere/source/svc/kern_svc_thread.cpp:302-304`
  (`SetThreadCoreMask64`, `uint64_t`);
  `libraries/libvapours/include/vapours/svc/svc_types_common.hpp:379-381`;
  Result mapping: `libraries/libvapours/include/vapours/results/svc_results.hpp:20,49-53`
  (`InvalidCoreId` 113, `InvalidHandle` 114, `InvalidCombination` 116 in
  module 1).

These line ranges were re-read from the raw pinned files, not inferred from
HTTP success. The ranges start at the named function/declaration and stop at
its closing statement; adjacent functions are not included as evidence.

## 7. Bounded owned opt-in implementation

The implemented slice is deliberately narrower than the generic contract:

- `dm_host_configure_virtual_cores(host, mask, preferred_core)` accepts a
  host-supplied 64-bit non-zero mask and one preferred core whose bit is set.
  It rejects zero, a core >= 64 and a preferred core outside the mask with
  `EINVAL`. It returns `EBUSY` without changing the host if the host is
  attached or a thread is active (sync wait/running/starting/pin or a thread
  stack lease). A plain attached main thread is sufficient to freeze policy.
- A host starts unconfigured. GetInfo ID 0 remains unsupported (`0x4201`)
  until this explicit call succeeds; it never infers a mask or core count.
- A configured GetInfo ID 0 returns exactly the configured 64-bit mask for the
  current-process pseudo handle and sub-id 0.
- Configured CreateThread accepts `-2` as the configured preferred core and
  accepts a core id only when `0 <= core < 64` and its bit is in the configured
  mask. Other values, including `-1`/`-3` and a high core outside the mask,
  return `ams::svc ResultInvalidCoreId` (`0xe201`) without state change.
- Unconfigured CreateThread preserves the pre-existing synthetic behavior;
  that behavior is not a game mask contract.
- SVC `0x0f` is implemented only as the narrower owned-state subset in
  section 8; all other forms remain explicitly unsupported. No physical
  affinity or native scheduling change is made.
- Configuration fields are host-local; a rejected call does not mutate another
  host. The owned test exercises the real rewritten gates for configured
  GetInfo0/CreateThread/SVC0x0f, unconfigured GetInfo0/0x0f, high-bit mask,
  two-host isolation and attach→EBUSY/no-change→detach→allowed policy.

The exact initial mask and target core count remain host policy; no legacy
hardcoded `7` is used. This slice does not complete the negative plan above or
claim real SDK initialization.

## 8. Bounded SVC 0x0f owned state

The SVC `0x0f` slice is intentionally narrower than the public reference:

- Only configured hosts participate. Unconfigured hosts return `0x4201`.
- Only a real owned kind-3 generation thread handle is accepted. The
  current-thread pseudo handle and stale/wrong-kind handles fail; stale or
  wrong-kind handles return the existing invalid-handle result without
  mutation.
- The requested core must be explicit and non-negative, `0 <= core < 64`;
  the mask must be non-zero, a subset of the configured host mask, and include
  the core bit. Zero mask/absent core bit return `ResultInvalidCombination`
  (`0xe801`); a core outside the host mask or `>= 64` returns
  `ResultInvalidCoreId` (`0xe201`).
- `IdealCoreDontCare`, `IdealCoreUseProcessValue` and `IdealCoreNoUpdate`
  (`-1`/`-2`/`-3`) remain unsupported (`0x4201`); no guessed special-value
  behavior is added.
- A successful call stores per-thread virtual core/mask state under the host
  mutex. CreateThread initializes that state from its validated core and the
  host mask. State is freed with the existing guest-thread object lifecycle;
  closing a handle removes it, and a recreated thread starts from CreateThread
  state, not stale values.
- No host scheduling, affinity syscall or physical-core mapping is performed.
- The owned test seam `dm_host_thread_virtual_core_state` reads the actual
  stored fields for verification; it is not an SVC readback or public contract.

The public reference validates virtual cores against `cpu::NumVirtualCores`
(its target bound is a Horizon property). The owned model's 0..63 domain is a
host-policy width for the explicitly configured 64-bit mask; accepting core
63 is a width test, not proof that Horizon has 64 virtual cores. The two
domains must not be conflated.
