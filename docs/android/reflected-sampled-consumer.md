# Reflected sampled-2D compute consumer (Issue #105)

Bounded offline subset; not a full shader ABI and not device acceptance. Parents #12/#13 remain open.

## Supported module form

- One compute `OpEntryPoint` `main`, one `LocalSize` (x only, y=z=1).
- Exactly one `StorageBuffer` Block binding and one `OpTypeSampledImage` wrapping a plain 2D, non-arrayed,
  non-multisampled `OpTypeImage` with `Sampled=1`, `Depth=0`, float32 `Sampled Type` and `Image Format=Unknown`.
  Integer/signed sampled types, wrong/missing sampled type, depth/shadow, arrays, multisampling and non-2D are rejected.
- Both descriptors on set 0, binding count 1, unique bindings.
- `res.json` must declare `sbufs` and `textures` matching the module; `images`, `cbufs`, arrays, multisampling,
  non-2D, counts != 1, duplicate bindings, unknown stages and malformed SPIR-V/sidecar are rejected before any Vulkan
  object exists.

## Execution / ownership

`dm_vk_reflect_compute_sampled` builds the descriptor set from the module-derived bindings and a caller-owned
`dm_vk_texture` + `dm_vk_sampler`. The texture must be the RGBA8 UNORM, 2D, single-layer, single-level result of
`dm_vk_texture_upload*`, in `SHADER_READ_ONLY_OPTIMAL`. The consumer never destroys or takes ownership of caller
texture/sampler resources; failure leaves the output canvas untouched. The storage buffer is temporary and owned by
the call. No generic registry/framework, no `SetDestination`, no game/NVN resource inference.

## Evidence

Owned `vulkan_reflect_sampled` fixture: a 4x2 nonuniform texture sampled with nearest/clamp into `packUnorm4x8` words,
compared against an independent host oracle; empty/invalid sampler, multi-level texture, sidecar mismatch,
`textures` count 2, duplicate binding, unsupported `images` category and capacity preflight all fail without writing the
canvas. Host MoltenVK success is not Adreno/device or compliance evidence.
Post-preflight failure injection covers descriptor-pool creation, command-buffer end and readback invalidation; all
leave the canvas unchanged, balance the consumer-owned shader, descriptor/pipeline layouts, pipeline, pool and
pool-owned set (plus buffer/memory/command/fence), and intercept caller image/view/sampler/memory destruction.
A deliberately leaked-shader build fails the same test while the unmodified consumer passes; no GPU completion or
image-rollback claim.
