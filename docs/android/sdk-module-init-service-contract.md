# SDK module-init service contract — phase 1 evidence note (#73)

# SDK 模块初始化服务契约：阶段 1 证据说明（#73）

Status: **bounded evidence only.** One root is selected and traced until the first unproven indirection. No service is
implemented, no port/command/reply is guessed, and no startup reachability is claimed. No game, SDK, original rtld or
phone was executed. Parent #7 and this issue stay open.

状态：**仅限有界证据。** 选定一个根，追踪到第一个未证明的间接点即停止；不实现服务、不猜端口/命令/回复、不声称启动
可达。未执行游戏、SDK、原始 rtld 或手机。父 #7 与本 Issue 保持打开。

Base: accepted `main` `source snapshot` (includes #74 and #71). Repository anchors are `path:line` at that commit.

## 证据标签 | Evidence tags

| Tag | Meaning |
|---|---|
| **A** | Repository code/docs/tests at the stated commit. |
| **B-old** | Existing private report only: body/thunk shape. It does not resolve imports. A direct edge is not execution or reachability proof. |
| **B-new** | New bounded read-only static derivation in this revision: GOT/relocation and symbol resolution from the same private input. Derived metadata only; no bytes, addresses or symbol dumps published. |
| **U** | Unknown; no evidence yet. |

## 1. Selected root

**`nninitInitializeSdkModule`** — the SDK module-initialization stage already listed in the public lifecycle inventory
(`dm/android/LIFECYCLE.md:15`, `docs/android/startup-services-inventory.md:40`). This is the one root for phase 1.

- [B-old] An existing private bounded report (unpublished; SDK image identity recorded inside it) decodes the exact
  function body: 60 bytes, 15 instructions, **9 direct `BL` edges**, **one final `B` tail branch**, **no `RET`**, no
  `SVC`, no direct string references, and two `mov w0, #1` calls.
- [B-new] All nine `BL` targets are 16-byte `adrp`/`ldr`/`add`/`br x17` import thunks; their GOT slots resolve through
  `R_AARCH64_JUMP_SLOT` (1026) relocations to these defined initializer symbols (family names, no addresses):
  `nndiagStartup`, `nn::os::InitializeVirtualAddressMemory`, `nn::oe::Initialize`, `nn::aoc::Initialize`,
  `nn::err::InitializeContextWriterApi`, `nn::fs::InitializeForApplication`,
  `nn::fs::SetLocalApplicationAccessLog(bool)` (constant `1`), `nn::pctl::Initialize`, and
  `nn::fs::SetResultHandledByApplication(bool)` (constant `1`). Nine edges map to seven initializer calls plus two
  `fs` setters.
- [B-new] The final `B` tail target is itself an import thunk whose GOT slot resolves to
  `nn::diag::InitializeApplicationAbortObserver()`. The root therefore does **not** return directly: it tail-branches to
  that initializer. Whether the tail callee returns, and with what state, is unproven here.
- [A] The public inventory records that this stage has named direct references to the `oe`, `aoc`, `err`, `fs`, `pctl` and
  diagnostic initialization families (`startup-services-inventory.md:118`). The [B-new] resolution above is the per-edge
  mapping; it confirms the family set and shows the two `fs` boolean setters. This is derived metadata, not an SDK
  execution or reachability claim.
- [A] Current runtime transport: `ConnectToNamedPort` resolves only locally registered names (`dm/android/runtime/ipc.c:572`
  dispatch, `:163` comment), `sm: GetServiceHandle` returns only an already registered local port
  (`dm/android/runtime/sm.c:30` handler), and `lm` OpenLogger/Log are bounded local implementations
  (`dm/android/runtime/lm.c:20`, `:52`). None of this is an SDK service session.

## 2. What is actually proven

| Fact | Evidence | Strength |
|---|---|---|
| Root body: 60 bytes, 15 instructions, 9 `BL` + one final `B` tail, **no `RET`**, no `SVC`, no direct string reference | [B-old] body decode | static only |
| Nine `BL` calls leave through GOT import thunks | [B-old] targets + thunk shape | static edges |
| GOT slots resolve to `nndiagStartup`, `nn::os::InitializeVirtualAddressMemory`, `nn::oe::Initialize`, `nn::aoc::Initialize`, `nn::err::InitializeContextWriterApi`, `nn::fs::InitializeForApplication`, `nn::fs::SetLocalApplicationAccessLog(bool)`, `nn::pctl::Initialize`, `nn::fs::SetResultHandledByApplication(bool)` | [B-new] RELA/GOT + dynsym | derived metadata |
| Two `fs` boolean setters receive constant `1` | [B-old] `mov w0,#1` placement + [B-new] names | static only |
| The final `B` is a tail call whose thunk resolves to `nn::diag::InitializeApplicationAbortObserver()`; the root does not return directly | [B-new] GOT resolution | static only; callee return unproven |
| `nn::oe::Initialize` body calls `nn::os::SdkMutexType::Lock`, `nn::apm::Initialize`, `nn::oe::InitializeApplet`, `nn::am::GetApplicationFunctions`, all defined in this module; the bounded linear scan recorded no `SVC` or service-name string (not an absence proof) | [B-new] one-level follow | static only |
| Local `sm:`/`lm` behavior is implemented and tested | [A] `ipc.c:572`, `sm.c:30`, `lm.c:20,52`, `tests/ipc.c:235` | owned host tests |

Not proven: whether the tail callee returns; whether any of the nine initializers executes at startup; which of
`apm::Initialize` / `oe::InitializeApplet` / `am::GetApplicationFunctions` opens a session; every port name, command ID,
request/reply layout and reply consumption.

## 3. First unproven protocol — stop point

Following the one selected initializer, `nn::oe::Initialize`, reaches four same-module calls. The first unproven protocol
is therefore one level below it: which of those four performs a session acquisition (named-port/`sm:`), and with which
port name and command/reply shape. The `oe::Initialize` body itself contains no `SVC` and no service-name string, so no
port or command can be attached at this level.

Phase 1 stops here. No command ID, request layout or success reply is asserted; the other eight resolved initializers are
recorded as edges only, not followed.

## 4. Next bounded step (available statically)

The next step does **not** require a phone or SDK execution yet: the four `oe` callees are defined in the same dumped
module, so one of them (`nn::apm::Initialize` is the natural candidate) can be followed one bounded level with the same
read-only GOT/relocation method until the first `sm:`/named-port session acquisition or a concrete missing-input stop.

Required output of that step: the proven callee body facts (direct `BL`/`SVC`/string edges), one port name if a string or
`GetServiceHandle` path is proven, and the exact first point where the command/reply protocol becomes unproven. If a
callee body turns out to be an import without a body in the available module images, record that exact missing input
once and stop. No command may be guessed in either case.

Phase 2 implementation remains out of scope until root review.

## 5. Ownership and failure acceptance plan (contingent, not implemented)

The following is a plan for a future proven service. It is **not** an implementation and does not assert current behavior.

- Host owns service registration, every session/domain handle and all rollback. A failed stage retires only the handles,
  sessions and domain children it created; pre-existing registrations are untouched. No duplicate root/port ownership.
- Unknown/unregistered service names stay unsupported. `0x4201` is the current local unsupported code; it is not a
  substitute for a proven Horizon ABI.
- Unknown command, malformed frame and unsupported version must fail explicitly, never return a success placeholder.
- Negative acceptance: no service may be registered merely to make initialization pass; a failed stage leaves no live
  handle/session; repeated acquisition/close remains bounded.
- Positive acceptance (future): one real startup path reaches the local endpoint and receives the proven reply — only after
  the next bounded step above is completed and reviewed.

## 6. Coordination and explicit unknowns

- #72 (virtual core mask) and #32 (touch parity) are separate owners; this note does not touch their files or claims.
- #8 lifecycle/rollback, #10 real-input acceptance, #16 log sink, #20 filesystem are linked only if a traced root proves
  those commands; none is claimed here.
- Unknowns: whether/how the tail callee returns; which `oe` callee performs session acquisition; port/service names;
  command/reply layouts; reply consumption; error mapping; initializer ordering and execution; whether any of the nine
  edges is required for startup.
- This note does not repeat the whole SDK inventory and does not claim that the family set is complete.

## 7. Reproduction boundary

The private input and existing report were read only. The [B-new] derivation used a bounded read-only script in the
private job workspace: NSO segment decode, dynamic `RELA`/`JMPREL` parsing, `adrp`/`ldr` GOT-address computation from the
nine thunk bodies and the tail thunk, and dynsym name lookup; no SDK instruction was executed. The follow-up scanner is linear, stops at its first `RET`, and is capped at 120 instructions; it is not a control-flow or complete string/dataflow proof. A root review corrected the symbol enumeration bound to the `DT_HASH` table’s `nchain` (the tag value itself is an address, not a count); rerunning the bounded analysis retained all nine named edges and the tail target. Derived names/counts only; no
SDK bytes, addresses, symbol dumps, private paths, logs, saves or credentials are included here.

The repository-side facts can be checked at the cited `path:line`. No new test was added because this slice is docs-only
and no behavior changed. Four green CI jobs do not prove private metadata or service semantics.
