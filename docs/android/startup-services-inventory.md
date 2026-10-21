# Android SDK startup: required-service and import inventory

Issue:  (A5). Status:
**bounded static inventory, not a startup implementation or acceptance
claim.** This document orders the presently known initialization path,
separates the implemented runtime subset from static references, and records
the remaining protocol frontiers. It does not execute a game, SDK, original
rtld, initializer or phone.

Repository anchors use the accepted main commit
`source snapshot` and are written as `commit:path:line`. Private metadata and
historical diagnostic evidence are summarized only as aggregate observations;
raw game/SDK bytes, logs, hashes of private payloads, saves and credentials are
not published here.

Evidence levels used below:

- **A — repository**: code, owned tests or published documentation at the
  stated commit.
- **B — static metadata**: read-only symbol/relocation/instruction metadata
  from the private SDK image. A direct reference is an edge, not proof that the
  edge executes, returns or covers its descendants.
- **C — historical diagnostic**: one previously consumed, restricted macOS
  stage. It is not Android execution and is not a current full-startup result.
- **U — unknown**: no whole-path closure or protocol proof.

## 1. Ordered known initialization path

The public lifecycle inventory records the stage order. The static startup
root additionally resolves the same broad order; it does not close all
initializer slots.

| # | Stage | Public anchor | Reached / evidence | Implemented in this runtime | Remaining boundary |
|---|---|---|---|---|---|
| 0 | Host reservation and module mapping | `source snapshot:dm/android/LIFECYCLE.md:10`, `source snapshot:dm/android/LIFECYCLE.md:21` | Owned tests; not an SDK stage | Host creation/destruction; mapping and rollback | Not proof of SDK entry, initializer execution or teardown |
| 1 | `_init_libc0` | `source snapshot:dm/android/LIFECYCLE.md:11` | Historical prefix; no observed destructor | No SDK wrapper implementation | Entry ABI and full libc phase are not closed |
| 2 | `nnosInitialize` | `source snapshot:dm/android/LIFECYCLE.md:12` | Historical prefix returned in one restricted stage (`source snapshot:dm/android/README.md:403`) | Memory/query, thread, synchronization, priority, cancellation and IPC backends | Stage itself is not a production entry; GetInfo and core contracts remain partial |
| 3 | Loader-ready adapter | `source snapshot:dm/android/LIFECYCLE.md:13` | Historical prefix | Owned loader/module adapter | Not the original rtld; no paired adapter teardown |
| 4 | `_init_libc1` | `source snapshot:dm/android/LIFECYCLE.md:14` | Historical prefix | No SDK wrapper implementation | Static/historical only |
| 5 | `nninitInitializeSdkModule` | `source snapshot:dm/android/LIFECYCLE.md:15` | Historical entry stopped at `ConnectToNamedPort` SVC `0x1f`, result `0x4201`; the transport is now locally implemented (`source snapshot:dm/android/README.md:409`, `:411`) | Named-port/SendSyncRequest transport, local `sm:` and bounded `lm` | The stage has not been rerun through the new transport; later service/session initialization is not closed |
| 6 | SDK init-array | `source snapshot:dm/android/LIFECYCLE.md:16`; `source snapshot:dm/android/README.md:115` | Static metadata only; initializers are recorded but never called | Linker inspection/recording | 115 SDK slots are inventory, not a proven reachable set; executing constructors is out of scope |
| 7 | `nninitStartup` | `source snapshot:dm/android/LIFECYCLE.md:17` | Static only; main callback subtree is unaudited | No game startup execution | The public row combines `nninitStartup` and `nnMain`; it is not execution-order evidence |
| 8 | `_init_libc2` | `source snapshot:dm/android/LIFECYCLE.md:18` | Static only | No SDK wrapper implementation | Its position comes from the private direct-reference order, not an executed trace |
| 9 | `nnMain` | `source snapshot:dm/android/LIFECYCLE.md:17` | Static only | No game main execution | Main has 852 init slots and 656 undefined symbols; neither is a service count |
| 10 | `nninitFinalizeSdkModule` | `source snapshot:dm/android/LIFECYCLE.md:19` | Static symbol is a bare `ret`; no body observed | No owned finalizer path | Reverse rollback and cleanup remain unproven |
| 11 | `nnosQuickExit` | `source snapshot:dm/android/LIFECYCLE.md:20` | Static exit edge only | No real SDK exit | Normal exit and process cleanup remain unproven |

