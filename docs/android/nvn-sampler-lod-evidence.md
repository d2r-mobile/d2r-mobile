# 多级纹理与 LOD 采样：自有 Vulkan 证据（#13）

# Multi-mip texture upload and LOD sampling: owned Vulkan evidence (#13)

本文只描述本仓库自有的、可复现的 Vulkan 侧证据。它不是 NVN 真机行为证明，也不代表 Adreno/设备验收。

This document describes only owned, reproducible Vulkan-side evidence in this repository. It is not proof of NVN
behaviour on real hardware and is not Adreno/device acceptance.

## 上传契约 | Upload contract

`dm_vk_texture_upload_mips(vk, width, height, levels, level[], out)`（`dm/android/vulkan/vk_texture_mips.c`）是显式
自有接口：调用者按级提供线性 RGBA8 数据与行距，本模块不解析、也不假设任何 guest/NVN 存储布局或 NVN pool 集成。

`dm_vk_texture_upload_mips` is an explicit owned entry point: the caller supplies linear RGBA8 data and a row stride per
level. The module does not parse or assume any guest/NVN storage layout, and there is no NVN pool integration.

- 格式固定 `VK_FORMAT_R8G8B8A8_UNORM`，2D、单层，`levels` 接受 1..完整链的任意前缀（`levels <= floor(log2(max(w,h)))+1`），超出完整链拒绝。
- `row_bytes = 0` 表示紧排；非 0 必须 ≥ `width*4` 且为 4 的倍数。
- 每级 `bytes` 必须覆盖 `row_bytes*(h-1) + width*4`（最后一行无需补齐），`size` 用 64 位算术校验。
- 维度受 `maxImageDimension2D`、`maxMipLevels`、`maxResourceSize` 与 `maxStorageBufferRange` 约束。
- 全部校验在 `vkCreateImage` 之前完成；失败时 `out` 不被修改，已建对象在 `done:` 统一回收。
- 既有 NVN block-linear `dm_vk_texture_upload` 已经支持多级，本切片未改动它，行为保持不变。

- Format is fixed to `VK_FORMAT_R8G8B8A8_UNORM`, 2D, single layer; `levels` accepts any prefix 1..full chain
  (`levels <= floor(log2(max(w,h)))+1`), values beyond the full chain are rejected.
- `row_bytes = 0` means tight; otherwise it must be >= `width*4` and a multiple of 4.
- Each level's `bytes` must cover `row_bytes*(h-1) + width*4` (the last row needs no padding); `size` is checked in 64-bit.
- Dimensions are bounded by `maxImageDimension2D`, `maxMipLevels`, `maxResourceSize`, `maxStorageBufferRange`.
- Every check happens before `vkCreateImage`; on failure `out` is untouched and partial objects are released at `done:`.
- The existing NVN block-linear `dm_vk_texture_upload` already supports multiple levels; it is unchanged by this slice.

接受任意前缀（1..完整链）是刻意的：稀疏上传与 mip 生成不在本切片内。

Accepting any prefix (1..full chain) is deliberate: sparse uploads and mip generation are out of scope for this slice.

## 采样对照 | Sampling matrix

`dm/android/tests/vulkan_sampler_mips.c` 用 4×4 三级纯色纹理（L0 红、L1 绿、L2 蓝）在 MoltenVK 上做像素对照：

| 场景 | 采样器 | 结果判据 |
| --- | --- | --- |
| 整数 LOD 0/1/2 | NVN minf=2（nearest mip） | 16/16 像素精确等于对应级纯色 |
| 分数 LOD 0.5 | NVN minf=4（linear mip） | 每通道等于两级均值 ±2，且不等于任一纯色 |
| 分数 LOD 0.5 | NVN minf=2 | 等于某一级纯色（驱动可选最近级），且不等于均值 |
| LOD clamp | `minLod=maxLod=1`，lod 0 与 2 | 都落在 L1 |
| LOD clamp | `minLod=maxLod=2`，lod 0 | 落在 L2 |
| NVN 非 mipmap minf=0，lod 2 | LOD 固定 0 | 读 L0 |
| 行距 | `row_bytes = w*4+8` | 四角像素仍为 L0 |
| 中途失败注入 | `vkCreateImageView` 在图像/内存/暂存已建后返回错误 | 图像与内存各销毁一次，视图未创建，`out` 字节不变 |

The fixture uses a 4x4 three-level solid-colour texture (L0 red, L1 green, L2 blue) and asserts independent pixel
oracles. See the table above for the exact criteria. A separate injected test fails `vkCreateImageView` after the image,
memory and staging buffer exist and checks balanced image/memory destruction plus an unchanged `out`.

## 明确未知 | Explicit unknowns

- 无真实 NVN 运行时/游戏执行，无 NVN pool 或 block-linear → Vulkan mip 集成。
- 无 Adreno/设备验收；无 validation layer（本机未安装，相关 CTest 仍 SKIP）。
- 不覆盖各向异性、compare、border、mirror-clamp、sRGB 取样或 mip 生成。
- 未证明分数 LOD 在真实 NVN 采样器上的选择规则；此处只证明本仓库映射出的 Vulkan 采样器行为。

- No real NVN runtime/game execution, no NVN pool or block-linear to Vulkan mip integration.
- No Adreno/device acceptance, no validation layer on this host (related CTests still SKIP).
- No anisotropic/compare/border/mirror-clamp/sRGB sampling or mip generation.
- The fractional-LOD selection rule of a real NVN sampler remains unproven; only the Vulkan sampler produced by this
  repository's mapping is exercised.
