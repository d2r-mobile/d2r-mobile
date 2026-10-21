# Four statically-called NVN red stubs: boundaries, evidence and proposed mappings (G0, issue #41)

中文要点：本文只做**源码级边界**与**可独立领取的实现任务**。四个入口的静态调用点存在（各 1/2/2/2），
运行时是否可达**未知**；NVN 没有公开规范，凡名字不能证明的行为一律标为 blocked，不写猜测映射、不做 no-op。
文中把「NVN 已由仓库头文件证明的类型/宽度」与「借 GL/Vulkan 对应物提出的消费者要求」分开标注：
`[NVN]` = 仓库可证；`[HYP]` = 借对应 API 的假设；`[OURS]` = 我们实现必须自保的检查，不代表 NVN 行为。
每个入口对应一个子 Issue：#53（SetPatchSize）、#54（MultiDrawArraysIndirectCount）、
#55（MultiDrawElementsIndirectCount）、#56（DownsampleTextureView，语义未定，先取证）。

This document is source-level. It says what the repository can prove about four NVN command-buffer entries the game
looks up and calls statically, what it cannot prove, and what a separately claimable implementation slice would have to
own. It is **not** a compatibility, reachability or playability claim, and it does not implement any of the four.

## Evidence rules used here

1. **Repository facts** are cited by file and line, from `main` at the commit that added this file.
2. **NVN protocol facts** are only asserted where the repository's own headers or prototypes show them
   (`dm/gfx/nvn/include/nvn.h`, `dm/gfx/nvn/nvn_protos.h`). NVN has no public specification to cite; anything else is
   marked **blocked**.
3. **Foreign API facts** are cited to primary Khronos pages and labelled `[HYP]` where used as a candidate mapping.
   They never prove NVN behaves the same way; element layouts in particular are *hypotheses* here.
4. `[OURS]` marks checks a safe Vulkan consumer must perform regardless of what NVN validates. They are our
   requirements, not claims about NVN.
5. **Runtime reachability is unknown** for all four entries. The only runtime note in the tree is a comment in
   `dm/gfx/nvn/hle_nvn.c:929-933` stating that ~42,000 frames across four host sessions produced zero calls; that is an
   anecdote from an earlier Apple-host session, not a reproducible measurement, and it says nothing about Android.

## Baseline: what the four entries are today

| Entry | Static call sites | Slot | First call site | Current behaviour |
|---|---:|---|---:|---|
| `nvnCommandBufferSetPatchSize` | 1 | `320f548` | `1746780` | red stub |
| `nvnCommandBufferMultiDrawArraysIndirectCount` | 2 | `320f5c8` | `1747614` | red stub |
| `nvnCommandBufferMultiDrawElementsIndirectCount` | 2 | `320f5d0` | `1747784` | red stub |
| `nvnCommandBufferDownsampleTextureView` | 2 | `320f780` | `1745e54` | red stub |

Source: `dm/gfx/nvn/nvn_slots.tsv:394,410,411,465` — the #11 inventory table (`docs/android/nvn-inventory.md:56-66`)
lists the same counts. These are **static call sites in `main`**, not call counts at run time.

"Red stub" has an exact meaning here. `nvn_unimpl_c()` (`dm/gfx/nvn/hle_nvn.c:59-63`) keeps a per-entry counter in
`static uint8_t g_unimpl_seen[NVN_NFUNC]` (`hle_nvn.c:57`) and logs when `!g_unimpl_seen[i]++` is true:

```
【红】NVN 未实现被调用：<name> (x0=… x1=… x2=…)
```

Because the counter is 8-bit, this is **not** exactly once per entry: it logs on call 1 and again every time the
counter wraps (call 257, 513, …), i.e. once per 256 calls. The counter is not atomic, so concurrent first calls can also
log more than once. The function always returns 0 and writes no state.

The four entries are deliberately **not** in the `g_nops` list (`hle_nvn.c:935-955`); the comment at `hle_nvn.c:929-933`
explains that they were kept as red stubs so a real call cannot pass silently.

### Documentation drift found while checking this (not edited by this task)

- `dm/gfx/nvn_metal_mapping.md:54` and `dm/gfx/README.md:183` list these entries among the empty `g_nops`
  implementations. They are not in `g_nops` (`hle_nvn.c:929-966`); they are red stubs.
