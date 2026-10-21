# Virtual-core callsite arguments: `nnMain` GetInfo 0 / SetThreadCoreMask

Issue: .
Status: **bounded read-only static evidence, not execution or runtime proof.**

This note records the argument construction at the previously recorded
`nnMain` callsites (the existing anchors remain in
[`virtual-core-contract.md`](virtual-core-contract.md)). It does not add an SVC
or a runtime success stub. The private SDK/main inputs were decoded as data
with their pinned hashes verified locally; original instructions were never
executed. Only aggregate facts are published.

## 1. Instruction order

The recorded calls occur in one straight-line `nnMain` prefix: there is no
conditional branch between them. The later `SetThreadCoreMask` call is
therefore conditional on the prior calls returning normally, including
`GetCurrentThread` and `GetThreadAvailableCoreMask`. This is instruction order,
not proof that any call executes or returns in a run.

1. Main calls `nn::os::GetCurrentThread` through an imported relocation and
   keeps the returned `ThreadType*` as the thread argument.
2. Main calls `nn::os::GetThreadAvailableCoreMask` at the recorded callsite.
   The returned `x0` is moved directly to the `SetThreadCoreMask` mask
   argument. It is not a constant, not truncated to 32 bits, and not derived
   from the legacy host value `7`.
3. Main sets the core argument to `-1` (`IdealCoreDontCare`), moves the
   `ThreadType*` into the handle argument, and calls
   `nn::os::SetThreadCoreMask` at the recorded callsite.
4. Main does not inspect a result or select a fallback path. It continues to
   `nn::time::Initialize` and `nn::oe::Initialize`.

## 2. SDK wrapper behavior

The SDK `ThreadManagerImplByHorizon::SetThreadCoreMask` wrapper receives the
`ThreadType*`, core and full 64-bit mask. It loads a 32-bit handle from the
current `ThreadType` object, forwards the signed core in `w1` and the full
mask in `x2` to raw SVC `0x0f`, then checks `w0`.

The SDK `ThreadManagerImplByHorizon::GetThreadAvailableCoreMask` wrapper calls
GetInfo ID `0` with the current-process handle and sub-ID `0`. On success it
returns the stored 64-bit value. It does not apply a local mask or core-count
policy.

Both SDK wrappers branch to an abort path when the SVC result is non-zero.
`SetThreadCoreMask` returns no value to main; `GetThreadAvailableCoreMask`
returns only the successful 64-bit value. There is no normal failure fallback
at these callsites.

## 3. Handle origin

`GetCurrentThread` returns the TLS-resident current `ThreadType*`; it is not a
raw handle. The thread-manager constructor initializes the handle field from
the loaded OS boot-parameter value, and the SetThreadCoreMask wrapper loads
that field as the 32-bit SVC handle. The field is therefore a loaded
boot-parameter handle, not a compiled pseudo constant. Its exact numeric value
is runtime state and remains **U**.

## 4. Precise consequences and blocker

- The recorded `nnMain` core argument is `-1` (`IdealCoreDontCare`), not an
  explicit non-negative core. The bounded owned SVC `0x0f` subset currently
  rejects `-1` with `0x4201`; the SDK wrapper's error path aborts on that
  result. The owned subset therefore does **not** cover this callsite and must
  not be described as SDK-call support.
- The actual mask is the runtime result of GetInfo ID `0`. No static value,
  target core count, or physical-core set is proven, and the legacy value `7`
  must not be substituted.
- The actual thread handle is a loaded boot-parameter value; its numeric value
  is not statically known.
- The pinned reference establishes the `-1` validation rule, but the target
  kernel's post-validation selection/update behavior is not proven. This note
  does not claim that behavior.
- `nnMain` execution, the numeric mask, the numeric handle, and whether the
  intervening calls return remain unproven.

Implementing `-1`/`-2`/`-3` would require separate public/reference proof and
an owner-reviewed contract; it is not justified by this callsite evidence
alone. No SVC `0x0e` readback, guessed mask, or success-stub behavior is added.
