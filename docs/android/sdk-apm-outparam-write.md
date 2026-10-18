# APM shim helper: observed stack out-param store, bounded and conditional (Issue #73)

Status: bounded read-only static evidence of **observed store presence**, not a complete write-set. Refs #73 (parent #7).
This note continues `docs/android/sdk-apm-blr-provenance.md` at its first blocker: whether the selected APM shim helper writes
the caller's stack out-param. One such store is observed and the caller chain around it is decoded; uniqueness,
completeness and the BLR receiver provenance remain **unproved**. The conditional vtable mapping is not promoted to fact.

## 1. Method and owned checks

- Input is the same accepted raw ELF metadata path (raw `r_info` indices retained, 4096-byte bounded mangled-name window).
- A bounded linear decoder inspects one helper body and one caller block. It decodes selected 64-bit `STR`/`STRB`/`STUR`/
  `STP` stores, `LDR`/`ADD`/`MOV`/`MOVZ`/`ADR`/`ADRP`, direct `BL` thunk targets and `BLR`; no instruction is executed.
- The decoder is deliberately **partial**. It flags but does not decode 32-bit `STR`/`STP`, SIMD stores, pre/post-index,
  register-offset and atomic store forms. In this helper it flags five undecoded 32-bit `STR` forms, so a decoded-store
  count is not a store census and absence of a decoded match is not absence of a write.
- Owned vectors (`helper_dump.py --selftest`, 7/7 pass): `str x1,[x0]`, `str x1,[x0,#8]`, `stp x1,x2,[x0]`, `strb x1,[x0,#3]`,
  the base-mismatch negative `str x1,[x1]`, and two unsupported-width checks: legal `str w0,[x19]` is flagged `STR32` and is
  **not** silently decoded.
- Limits: linear, single-helper, no general CFG; no alias analysis beyond the exact decoded base register; nine direct
  calls and four `BLR` calls are not modeled for writes through saved pointers; unvisited code and other services unknown.

## 2. Helper identity

The call in `nn::apm::Initialize` (236-byte extent, 59 instructions) targets the `1026` GOT slot accepted in PR #87:
defined local symbol, **nonzero raw `r_info` index**, **281-byte** mangled name, the lambda of
`nn::sf::SimpleAllInOneHipcClientManager<3>::InitializeShimLibraryHolder<nn::apm::IManager, ...>::operator()(SharedPointer<nn::apm::IManager>*)`.
Its symbol size is **408 bytes / 102 instructions**, ending in one `RET`. The body has nine direct `BL` calls and four
`BLR` calls; among the direct calls are `os::LockMutex`, `os::UnlockMutex`, `lmem::CreateUnitHeap`,
`hipc::InitializeHipcServiceResolution`, **`hipc::ConnectToHipcService`**, `hipc::FinalizeHipcServiceResolution`,
`lmem::DestroyUnitHeap`.

## 3. Observed store and conditional caller chain

The caller forms a temporary holder on its own stack, zero-initializes a second stack slot, and calls the helper with
`x0` = temporary holder and `x1` = address of the zeroed slot. Inside the helper:

- `x1` is moved into `x19` at entry (member function: `x0` is `this`, `x1` is the `SharedPointer<IManager>*` parameter);
  no other `x19` assignment is decoded before the store below;
- one decoded 64-bit store writes the constructed object pointer to `[x19]`.

That is an **observed store**, not an exclusive write-set: five undecoded 32-bit `STR` forms occur in the same body, and
any of them (or an unmodeled call through a saved pointer, or an alias of the slot) could also write the caller slot. The
caller reloads the slot and null-checks it before loading `[vptr]`, `[vptr+8]` and executing `BLR`.

**Conditional chain:** IF the observed 64-bit store executes, IF the helper returns normally, IF no undecoded form or
aliased/interprocedural write intervenes, THEN the caller's `BLR` receiver is that stored object pointer. This does not
prove the slot is only ever zero or that pointer, does not prove every failure path leaves it zero, and does not resolve
the PR #87 out-param blocker. The observed store is evidence of presence, not unique provenance.

## 4. Observed callsite, not a proven connection

Inside the same body there is a resolved static **callsite** to `hipc::ConnectToHipcService`, with a local `svc::Handle`
out-parameter and a name pointer taken from the holder; the body also performs a virtual allocation (`40`/`8` argument
constants) and initializes object vtable fields from a static table. The callsite does **not** prove that the connection is
reached on any path, that it succeeds, that it returns, or that a handle is valid. It is not a `genuine connection` claim.

The connected service **name** is not proved: the name pointer comes from the holder, and the holder's initialization
(including the relation of the earlier `apm` literal to that name field) is not decoded. No port name is asserted.

## 5. Remaining blocker

- Write-set completeness and uniqueness are **unknown**: partial store decoding (five undecoded 32-bit forms), unmodeled
  addressing forms, nine direct calls and four `BLR` calls, and absent alias analysis all prevent an exclusive-write proof.
- The vtable candidate from PR #87 stays **conditional**: object type and address-point convention are not proved; no
  `Release`/`OpenSession` fact.
- No `IManager` `OpenSession` command ID, request layout, reply parser or reply consumer is proved.
- Next frontier would need a scoped alias/interprocedural write proof or the object/vtable construction site; this note
  does not start it.

No bytes, addresses, raw logs, private paths or keys are published. No runtime stubs, scanner/quarantine or global
settings changes. Issues #73 and #7 remain open.