The private metadata inventory resolves SDK `nn::init::Start` as a direct
reference chain through libc phase 0, `nnosInitialize`, libc phase 1,
`nninitInitializeSdkModule`, main `nninitStartup`, libc phase 2, `nnMain`,
SDK finalize and quick-exit. It also finds indirect callback sites and does not
close their targets. The public lifecycle row for `nninitStartup`/`nnMain` is
combined; the split rows above follow the private direct-reference order, not
a proven execution order. This is evidence for order, not for successful
execution of every stage.

## 2. Implemented subset at `source snapshot`

These are bounded, owned implementations. They do not constitute SDK startup.

### 2.1 Transport, SM and LM

| Facility | Implemented behavior | Explicit failure / ownership boundary | Repository anchor |
|---|---|---|---|
| `ConnectToNamedPort` SVC `0x1f` | Resolves only locally registered names (`sm:`, `lm` and host-registered ports), creates a real session handle | Invalid address `0xcc01`, invalid size `0xca01`, invalid handle `0xe401`, out of memory `0xd001`, out of resource `0xce01`; unknown name does not create a session | `source snapshot:dm/android/runtime/ipc.c:160`, `source snapshot:dm/android/runtime/ipc.c:387` |
| `SendSyncRequest` SVC `0x21` | Snapshot of the caller TLS buffer; descriptor-free type4/CMIF v0, type6/CMIF v1 with send-PID, and the two proven type7 controls | Unsupported forms `0x4201`; bad state `0xfa01`; invalid address `0xcc01`; invalid handle `0xe401`; out of resource `0xce01`; no fabricated reply or descriptor following | `source snapshot:dm/android/runtime/ipc.c:205`, `source snapshot:dm/android/runtime/ipc.c:417` |
| `sm:` RegisterClient | Real per-host client registration for an existing session | Missing send-PID/undersized request or duplicate registration is rejected; no client is created | `source snapshot:dm/android/runtime/sm.c:30`, `:36` |
| `sm:` GetServiceHandle | Returns a real session handle only for an already registered local port | Unknown/unregistered service is unsupported; handle publication is transactional and rolls back | `source snapshot:dm/android/runtime/sm.c:50`, `:67` |
| `lm` OpenLogger / domain close | Real per-session domain root, up to eight child ids, 52-byte OpenLogger reply, exact child retirement | Allocation failure rolls back unpublished children; duplicate/wrong root is unsupported | `source snapshot:dm/android/runtime/lm.c:20`, `source snapshot:dm/android/runtime/ipc.c:441` |
| `lm` Log | Bounded wire decode, owned read pin, copy into four 1 KiB slots, FIFO dequeue | Malformed/unsupported range is rejected (`0x4201`/`0xcc01`/out-of-resource); queue is not a sink | `source snapshot:dm/android/runtime/lm.c:52`, `source snapshot:dm/android/runtime/lm_wire.c:45` |

The owned fixture covers the real SM → `lm` path, including RegisterClient,
GetServiceHandle, domain conversion, OpenLogger and Log
(`source snapshot:dm/android/tests/ipc.c:497`, `:542`). This is transport evidence, not
proof that an SDK service session exists or that game initialization advances.

### 2.2 Memory, thread and synchronization SVCs

