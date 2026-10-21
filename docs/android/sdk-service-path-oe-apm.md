# SDK service-path trace: oe → apm / applet (#73 phase 1 continuation)

# SDK 服务路径追踪：oe → apm / applet（#73 阶段 1 续）

Status: **bounded static evidence, no protocol proven.** A selected path from `nninitInitializeSdkModule` through
`nn::oe::Initialize` was traced with read-only GOT/relocation analysis and per-function CFG scans until the first
unsupported indirection. No SDK/game/rtld/phone was executed; no endpoint or command is implemented or guessed.

状态：**有界静态证据，未证明协议。** 从 `nninitInitializeSdkModule` 经 `nn::oe::Initialize` 选了路径，用只读
GOT/重定位与逐函数 CFG 扫描追踪到第一个不支持的间接点即停止。未执行 SDK/游戏/rtld/手机；未实现或猜测端点与命令。

Base: accepted `main` `source snapshot` (includes #76 and the corrected #73 note).

## 1. Method and limits

- Read-only static input: the user-owned dumped SDK module (same image identity as the corrected #73 note). No bytes,
  addresses, symbol dumps, private paths, logs, saves or credentials are published here.
- For each thunk reached by a `BL`: decode any `adrp Rd` / `ldr Rt,[Rn,#imm]` / `add Rd2,Rn2,#imm12` / `br Rt2` chain,
  require `Rn == Rd`, `Rd2 == Rn2 == Rd`, `Rt2 == Rt`, and take the GOT slot from the `ldr` displacement. `add` may
  differ from the `ldr` displacement (lazy-resolver shape); it is recorded separately.
- Relocations: `1026` JUMP_SLOT with a defined symbol record gives a local body; `1026` with symbol index 0 is
  **unresolved under this method** (not assumed ifunc/weak/lazy); `1027` is a relative addend; every other type is
  `UNSUPPORTED_TYPE_n`. No fallback inference.
- Per-function scan: CFG over direct `B`, `B.cond`, `CBZ/CBNZ`, `TBZ/TBNZ` within the symbol's extent, capped at 1200
  instructions. `BLR`/`BR` are classified as unresolved indirect calls/tails with their register and stop that path;
  they are never treated as ordinary instructions or as evidence of absence. `CAP_HIT` is reported explicitly.
- Address dataflow uses correct operand fields (`Rd = w & 31`, `Rn = (w >> 5) & 31` for ADD/LDR), matches ADRP on the
  source register used. Root review narrowed address tracking to contiguous supported ADRP/ADD/LDR chains; every
  other instruction clears tracked state, including calls, branches and unknown writes. This deliberately loses
  candidates rather than retaining stale provenance. Printable targets are static string/REF candidates only.
- Owned synthetic microchecks (`selftest_analyzer.py`, 13 checks, all pass): 10 instruction-level vectors (ADRP x17 /
  LDR x0 / BR x0 decoding, lazy add!=disp shape, LDR base-register mismatch, BR-register mismatch, ADD-register
  mismatch, BLR/BR word recognition, 1026-sym0 / 1027 / 1025 relocation classification) plus 3 trace-level integration
  checks: distinct `ADD x1,x0,#imm` finds the literal, `MOVZ x0,#0` write-kill blocks the stale ADRP literal, and a
  `BL` call-clobber blocks it. These vectors would fail the previous tracer.
- Limits: per-function extent only; visited-instruction cap (1200); indirect calls stop the path. Absence inside scanned
  bodies is not global absence, and an unresolved edge is a stop, not a proof of no edge. Published counts are
  **visited instructions**, never symbol extent size; unvisited instructions remain unknown.
- Root independently reran the 13 checks plus distinct-destination, register-MOV and unsupported-EOR clobber probes.
  The earlier selective write-kill missed unrecognized register writes. All five selected functions were rescanned
  with the conservative tracer; old APM/applet logs from a different scan method are not reused.
- No other SDK family was enumerated.

## 2. Selected path and what is proven

| Step | Body facts (bounded) |
|---|---|
| `nninitInitializeSdkModule` | 9 `BL` + one final `B`; no `RET`; resolves to the nine initializers recorded in the corrected #73 note (oe, aoc, err, fs×3, pctl, os VAM, diag startup) |
| `nn::oe::Initialize` | direct calls `os::SdkMutexType::Lock`, `apm::Initialize`, `oe::InitializeApplet`; body ends in a tail branch (no `RET`); no `SVC`, no indirect call in the scanned body |
| `nn::apm::Initialize` (selected candidate) | 58 instructions visited of a 236-byte extent: mutex lock/unlock, one unresolved symbol-index-0 JUMP_SLOT, one `BLR x8`; static address candidate `apm`. No direct `SVC` in the visited set; no service acquisition or return of the indirect call is proven. The earlier “not a service path” conclusion is withdrawn. |
| `nn::oe::InitializeApplet` | 26 instructions visited of a 464-byte extent: mutex lock/unlock, `am::InitializeApplicationInterfaces`, `am::GetCommonStateGetter`, then one `BLR x8` frontier. The old longer direct-call list came from scanning beyond an unresolved indirect call and is withdrawn. |
| `nn::am::InitializeApplicationInterfaces` | static address candidate **`appletOE`** via `adrp`+`add`; `am::InitializeCommonInterfaces(proxy)`; diag `AbortImpl`; process-handle helpers; one `BL` through a JUMP_SLOT (1026) with symbol index 0 (**unresolved**); **four `BLR x8` indirect calls**; 137 instructions visited of a 776-byte extent, cap not hit |
| `nn::am::InitializeCommonInterfaces(IStackableAppletProxy*)` | 1436-byte symbol extent, **184 instructions visited** (cap not hit): mutex operations and diag `AbortImpl`, plus **seven `BLR x8` indirect calls**; no direct `SVC`, `GetServiceHandle`, `ConnectToNamedPort` or CMIF request/reply call in the visited set |

## 3. First unsupported indirection — stop point

The corrected trace identifies `apm` and `appletOE` as static address candidates. `appletOE` appears in
`am::InitializeApplicationInterfaces` (this is not an executed service acquisition and not proof of one). The actual
service acquisition is not a direct call in the visited bodies. The first frontier is the `BLR x8` virtual dispatch on
the `IStackableAppletProxy` interface (seven sites in `InitializeCommonInterfaces`, four in
`InitializeApplicationInterfaces`) together with the unresolved symbol-index-0 JUMP_SLOT. The receiver/vtable provenance
of `x8` is not established by the visited instructions, and the symbol-0 slot has no name or defined target under this
method.

No port-to-command mapping, command number, request layout, reply layout or reply consumer is proven. The earlier claims
that these bodies contain no virtual calls or service strings, that APM is not a service path, and that the longer
applet call list was reached are retracted. Old logs mixed linear and CFG scans; root rescanning corrects those
claims. APM and InitializeApplet each also stop at their own unresolved `BLR x8` frontier.

## 4. Concrete blocker / next bounded step

**Blocker (recorded once):** resolve the `x8` receiver/vtable provenance for the `BLR x8` sites (which object, which
vtable slot, whether it reaches `sm:` `GetServiceHandle` with `appletOE`) or resolve the symbol-index-0 JUMP_SLOT through
loader/relocation metadata. Without that, no port, session or command can be attached.

Static-first unblock: a bounded register/dataflow trace of `x8` from function entry (including the proxy argument passed
into `InitializeCommonInterfaces`) and a relocation-name mapping for the symbol-0 slot. A controlled run is not requested
and would need separate authorization; no service or call order is guessed in the meantime.

## 5. Explicit unknowns

- Port/service name, session acquisition, domain conversion, command IDs, request layout, reply layout, reply consumer.
- Whether the symbol-index-0 slot is an ifunc/lazy resolver or an undefined/weak import; under this method it remains
  unresolved, and no such identity is assumed.
- The provenance of the `x8` receiver/vtable used by the `BLR x8` sites (proxy object, vtable slot and backing session).
- Whether `apm::Initialize`, `oe::InitializeApplet` or the `am` interface calls execute at startup at all.
- Behaviour of the other eight initializers and of all other SDK families (not enumerated).

## 6. Ownership/failure acceptance plan (contingent, unchanged)

If a service is later proven: host owns registration, sessions/domain handles and rollback; a failed stage retires only
what it created; unknown service stays unsupported (`0x4201` is the current local code, not an ABI substitute); unknown
command/malformed frame/unsupported version fail explicitly; no service is registered merely to make init pass; repeated
acquire/close stays bounded. No implementation is proposed before root review.

## 7. Reproduction boundary

The read-only tracer lives in the private job workspace and reads the dumped module directly; it executes no instruction
from it. Only derived symbol names and counts appear above. Repository anchors for the local transport remain the ones in
the corrected #73 note. No production admission, scanner or quarantine change is included.