- `docs/android/nvn-inventory.md:62-65` calls the failure mode silent / "wrong pixels rather than a crash". The actual
  behaviour is a periodic red log and no work recorded; that is neither silent nor pixel-producing.

## Types and widths: known vs hypothesised

| Fact | Value | Source |
|---|---|---|
| `NVNbufferAddress` | `uint64_t` | `[NVN]` `nvn.h:41` |
| `NVNcommandBuffer` | reserved 0xA0 bytes, game-owned storage | `[NVN]` `nvn.h:131`; host mirror `cmd_t` in `nvn_obj.h` |
| `NVNtexture` / `NVNtextureView` | 0xC0 / 0x28 bytes | `[NVN]` `nvn.h:120-127`; `FITS(view_t, NVNtextureView)` `nvn_obj.h:39` |
| `stride` parameter | `ptrdiff_t` | `[NVN]` prototype `nvn_protos.h:384-385` (width is the host ABI's) |
| Indirect element layouts (16 / 20 bytes) | DrawArrays: count, instanceCount, first, baseInstance; DrawElements: count, instanceCount, firstIndex, baseVertex, baseInstance | `[HYP]` OpenGL 4.6 counterparts (links below); NVN does not document these in the repository |
| Vulkan counterparts | `VkDrawIndirectCommand`: 4×uint32; `VkDrawIndexedIndirectCommand`: 4×uint32 + int32 vertexOffset | `[HYP]` mapping target only |

- `VkDrawIndirectCommand`: https://docs.vulkan.org/refpages/latest/refpages/source/VkDrawIndirectCommand.html
- `VkDrawIndexedIndirectCommand`: https://docs.vulkan.org/refpages/latest/refpages/source/VkDrawIndexedIndirectCommand.html
- `vkCmdDrawIndirectCount` (valid usage quoted in Entry 2): https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndirectCount.html
- `vkCmdDrawIndexedIndirectCount` (valid usage quoted in Entry 3): https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirectCount.html
- `vkCmdBindIndexBuffer`: https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindIndexBuffer.html
- `VkPhysicalDeviceVulkan12Features`: https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan12Features.html
- GL counterparts: [glMultiDrawArraysIndirectCount](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glMultiDrawArraysIndirectCount.xhtml),
  [glMultiDrawElementsIndirectCount](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glMultiDrawElementsIndirectCount.xhtml),
  [glPatchParameter](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPatchParameter.xhtml)

## Entry 1 — `nvnCommandBufferSetPatchSize` (child: #53)

- **Signature**: `void(nvnCommandBuffer*, int)` (`nvn_protos.h:368`; public prototype `nvn.h:1573`).
- **Data units**: the `int` is a scalar patch size, taken to be *vertices per patch* (`[HYP]`, analogue of GL
  `GL_PATCH_VERTICES`; GL requires `1..GL_MAX_PATCH_VERTICES`). The candidate NVN bound is
  `NVN_DEVICE_INFO_MAX_PATCH_SIZE` (`[NVN]` `nvn.h:311` = 0x4F). The ABI typedef proves only the parameter type:
  NVN's validation, error behaviour and whether the value is per command buffer or per draw are **unproven**.
- **Ownership / lifetime**: the value is a scalar, but `cmd` **is a pointer** to game-owned `NVNcommandBuffer` storage
  (`[NVN]` reserved 0xA0, `nvn.h:131`), mirrored in place by `cmd_t`. A recording implementation may read and write that
  object during the call and writes the command into command memory the game supplied; it must not retain `cmd`
  afterwards. `nvnCommandBufferFinalize` (`hle_nvn.c:484`) clears the host magic, so nothing may assume the buffer is
  valid after finalize. Whether NVN resets the patch size at render-target changes or finalize is **unknown**.
- **Feature / limit queries**: NVN `NVN_DEVICE_INFO_MAX_PATCH_SIZE`; Vulkan `tessellationShader` and
  `maxTessellationPatchSize`. The translator contract only models `vs/ps/cs`
  (`dm/gfx/tools/dmsh_spirv_contract.py:39`) with target ABI `vulkan1.1` (`:44`), and the Android backend has no
  hull/domain pipeline — a truthful implementation is **blocked on tessellation stages**.
- **Proposed mapping (hypothesis, not adopted)**: record the value; set
  `VkPipelineTessellationStateCreateInfo::patchControlPoints` for `PATCHES` pipelines.
- **Unsupported behaviour**: red diagnostic naming the entry, the requested value and the missing capability; no state
  change; a following `PATCHES` draw must never silently use a default.
- **Owned tests**: recorder-level positive (value 4 observed in the record), negatives (0, negative, above the queried
  maximum, feature off), lifetime negative (record, finalize the command buffer, replay must not read the old `cmd`),
  and a discrimination case where a following `PATCHES` draw must fail rather than draw with an assumed default.
- **Reachability note**: one static call site, while `SetInner/OuterTessellationLevels` have **zero**
  (`nvn_slots.tsv:395-396`). Whether the call is setup-only, and whether patches are ever drawn, is unknown.

## Entry 2 — `nvnCommandBufferMultiDrawArraysIndirectCount` (child: #54)

- **Signature**: `void(cmd, NVNdrawPrimitive, NVNbufferAddress indirect, NVNbufferAddress drawCount,
  int maxDrawCount, ptrdiff_t stride)` (`nvn_protos.h:384`).
- **`[NVN]` known**: `indirect` and `drawCount` are 64-bit GPU addresses; `maxDrawCount` is `int`; `stride` is
  `ptrdiff_t`; the primitive enum includes `NVN_DRAW_PRIMITIVE_PATCHES = 0xE` (`nvn.h:735`).
- **`[HYP]` units**: `indirect` points at an array of 16-byte elements `{count, instanceCount, first, baseInstance}`;
  `drawCount` points at one 32-bit count; `stride` is bytes between elements; `maxDrawCount` clamps. Whether NVN's
  `drawCount` is the address of a count (vs a byte offset into a bound buffer), whether the count is read on the GPU,
  and whether `stride = 0` means tightly packed are **blocked**.
- **GPU address → owned resource `[OURS]`**: this fork maps GPU address = CPU address (`hle_nvn.c:16`), but a Vulkan
  consumer must resolve each address through the memory-pool range registry to an owned `VkBuffer` plus a byte offset;
  casting a raw address to `VkBuffer` is invalid. Resolution must be overflow-safe (`base + off` in 64-bit, reject
  wrap), reject addresses outside every registered range, and keep the buffer alive until the submission that consumes
  the recorded command has completed (a retainer, not a borrowed pointer).
- **Bounds `[OURS]`** — two distinct layers, kept separate:
  - **Vulkan valid usage (must hold regardless of the count value at run time)**:
    `stride` is a multiple of 4 **and** `>= sizeof(VkDrawIndirectCommand)` = 16
    (`VUID-vkCmdDrawIndirectCount-stride-03110`); `offset` is a multiple of 4 (`-offset-02710`);
    `countBufferOffset` is a multiple of 4 (`-countBufferOffset-02716`); both `buffer` and `countBuffer` were created
    with `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` (`-buffer-02709`, `-countBuffer-02715`);
    `countBufferOffset + 4 <= size(countBuffer)` (`-countBufferOffset-04129`); the count read from the buffer must be
    `<= VkPhysicalDeviceLimits::maxDrawIndirectCount` (`-countBuffer-02717`). When `maxDrawCount >= 1`,
    `offset + stride * (maxDrawCount - 1) + 16 <= size(buffer)` (`-maxDrawCount-03111`).
    The capacity bound uses **`maxDrawCount`, not** `min(count, maxDrawCount)`: the GPU count is only known during
    execution and may be larger than any value observed today, while valid usage is fixed when the command is recorded.
    `maxDrawCount == 0` is valid and performs no draws — there is no `-1` arithmetic in that case, so an implementation
    must branch instead of subtracting.
  - **NVN constraint (additional, not a substitute)**: `indirectOffset` must satisfy
    `NVN_DEVICE_INFO_INDIRECT_DRAW_ALIGNMENT` (`nvn.h:245`). Whether NVN's `stride == 0` means "tightly packed" is a
    **hypothesis**; it may only be normalized to 16 after evidence, otherwise the call is rejected as unsupported, not
    guessed.
  - **Narrowing**: `stride` is `ptrdiff_t` and `maxDrawCount` is `int` in the NVN prototype, while Vulkan takes
    `uint32_t` for both. Negative values and values above `UINT32_MAX` are rejected; they are never truncated. All
    `offset + stride * (maxDrawCount - 1) + 16` arithmetic is performed in 64 bits and checked before use.
- **Unsupported behaviour**: red diagnostic with entry, required feature, stride and maxDrawCount; zero draws recorded.
- **Owned tests** (plan, not claimed tests): count=2 draws both triangles, count=1 draws only the first, count above
  `maxDrawCount` truncates, stride 16 vs 32 agree, `maxDrawCount == 0` records zero draws without underflow,
  stride/misaligned/out-of-range record negatives, buffer-lifetime negative (free or rebind the pool before submit),
  feature-off negative, and a discrimination case where the count buffer reports 0 or 1 but `maxDrawCount` is large and
  the indirect buffer only has room for one record — the Vulkan capacity requirement still fails, so the consumer must
  reject even though "today's count" would fit. A discrimination case where treating the count as a CPU constant fails.
- **Feature / limit queries**: `VkPhysicalDeviceVulkan12Features::drawIndirectCount`
  (https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan12Features.html) or
  `VK_KHR_draw_indirect_count`; `VkPhysicalDeviceLimits::maxDrawIndirectCount`; `multiDrawIndirect`. The consumer ABI is
  `vulkan1.1`, so the path must be gated explicitly.
- **Proposed mapping (hypothesis, not adopted)**: `vkCmdDrawIndirectCount(commandBuffer, buffer, offset, countBuffer,
  countBufferOffset, maxDrawCount, stride)`; Vulkan executes `min(count in buffer, maxDrawCount)`
  (https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndirectCount.html). A CPU-readback fallback would
  require both explicit synchronization against the producing submission **and** observed evidence that NVN's count
  behaves the same; host-visible memory alone is not sufficient.
- **Unsupported behaviour**: red diagnostic with entry, required feature, stride and maxDrawCount; zero draws recorded.
- **Owned tests** (plan, not claimed tests): count=2 draws both triangles, count=1 draws only the first, count above
  `maxDrawCount` truncates, stride 16 vs 32 agree, `maxDrawCount == 0` records zero draws without underflow,
  stride/misaligned/out-of-range record negatives, buffer-lifetime negative (free or rebind the pool before submit),
  feature-off negative, and a discrimination case where the count buffer reports 0 or 1 but `maxDrawCount` is large and
  the indirect buffer only has room for one record — the Vulkan capacity requirement still fails, so the consumer must
  reject even though "today's count" would fit; plus a case where treating the count as a CPU constant fails.

## Entry 3 — `nvnCommandBufferMultiDrawElementsIndirectCount` (child: #55)

- **Signature**: `void(cmd, NVNdrawPrimitive, NVNindexType, NVNbufferAddress indirect, NVNbufferAddress drawCount,
  NVNbufferAddress indices, int maxDrawCount, ptrdiff_t stride)` (`nvn_protos.h:385`).
- **`[NVN]` known**: index types are `UNSIGNED_SHORT = 0x1`, `UNSIGNED_INT = 0x2` (`nvn.h:742-743`); the index buffer
  is passed as an explicit 64-bit GPU address (not a bound element buffer, unlike GL).
- **`[HYP]` units**: 20-byte elements `{count, instanceCount, firstIndex, baseVertex, baseInstance}`; `baseVertex` is a
  signed value added to each index (Vulkan models it as `int32_t vertexOffset`).
- **GPU address → owned resource `[OURS]`**: the same resolution, overflow and lifetime rules as entry 2, applied
  separately to `indirect`, `drawCount` and `indices`.
- **Bounds `[OURS]`**: the Vulkan rules of entry 2 apply unchanged, with the indexed sizes and VUIDs —
  `stride` multiple of 4 **and** `>= sizeof(VkDrawIndexedIndirectCommand)` = 20
  (`VUID-vkCmdDrawIndexedIndirectCount-stride-03142`); `offset` multiple of 4 (`-offset-02710`);
  `countBufferOffset` multiple of 4 (`-countBufferOffset-02716`); `buffer`/`countBuffer` created with
  `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` (`-buffer-02709`, `-countBuffer-02715`);
  `countBufferOffset + 4 <= size(countBuffer)` (`-countBufferOffset-04129`); buffer count
  `<= maxDrawIndirectCount` (`-countBuffer-02717`); and when `maxDrawCount >= 1`,
  `offset + stride * (maxDrawCount - 1) + 20 <= size(buffer)` (`-maxDrawCount-03143`, again worst case
  `maxDrawCount`, and no `-1` arithmetic when it is 0).
  On top of that, the resolved index range is `indexOffset + firstIndex * indexWidth` for `count * indexWidth` bytes;
  `firstIndex * indexWidth` must be accounted for separately, because a `count * indexWidth` check alone accepts an
  out-of-range `firstIndex`. Multiplications and additions are overflow-checked. `baseVertex` is signed, in vertices, and
  is added to each fetched index; the sum must be checked against the vertex range the bound pipeline declares, where one
  is known. NVN's own checks and its `baseVertex` sign/units are **unproven**; these are consumer requirements. The same
  checked narrowing applies: negative or `> UINT32_MAX` `stride`/`maxDrawCount` are rejected, never truncated.
- **Feature / limit queries**: same as entry 2, plus `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` for the resolved index buffer and
  the `VkIndexType` mapping.
- **Proposed mapping (hypothesis, not adopted)**: `vkCmdBindIndexBuffer(commandBuffer, indexBuffer, indexOffset,
  indexType)` + `vkCmdDrawIndexedIndirectCount(...)`. Binding requires `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` and an offset
  that is a multiple of the index size
  (https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindIndexBuffer.html).
- **Unsupported behaviour / tests** (plan, not claimed tests): as entry 2, with the indexed sizes — including
  `maxDrawCount == 0` (no underflow, zero draws), a `firstIndex` out-of-range case, and the capacity discrimination case
  where the count buffer reports 0 or 1 but `maxDrawCount` is large and the indirect buffer only has room for one
  20-byte record, which must still fail. The discriminating negative must show that ignoring the resolved `indices`
  buffer (using a previously bound one) produces wrong pixels.

## Entry 4 — `nvnCommandBufferDownsampleTextureView` (child: #56, **blocked on semantics**)

- **Signature**: `void(cmd, const NVNtexture *src, const NVNtextureView *srcView, const NVNtexture *dst,
  const NVNtextureView *dstView)` (`nvn_protos.h:439`).
- **Family**: `Downsample`, `TiledDownsample`, `DownsampleTextureView`, `TiledDownsampleTextureView`
  (`nvn.h:1686-1694`). **Only `DownsampleTextureView` has static call sites (2)**; the other three have **zero**
  (`nvn_slots.tsv:463-466`). This document and child #56 therefore cover this one entry, not the family.
- **What is proven**: the view is a 0x28-byte value with levels/layers/format/swizzle/target setters
  (`nvn.h:127`, `nvn_protos.h:167-179`), mirrored by `view_t` (`nvn_obj.h:39`); recording must copy those fields and
  must not retain the `NVNtexture*` / `NVNtextureView*` pointers after the call.
- **What is not proven**: the sampling rule (point / 2×2 average / sRGB-decoded average), the required size relation
  between src and dst, format conversion and depth/stencil handling, aliasing between src and dst views, and the
  required ordering relative to render passes.
- **Why no mapping is proposed**: `vkCmdBlitImage` (nearest/linear), `vkCmdCopyImage`, and a compute reduction all fit
  the name and differ in pixels; choosing without evidence would be inventing protocol. No public NVN specification is
  available to cite, and no HLE reference exists in the tree.
- **Unsupported behaviour**: a call must produce an explicit "semantics unestablished (G0.4)" red diagnostic and no GPU
  work — not a no-op that looks like success.
- **Owned tests now**: assert the diagnostic; assert pixels unchanged across repeated calls; assert that a command
  recorded and then finalized leaves no dangling texture/view pointer.

## What remains blocked (and what would unblock it)

| Question | Blocking evidence needed |
|---|---|
| Do any of the four call sites execute at run time? | permitted instrumented run with per-entry call counts; static sites prove nothing |
| NVN's argument units/validation for the two count variants | SDK documentation or captured argument behaviour across content |
| `baseVertex` sign and index-range behaviour | captured arguments plus a case with a negative `baseVertex` |
| Whether `SetPatchSize` is meaningful without tessellation stages | shader inventory showing patch/tess stages, plus a captured argument value |
| `DownsampleTextureView` sampling rule and size/format constraints | captured arguments (formats, levels, layers, dimensions) and output comparison |

## Reproduce the static facts

```
python3 dm/gfx/tools/nvn_inventory.py --check     # exits 1 today: exactly these four static-called red stubs
awk -F'\t' '$1 ~ /SetPatchSize|MultiDraw(Arrays|Elements)IndirectCount|DownsampleTextureView/ {print $1,$3,$4}' \
    dm/gfx/nvn/nvn_slots.tsv                      # the rows quoted above
```

`--check` exiting 1 is the intended inventory signal, not a test failure to hide.