| SVC / query | Current implementation | Limits | Repository anchor |
|---|---|---|---|
| SVC `0x01` SetHeapSize | Bounded heap sizing and zero-on-recommit behavior | Not a full physical-memory or allocator contract | `source snapshot:dm/android/runtime/svc.c:18` |
| SVC `0x04`/`0x05` memory alias map/unmap | Alias creation/removal over owned heap and stack ranges | Alias mapping only; no general `MapSharedMemory`/`MapPhysicalMemory` claim | `source snapshot:dm/android/runtime/svc.c:19`, `source snapshot:dm/android/runtime/memory.c:49` |
| SVC `0x06` QueryMemory | Exact owned free/heap/stack/module ranges and output bytes | Requires an owned or explicitly registered range; not every region-policy rule | `source snapshot:dm/android/runtime/memory.c:127` |
| SVC `0x29` GetInfo | IDs 4/5/11/12/13/14/15; conditional 2/3 alias, 16 system-resource zero, 21/22 accounting | ID 0 (virtual core mask) is absent; unsupported IDs return `0x4201`; accounting is synthetic, not host RSS | `source snapshot:dm/android/runtime/memory.c:130`, `:141` |
| Thread SVCs `0x08/0x09/0x0a/0x0c/0x11/0x12/0x16/0x17/0x18/0x19/0x45` | Create/start/exit, priority getter, event and thread waits, cancellation | Virtual priority only; no physical affinity or priority inheritance; `0x0f` SetThreadCoreMask is not implemented | `source snapshot:dm/android/runtime/svc.c:43`, `source snapshot:dm/android/runtime/sync.c:306` |
| SVC `0x34`/`0x35` address arbitration | 32-bit wait/signal predicates and timeouts | 64-bit equality, priority ordering and cancellation are not implemented | `source snapshot:dm/android/runtime/svc.c:47` |

`docs/android/runtime-contract.md:47-49` records that SVC `0x36` currently
returns `0x4201` and that unsupported non-Result SVCs must not fabricate a
Result. That admission decision is Issue #3/#4/#5 work, not an A5 service
success.

### 2.3 Imports

The six shipped RomFS host veneers are undefined in main and defined in the
SDK; therefore they intercept main's undefined relocations but do not replace
the SDK's own internal filesystem implementation. The six names are the
read-only cache/mount/open/read/size/close subset, with raw extracted
resources only. Directory enumeration, writable storage, transactional saves
and writes are outside this subset. Main also directly references `FlushFile`,
which is not one of the six.

The 13 SDK and 656 main undefined-symbol counts are linker/import inventory,
not a missing-service count: ordinary module linking resolves many symbols.
No unresolved import is converted to success.

## 3. Proven references versus unproven reachability

The following are concrete static edges from the private metadata inventory.
They justify bounded investigation, not a claim that the service is needed
before first entry.

| Reference | What is proven | What is not proven |
|---|---|---|
| `nnosInitialize` → resource manager → VAMM/GetInfo callsites (IDs 2/3, 16, 21/22) | Direct wrapper references and constant argument suffixes in the selected straight-line paths | That every call executes, returns, or that all descendants are covered |
| Thread manager constructor → SVC `0x0c` and SVC `0x25` callsites | Direct edges to verified priority/thread-id wrappers | That priority is required for every startup path |
| Main `nnMain` → GetInfo ID 0 and SetThreadCoreMask SVC `0x0f` | Direct reference/callsites | Virtual-core semantics and whether a particular game path reaches them |
| `nninitInitializeSdkModule` → oe/aoc/err/fs/pctl/diag initialization references | Named direct references to service initialization | That any session is opened, which commands are used, or that all are required |
| `lm` Log/OpenLogger references | Bounded local protocol shapes proven from SDK dataflow for the fixture | SetDestination, sink behavior and arbitrary log command coverage |

The important distinction is:

- **statically imported**: a symbol/relocation exists in a module;
- **statically referenced**: a resolved direct edge reaches a wrapper or
  initializer;
- **reachable**: an execution path is shown to call it in the required order;
- **implemented**: the host has an owned, tested behavior for the observed
  protocol.

Only the last two together would justify treating a service as available. This
inventory establishes some fourth-column behavior for the local transport and
memory/thread subset, but no whole first-to-last path.

## 4. Concrete gap register

The table links only existing, nonduplicate work. A blank owner means no
existing implementation issue was found; this docs slice does not invent a
protocol or create a duplicate. Such a gap needs a separate, owner-reviewed
subtask before implementation.

