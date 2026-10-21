# Android runtime contract: async events, TLR preemption state and SVC 0x36

Issue:  (A1). Status: **design and
falsifiable acceptance plan only.** The [repo] rows describe existing code. The [design]
proposals added by this PR are not implemented, and nothing here is verified on Android. No game, SDK, original rtld or phone was executed to write it. The
production scanner, diagnostic quarantine and SDK admission block stay unchanged.

Evidence base: repository commit `source snapshot`
(paths below are relative to the repository root, `file:line` at that commit), plus
open-source reference implementations pinned to exact commits:

- libnx `20cbcfd90567f7bf84df7725f27e345e2164fa6b`
- Atmosphère `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`

Atmosphère's mesosphere kernel is a clean-room *reference mechanism*, not the
proprietary kernel. This contract does not require proprietary source. Each claim is
tagged:

| Tag | Meaning |
|---|---|
| **[repo]** | Read from this repository. |
| **[ref]** | Read from the pinned reference sources. |
| **[design]** | A proposed admission obligation. It does not exist in code today. |
| **[unknown]** | An open question. |

## 1. Observable events: current behavior vs. proposed obligation

"Guest resumes" means execution continues in the interrupted guest instruction stream.
"Host recovers" means the host keeps running safely, while the guest thread may end.
The *Current* column describes today's code. It does **not** guarantee detection or
stopping beyond what the cited lines do. The *Obligation* column describes what
admission (Issue #5) would require.

| Event | Current Android ARM64 behavior (`dm/android/runtime`) | Guest resumes / host recovers (current) | Proposed admission obligation |
|---|---|---|---|
| Gate round trip at a registered site | **[repo]** TP lookup, domain/active/SP/bounds checks, 160-byte frame below the RUN frame, service on the native stack, then x18/x29/LR/FPCR/FPSR restored (`bridge.S:16-157`). | yes / yes | Keep as is. Device evidence is still required. |
| Unregistered gate return address | **[repo]** `abort()` (`dispatch.c:26-28`). | no / no (process ends) | Keep fail-stop. |
| Gate reentry (`domain` SERVICE/EXITING or `active != 0`) | **[repo]** `brk #0xd18` (`bridge.S:43-47,186-187`). This covers signal handlers or callbacks that reach the gate during a service. | no / no | Keep fail-stop. |
| Async signal while the guest runs | **[repo]** The runtime installs no handler. A non-`SA_ONSTACK` host handler would run on the guest stack (production minimum 4096 bytes, `bridge.c:33`) with the native TP, and its effect is uncontained. bionic gives each pthread an alternate signal stack, which only `SA_ONSTACK` handlers use ([bionic `pthread_create.cpp`](https://android.googlesource.com/platform/bionic/+/refs/heads/main/libc/bionic/pthread_create.cpp), `__init_alternate_signal_stack`). | **[unknown]**, no containment exists | **[design]** Only handlers listed and reviewed as benign may return into the guest. Any other delivery must stop. No handler is listed today. |
| Async signal during a service (SERVICE domain) | **[repo]** The handler runs on the native stack, and gate reentry traps. | depends on the handler / **[unknown]** | Same rule as the previous row. |
| Host → guest callback | **[repo]** No entry point exists. Nested entry during SERVICE traps. | no / no | Out of envelope. |
| ExitThread (SVC 0x0a) from a started guest thread | **[repo]** Only a registered 0x0a site sets `action=1` (`bridge.c:207-212`). The terminal path leaves to RUN (`bridge.S:158-176`, `thread_stack.S` LEAVE), and the host detaches TLS and signals completion (`sync.c:169-175`). External or registered threads get `BAD_STATE` (`sync.c:257`). | no (thread ends) / **yes** | Keep. See §2 for TLR state at exit. |
| Guest entry returns without ExitThread | **[repo]** `brk #0` (`thread_stack.S` RUN `1:`). The sanitized build aborts via `dispatch.c:48`. | no / no | Keep fail-stop. |
| Native thread exit while attached | **[repo]** The TSD `release` aborts if the bridge is active or the thread is off the native stack (`context.c:21-30`, `bridge.c:77-82`). | no / no in that case | Keep. |
| Synchronous exception in guest code (SEGV/BUS/ILL/BRK) | **[repo]** No handler, so the platform default applies (debuggerd on Android). There is no Horizon user-exception emulation. | no / no | Out of envelope. |
| Host preemption of a guest thread | **[repo]** Guest threads are plain pthreads (`sync.c:240-245`). Virtual priority and affinity are not mapped (`sync.c:209-210`). | yes / yes | Preemption remains possible at any instruction. §2 defines what the guest may observe about it. |
| SVC 0x36 SynchronizePreemptionState | **[repo]** `dm_host_svc` `default` writes `0x4201` to x0 and returns (`svc.c:16,49-52`). | yes, with x0 clobbered / yes | **[design]** §3. |
| GetInfo(26 IsSvcPermitted) via SVC 0x29 | **[repo]** `svc.c:19` routes to `get_info` (`memory.c:130-160`). A call with handle 0 and sub-ID 0x36 returns `DM_SVC_INVALID_COMBINATION` at `memory.c:138`, because `r[3] != 0` for every ID other than 11. ID 26 is also absent from the switch, whose `default` returns `DM_SVC_NOT_IMPLEMENTED`. | yes (an error Result) / yes | **[design]** §3, rule 3. |
| Other unknown SVC numbers | **[repo]** `0x4201` in x0, and the guest resumes (`svc.c:16,49-52`). | yes / yes | **[design]** Returning a Result is allowed only for SVCs whose ABI returns a Result in x0. Any other unsupported SVC (non-Result, no admitted service-specific ABI) must fail-stop. Supported non-Result SVCs follow their verified ABI: GetSystemTick 0x1e returns a u64 in x0 (`svc.c:32-34`), ExitThread 0x0a does not return to the caller, and SVC 0x36 after admission is void (§3). |

The macOS owned tests run the same `dm/android/runtime` source, keyed by `tpidrro_el0`.
They are the only execution evidence (`dm/android/BASELINE.md:16-21`), and they do
not exercise signals or SVC 0x36. Linux execution is Issue #64's scope and is not
claimed here. The upstream macOS game host (`dm/host`) uses a different gate. Its
`default` SVC case logs once and returns **success** (`dm/host/hle_svc.c:248-250`),
and its ExitThread is at `dm/host/hle_thread.c:463`. That placeholder success must not
be ported.

## 2. TLR+0x100 / +0x102 state machine

### Layout and allocation

- **[ref]** `ams::svc::ThreadLocalRegion` (arm64) places `volatile u16 disable_count`
  at 0x100 and `volatile u16 interrupt_flag` at 0x102, with `static_assert`s
  ([`svc_thread_local_region.hpp:24-36`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libvapours/include/vapours/svc/arch/arm64/svc_thread_local_region.hpp#L24-L36)).
- **[repo]** On Android, the TLR is at `tls + 8192` inside an `8192+0x600` allocation
  (`context.c:60-70`). The runtime never reads or writes TLR+0x100..0x103. The guest
  locates the TLR only through the `DM_GATE_TLR_READ` gate (`dispatch.c:35`).
- **[repo] Initial bytes are zero on both allocation paths.**
  - Without accounting, the allocation comes from `calloc` (`context.c:60`).
  - With accounting, `dm_host_accounting_local_create` makes a slot inside the arena
    readable and writable (`accounting.c:129-147`). The arena lies inside the private
    anonymous `PROT_NONE` reservation (`heap.c:56`). The only shared remap covers the
    heap (`heap.c:68`), and `MapMemory` remaps only the stack region (`memory.c:56,64`).
  - Retired locals are reset with a fixed private anonymous `mmap` before their slot
    is freed (`accounting.c:152-157`, `image_map.c:114-118`). Retired locals that are
    still pinned stay in the list and count as occupied (`accounting.c:141-142,155`).
  - Private anonymous pages are zero-filled ([mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html), `MAP_ANONYMOUS`).
- Zero at attach is therefore the *initial* state only. It says nothing about later
  transitions, so no conclusion about the pending path may be drawn from it.

### Reference transitions

The state is `(count = TLR+0x100, flag = TLR+0x102, P = "this thread is pinned")`.
`P` is kernel state and is not visible in the TLR.

| # | Trigger | Guard | Effect | Source |
|---|---|---|---|---|
| T1 | Guest increments or decrements `count` | none (guest memory) | `count ± 1`. Nesting is counting. | **[ref]** The user owns it. The kernel only reads it (`kern_k_thread.hpp:648`). |
| T2 | Interrupt, or a user-mode exception handled by `HandleException`, taken from EL0 | `count != 0` and no thread pinned on this core | `P := 1`, `flag := 1` | **[ref]** [`kern_k_interrupt_manager.cpp:187-194`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/arch/arm64/kern_k_interrupt_manager.cpp#L187-L194), [`kern_exception_handlers.cpp:609-616`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/arch/arm64/kern_exception_handlers.cpp#L609-L616), [`kern_k_thread.hpp:649-650`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/include/mesosphere/kern_k_thread.hpp#L648-L650) |
| T2' | Same trigger | already pinned, or `count == 0` | no change | Same guard. |
| T3 | Guest decrements `count` to 0 (outermost exit) | — | `count := 0`. **`flag` stays 1 if T2 happened.** `(0, 1, P=1)` is the valid *pending* state until the acknowledgement in T4. | Follows from T1, T2 and T4. |
| T4 | SVC 0x36 | the current thread is the pinned thread on this core | `flag := 0`, `P := 0` | **[ref]** [`kern_svc_synchronization.cpp:119-132`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_synchronization.cpp#L119-L132) |
| T4' | SVC 0x36 | not pinned | **no change**, including a `flag` the guest wrote itself. Repeated calls are idempotent. | Same lines. The clear is conditional on pinning. |

A guest write to `flag` is ordinary memory traffic. The reference clears the flag only
in T4, and only for the pinned thread. **[unknown]**: the exact sequence this SDK uses
around these fields (the increment site, the order of decrement and check, any re-read
after SVC 0x36). Issue #5 must trace it from the admitted input without publishing
bytes.

### Observable invariants (the guest-visible contract)

The kernel pinning *mechanism* is not required. The required observable behavior is:

- **[design] O1.** `count` is guest-owned. The runtime never writes it, and its value
  is unchanged across every gate round trip, host preemption and contained signal.
- **[design] O2.** The runtime changes `flag` only through T2 (0→1, only while
  `count != 0` and its emulated `P == 0`) and T4 (1→0, only while emulated `P == 1`).
  `flag == 1` together with `count == 0` is **allowed**: it is the pending state of
  T3. The runtime never clears a `flag` that it did not set (T4').
- **[design] O3 (scope note, not a guarantee).** T2 guarantees that the pinned thread
  is not migrated or descheduled in favor of process peers on that core. No Android
  design here provides that. Neither option below gives critical-section atomicity or
  forward-progress equivalence. Whether the SDK depends on it is **[unknown]** (A2).

### Candidate realizations (conditional, neither admitted)

- **[design] Candidate A, never pin.** The emulated `P` is always 0, so T2 never fires,
  and SVC 0x36 always takes T4' (a no-op). The observable range is
  `flag ∈ {0, guest-written values}`. This matches the reference for an execution in
  which no interrupt landed inside a counted section. It is a *legal subset* of
  reference traces, not an equivalent emulation. It is refuted for this SDK if any SDK
  section needs T2's exclusion (A2, counterexample C9).
- **[design] Candidate B, synthetic pin.** The runtime holds a per-thread emulated `P`.
  At a chosen host-observable point while `count != 0`, it applies T2. Because
  reference interrupt timing is nondeterministic, this also produces a legal reference
  trace. SVC 0x36 applies T4 or T4'. This exercises the guest's pending path but does
  not provide pinning (O3).
- **Thread exit with `count != 0` or `P == 1`.** The reference unpins inside the
  kernel, which is not guest-observable. **[design, bounded choice]** The runtime
  clears its emulated `P`, records one diagnostic event (thread ID and the values of
  both fields), and completes ExitThread normally. This is a choice for diagnosability,
  not a requirement implied by the reference ABI.

## 3. SVC 0x36 (SynchronizePreemptionState) ABI

- **[ref]** SVC number 0x36 takes no input and returns nothing
  (`void svcSynchronizePreemptionState(void)`, 8.0.0+, documented as "used during
  abort/panic") ([libnx `svc.h:1000-1004`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L1000-L1004)).
  The kernel handler is `void SynchronizePreemptionState64()`
  ([`kern_svc_synchronization.cpp:154-156`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_synchronization.cpp#L154-L156)).
  Permission can be queried through `InfoType_IsSvcPermitted = 26`
  ([libnx `svc.h:212`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L212)).
  Atmosphère's own OS library aborts unless that query reports the SVC as permitted
  ([`os_disable_counter.os.horizon.hpp:24-26`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/os/impl/os_disable_counter.os.horizon.hpp#L24-L26)).
  That library is not the game SDK, so whether the game SDK performs the same check is
  **[unknown]**.
- **Consequence.** The caller reads no Result, so a returned error code cannot be
  observed as a failure. Today's `0x4201` (`svc.c:51`) behaves like a silent T4' no-op
  that also clobbers x0. **[unknown]**: Horizon's register-preservation rule for void
  SVCs (A5).
- **[design] Admission rules.**
  1. Before admission: SVC 0x36 must fail-stop with a diagnostic (SVC number and site).
     It must not return `0x4201`, and it must not return success.
  2. After admission: implement T4/T4' exactly. Leave `count` and all registers other
     than the ABI's clobber set unchanged, and resume at the next instruction.
  3. GetInfo(26, handle 0, sub-ID 0x36) must report the outcome the chosen candidate
     supports: permitted when SVC 0x36 is implemented, not permitted otherwise. Today it
     returns `DM_SVC_INVALID_COMBINATION` (§1). Whether the SDK reacts to that by
     aborting, by skipping the counter, or by ignoring it is **[unknown]**.

## 4. Finite support envelope

Today's code does **not** enforce this envelope. It is the target that admission must
prove.

- **Inside the envelope:**
  - gate round trips at registered sites under the stack bounds of `bridge.S:48-91`;
  - ExitThread from a started guest thread at a registered 0x0a site;
  - host preemption, under O1 and O2 and one admitted candidate from §2;
  - signal handlers listed as reviewed benign (none today).
- **Outside the envelope, must stop:**
  - callbacks into the guest;
  - guest-visible exception delivery;
  - any unlisted signal delivery, including one that would *successfully* resume the
    guest;
  - unregistered gates and entry returns;
  - SVC 0x36 before admission;
  - any unsupported SVC whose ABI has no Result in x0 and that has no admitted
    service-specific ABI. Explicitly supported non-Result SVCs (0x1e, 0x0a, and 0x36
    after admission) follow their verified ABI instead of a fabricated Result;
  - unknown SVCs, unless the Result-only rule in §1 is proved per SVC number.

## 5. Counterexamples (plans only, none implemented)

These tests are owned and game-free. A host run is a synthetic falsification of the
runtime design, not proof of guest compatibility. Android results require a device run
with the review material that `dm/android/BASELINE.md` requires.

| ID | Kind | Refutes | Setup | Refuting observation |
|---|---|---|---|---|
| C1 | synthetic | O1 | The guest sets `count = 3`, then makes N gate round trips (SVC 0x1e, TLR read) under forced host preemption. | `count != 3`, or `flag` changed under Candidate A. |
| C2 | synthetic | "zero at attach ⇒ the pending path never runs" | Under Candidate B, force T2 with `count = 1`, then decrement to 0 (T3). | `flag` reads 0 before SVC 0x36, which means the pending state was lost. Or `flag` stays 1 after SVC 0x36 (T4 not applied). |
| C3 | regression | The zero-fill proof in §2 | Attach, retire and re-attach on the accounting path, then read TLR+0x100..0x103 before guest entry. | Any nonzero byte. |
| C4 | synthetic | §3 rule 1 | `svc #0x36` at a registered site before admission. | The next guest instruction executes. |
| C5 | synthetic | O2 and T4' | The guest writes `flag = 1` with emulated `P == 0`, then calls SVC 0x36 twice. | `flag` changes. The reference keeps it at 1 for an unpinned thread, and this test does not assume an unconditional clear. |
| C6 | synthetic | §4 signal rule | A non-listed, non-`SA_ONSTACK` signal arrives on a guest thread with a 4 KiB stack. | The guest resumes, with or without corruption, instead of stopping. |
| C7 | synthetic | The §2 thread-exit choice | ExitThread with `count = 2` and emulated `P == 1`. | No diagnostic event, or ExitThread fails, or `P` is still set afterwards. |
| C8 | synthetic | The unknown-SVC rule | An unsupported non-Result SVC with no admitted ABI goes through the default path. Control cases are 0x1e and an admitted 0x36. | The unsupported SVC continues with a fabricated x0, or a control case receives a Result instead of its verified ABI behavior (a u64 tick for 0x1e, a preserved x0 for 0x36). |
| C9 | reference equivalence | Candidate A for this SDK (A2) | Two guest threads contend on an SDK section protected by the counter, under host preemption. | Any outcome the SDK's single-core pinned execution forbids, such as a torn update. This requires the traced SDK sequence (#5). |

## 6. Assumption register

| # | Assumption | Status | Closes when |
|---|---|---|---|
| A1 | The mesosphere TLR offsets match the target console kernel. | Reference-backed, not proven for the target. | The SDK's own accesses are traced (#5). Proprietary source is not needed. |
| A2 | No SDK section depends on T2 exclusion. | Unknown. | The per-site argument is made and C9 passes. |
| A3 | The accounting-path TLR is zero at attach. | **Source-traced** (§2). | C3 runs as a regression test. |
| A4 | No host signal handler runs on guest stacks on Android. | Unknown. | C6 passes, plus a device survey of installed handlers. |
| A5 | Horizon register preservation for void SVCs. | Unknown. | The reference SVC entry path is reviewed, and the gate restore set matches it. |
| A6 | Unpinned host scheduling does not break SDK sections on the target device. | Unknown. No scheduling proof exists. | A2 is closed, followed by a device stress run. |

Nothing here unblocks production admission by itself.

## 中文摘要

本文件是 #3 的设计与可证伪验收方案。[repo] 部分描述的是现有代码；本 PR 新增的 [design] 提案尚未实现，所有内容都未在 Android 上验证。没有执行游戏、SDK、原始 rtld 或手机。
§1 把“现有行为”和“拟定准入义务”分列，现有代码并不保证会检测或停止未列事件。
ExitThread 由宿主回收线程，客体不再继续；信号目前没有任何拦截。
§2 用状态机描述 TLR+0x100（disable_count，客体所有）和 +0x102（interrupt_flag）。
宿主只能在计数非零时置位 flag；最外层递减到 0 之后，flag 可以保持为 1，即待处理状态，直到 SVC 0x36 确认。
SVC 0x36 只在当前线程已被钉住时清除 flag（参考 `kern_svc_synchronization.cpp:119-132`），否则不做任何改变。
候选 A 和 B 都只是有条件的设计，都不提供临界区原子性。
在 `calloc` 和记账两条分配路径上，TLR 的初始全零都已由源码证明；但这只说明初始状态，不能据此推断待处理路径不存在。
GetInfo(26) 当前返回 INVALID_COMBINATION。
C1–C9 都只是计划，并标注为合成证伪或参考等价；§6 登记剩余假设。
