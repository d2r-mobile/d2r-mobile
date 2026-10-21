# apm BLR x8 provenance slice (#73)

# apm BLR x8 来源切片（#73）

Status: **bounded static evidence, no service acquisition proven.** This note follows only the earliest (and only)
`BLR x8` in `nn::apm::Initialize` to its receiver/vtable slot and stops at the first unproven boundary. No SDK/game/rtld
instruction was executed; no endpoint or command is implemented or guessed.

状态：**有界静态证据，未证明服务获取。** 本说明只追踪 `nn::apm::Initialize` 中最早（也是唯一）的 `BLR x8` 到其
接收者/vtable 槽，并在第一个未证明边界停止。未执行 SDK/游戏/rtld 指令；未实现或猜测端点与命令。

Base: accepted `main` `source snapshot`; independent worktree `issue-73-apm-provenance`. Private root tracer/logs are immutable
and were copied for this slice; only derived symbol names/counts appear here.

## 1. Method and owned checks

- Conservative contiguous-block slice around the first `BLR xN`: ADRP/ADD/LDR/MOV register tracking with correct
  `Rd = w & 31` / `Rn = (w >> 5) & 31`; any unsupported instruction, call, or branch boundary invalidates all tracked
  address state. Stack-base loads are recorded as `stack` descriptors, never resolved into object identity.
- Relocations: `1026` with a defined symbol record is a local target; a genuine raw `r_info` symbol index 0 is
  unresolved; a nonzero index whose name is missing/truncated is `name unavailable` (never index 0); an undefined symbol
  is `IMPORT_UNDEFINED`; `1027` is a relative addend; other types are unsupported. The raw index is retained in the
  private method.
- Long mangled names use a bounded 4096-byte window; the earlier 128-byte window truncated a 281-byte name and
  misclassified that slot as nameless/index-0. That attribution is retracted for the apm helper slot in this note and in
  `sdk-service-path-oe-apm.md`; other unresolved slots keep their unknown status.
- Owned microchecks (`provenance_slice.py --selftest`, 11 checks, all pass): global-slot load, parameter move,
  unsupported `EOR` write, call clobber, branch boundary, actual index-0, nonzero truncated name (not index-0), 281-byte
  long name, nonzero undefined import, 1027 relative, 1025 unsupported.
- Limits: one contiguous block; no interprocedural write proof; unvisited code remains unknown; ABI-derived vtable
  ordering is labelled as such, not as observed execution.
- **Automated vs manual:** the automated slice for this block yields `x8 = load_unknown` and `x0 = unknown` (conservative
  invalidation at the null check/unsupported instruction). The stack-address call, stack load and `[[x0]+8]` chain below
  are **manual adjacent observations** of the same block, not automated descriptor proof.

## 2. Observed chain of the BLR

In the 236-byte `nn::apm::Initialize` extent (58 instructions visited by the conservative tracer), the only `BLR x8`
sits in a later contiguous block:

1. a stack address is formed and passed as `x0` to a call through a GOT thunk;
2. after a null check, `x0` is loaded from a stack slot;
3. `x8 = [x0]` (object vptr), then `x8 = [x8 + 8]` (vtable slot);
4. `BLR x8`.

With the corrected string bound, the GOT slot of the call in step 1 is a `1026` entry with a **nonzero raw symbol index and
a 281-byte name** that resolves to a **defined local** symbol: the lambda of
`nn::sf::SimpleAllInOneHipcClientManager<3>::InitializeShimLibraryHolder<nn::apm::IManager, ...>::operator()(SharedPointer<nn::apm::IManager>)`.
The earlier 128-byte window truncated this name and produced the now-retracted "nameless/index-0" label. This call is the
only candidate producer of the object pointer in the block (manual adjacent observation; the automated descriptor stays
`load_unknown`), and it ties the receiver to the `nn::apm::IManager` shim-holder initialization path. It is **not**
proven that this call writes the stack out-param; that write is an ABI/dataflow assumption, so the receiver identity is a
bounded candidate, not a fact.

The static vtable `_ZTV...ObjectImplFactory...CmifProxy<nn::apm::IManager>...ObjectE` (104 bytes) contains, in order:
offset-to-top `0`, typeinfo, `AddReference`, `Release`, `GetProxyInfo`, `GetInterfaceTypeInfo`, `_nn_sf_sync_OpenSession`,
`_nn_sf_sync_GetPerformanceMode`. **Conditional candidate:** if the receiver really is this `CmifProxy` object *and* the
Itanium C++ ABI address-point convention applies
([primary spec §2.5.2](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable)), then `[vptr + 8]` is the second virtual
entry, i.e. a candidate `Release`. The actual target remains unknown because both preconditions are unproven; this note
does **not** conclude that the call is or is not `OpenSession`/a service acquisition.

## 3. The `apm` literal

The function also holds a printable literal `apm`. It is loaded into `x0` before a call whose thunk resolves (by symbol
record) to `nn::os::UnlockMutex`, so its flow into a known session-acquire helper is **not proven** in this slice. It is
kept only as a static address candidate; no port name and no service endpoint is inferred from it.

## 4. Stop point / blocker

First unproven boundary: the stack out-param write that would make the shim object the BLR receiver, and the exact vptr
convention of the apm `IManager` object. Without both, neither `OpenSession` nor any command/reply is attached to this
path, and the conditional `Release` mapping is not promoted to a fact. `_nn_sf_sync_OpenSession` exists as a static
vtable entry and proxy method symbol; whether this path reaches it remains unknown. No command ID, request layout or
reply consumer is proven.

Next static evidence that would help: a bounded write-set proof for the step-1 call's out-param (or a trace of the
`InitializeShimLibraryHolder` body showing it stores the holder), or an object-layout/vptr convention for
`nn::sf::detail::ObjectImplFactory...::Object`. A live probe is not requested.

## 5. Explicit unknowns

- Whether the step-1 call writes the stack out-param and thus creates the BLR receiver.
- Exact vptr convention and whether `[vptr+8]` is `Release` on this platform.
- Whether the `apm` literal is ever used as a service name; no acquire path is proven.
- `_nn_sf_sync_OpenSession` command ID, request/reply layout, reply consumer and reachability.
- Other apm functions, other services, startup ordering and execution.

## 6. Boundaries

No game/SDK/rtld/phone execution, no live probes. No SDK bytes, addresses, raw logs, private paths, keys or saves are
published; only derived symbol names and counts. No runtime endpoint, scanner, quarantine or admission change. Parent #7
and #73 remain open; this is evidence only.