| ID | Proven gap | Existing owner | Required ownership / failure boundary | Negative acceptance |
|---|---|---|---|---|
| G1 | GetInfo ID 0 and SetThreadCoreMask SVC `0x0f` are referenced by main but absent from the current dispatcher | #72 (bounded child); #8 coordination; #10 acceptance | Define a guest virtual core-mask model, validate core inputs and keep it distinct from host affinity; unsupported paths stay explicit | Invalid masks or unsupported calls must fail, never report physical affinity success; owned test must distinguish virtual state from host scheduling |
| G2 | `oe`, `aoc`, `err`, `fs`, `pctl` and diagnostic initialization are directly referenced by SDK module initialization, but no remote service sessions exist | #73 (bounded child); #8 lifecycle; #10 acceptance | Host owns every session/domain handle; unknown service names remain unsupported; rollback must retire only handles created by the failed stage | No service may be registered merely to return success; unknown service must remain `0x4201`, and a failed stage must leave no live session |
| G3 | SVC `0x36` SynchronizePreemptionState and the remaining TLR/preemption contract are not implemented | #3, #4, #5 | Per-thread TLR state; non-Result ABI must not fabricate a Result; admission remains strict | Unsupported pre-admission SVC `0x36` must fail-stop; no unconditional flag clear or fabricated x0; see `docs/android/runtime-contract.md:179-183` |
| G4 | `lm` SetDestination is unsupported; the four-packet queue has no general consumer/sink | #16 | Consumer owns dequeue/backpressure and stop; bounded queue must not silently discard while returning success | Queue-full, closed session and unsupported SetDestination must return an explicit failure; no success without retained/consumed data |
| G5 | The six RomFS overrides are read-only and do not cover SDK-local filesystem calls or main `FlushFile` | #20 for persistent filesystem/save semantics | Host owns file handles and roots; writable/unsupported operations fail explicitly | No fake writable/fs success; undefined or uncovered filesystem operations must not be silently treated as mounted |
| G6 | Full SDK initialization, later module stages, init/fini execution and normal exit are not demonstrated | #10 (final acceptance), with #8 for lifecycle | Real-input approval, ordered stages, isolation/watchdog and cleanup evidence | A single returned diagnostic stage, compile, or synthetic test cannot be accepted as full startup; all unsupported stops remain visible |

`nninitFinalizeSdkModule` has no observed body and `nnosQuickExit` is static
only (`source snapshot:dm/android/LIFECYCLE.md:19-20`); reverse rollback is therefore
also part of G2/G6 rather than a claimed teardown success.

## 5. Unknowns and precise unblock evidence

These remain **unknown**, not implemented or “not needed”:

1. **Initializer closure.** 115 SDK and 852 main INIT_ARRAY slots are
   metadata; the number that actually executes is unknown. Unblock: a bounded
   trace of one approved root's initializer entries and observed returns,
   without publishing code bytes.
2. **Indirect startup entries.** SDK `nn::init::Start` contains indirect
   callback sites; main's `nninitStartup` callback subtree is unaudited.
   Unblock: resolve one target provenance and execution boundary with a
   reviewed trace; do not relax the strict scanner.
3. **Remote SM/service sessions.** `sm:` serves only locally registered ports.
   Unblock: one bounded `GetServiceHandle` trace from a real initialization
   root, its exact command/response shape, and a negative test for an
   unregistered name.
4. **VAMM/process accounting semantics.** GetInfo 2/3, 16 and 21/22 have
   conditional implementations, but the SDK's expected region/accounting
   model is not fully proven. Unblock: the exact branch and output consumption
   for each remaining startup call, not a fabricated constant.
5. **Virtual cores.** GetInfo 0 and SVC `0x0f` are absent. Unblock: prove the
   guest-visible mask semantics and the expected failure for unsupported
   affinity, then implement an owned virtual model.
6. **Filesystem import reachability.** Six overrides are known; SDK-local
   filesystem calls and `FlushFile` are not covered. Unblock: an import/
   callsite inventory for one startup root and an explicit failure contract
   for each unsupported operation.
7. **lm sink.** OpenLogger is implemented; SetDestination/sink and long-running
   backpressure are not. Unblock: prove the command shape and consumer
   ownership before adding a sink.

## 6. What this slice does and does not establish

- It satisfies only the inventory portion of Issue #7: ordered known path,
  evidence anchors, implemented subset, static imports and unknown boundaries.
- The concrete G1/G2 gaps are now tracked by bounded evidence-first children
  #72 and #73; their protocol evidence, implementation and review remain open.
- It does not close #7, #8, #10, #3, #4, #5, #16, #20, #72 or #73.
- It does not establish that symbol presence means game need, that a static
  import is reachable, that a real service session exists, or that a full SDK
  initialization/exit works.
- No game/SDK/rtld/phone execution, artifact publication, raw log publication
  or success stub was used to write this inventory.
