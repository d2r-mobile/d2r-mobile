# 固定布局 uniform 原生消费子集（#109）

本切片在既有反射消费方上增加一种、也是唯一一种 uniform 常量块输入：只读、固定 16 字节的 std140 `uvec4`，与一个输出 storage buffer 组合为单入口 1D compute。它不解析 NVN 常量缓冲 ABI，也不代表 Maxwell/Adreno 语义。

## 支持的模块形态

- 单个 `OpEntryPoint`，`GLCompute`，入口 `main`，恰好一个 `LocalSize`（y=z=1）。
- set 0、count 1、绑定唯一：
  - 一个 `StorageBuffer` Block（输出）；
  - 一个 `Uniform` Block（输入），结构体必须带 `Block` 装饰，且只有一个成员；
  - 成员必须是 `uvec4`（`OpTypeVector`，4 个 uint32 分量），`Offset` 装饰为 0，类型大小严格 16。
- 至多一个既有 push-constant 块；本夹具不使用。
- 不支持：多成员/数组/运行时数组、其他成员类型或布局、多个 uniform 块、uniform+采样/图像混用、dynamic offset、未知 stage、畸形模块。
- `OpTypeInt` 必须恰好带宽度与 Signedness（Signedness 仅 0/1）；缺失或非法 Signedness 直接拒绝，绝不默认成 unsigned；`OpTypeFloat` 独立要求恰好宽度。
- `res.json` 只交叉核对，不单独决定绑定。`cbufs` 必须显式声明 `binding`、`count=1`、`offset=0`、`size=16`，并与模块推导值逐项一致；缺少 `offset`/`size` 或数值不符即拒绝。`images` 必须为空。
- 所有绑定类别的 `count` 若出现，必须是 JSON 整数且精确为 1（在收窄前比较）；`UINT32_MAX`、`2^32+1`、字符串/布尔均拒绝；沿用既有“缺省视为 1”的约定。

## 生命周期与失败语义

- 调用者在 `dm_vk_reflect_compute_uniform` 调用期间提供恰好 16 字节输入；函数同步复制到自有 host-visible uniform buffer，返回后不再引用调用者内存。
- 非一致内存：上传前调用既有 `dm_vk_buffer_sync(..., upload=1)` 刷新；输出读回前调用 `dm_vk_buffer_sync(..., upload=0)` 失效。
- 预检在创建任何 Vulkan 对象之前完成：`dm_vk_reflect_check` 校验设备 `maxUniformBufferRange`，调用方再校验 `uniform_size == plan->cbuf_size`、非空输入、画布容量和 push 长度。
- 失败不改画布、不改调用者输入；所有自有 shader、descriptor/pipeline layout、pipeline、pool、pool-owned set、uniform/output buffer 均回收。调用者从不拥有或销毁这些对象。

## 验收边界

- 自有 `uniform.comp` 用四个非平凡、互不相同的 uint 输入做 32 位无符号变换；宿主 CPU 用独立同式 oracle 逐字比较，错误绑定或忽略 uniform 都不能通过。
- 负例覆盖：短输入、空输入、sidecar 的 `size`/`offset` 不符/缺失、有效但非 `uvec4` 的模块、设备 uniform-range 上限、保留的既有 storage/sampling 拒绝。
- 纯 CPU 载入负例：删除 `OpTypeInt` Signedness 操作数的模块必须拒绝；sidecar `count` 为 `2^32+1`、`UINT32_MAX`、字符串或布尔值必须拒绝；缺失 `count` 仍按既有约定视为 1。
- 后创建失败注入覆盖 descriptor-pool 创建、command 结束、读回失效；检查全部自有对象计数归零、画布哨兵不变、调用者输入不变。
- 只在授权宿主 Vulkan/MoltenVK 上执行；宿主通过不等于 Android Adreno/设备验收，也不声称 GPU 完成或镜像回滚。

## 验证

- Host CTest：`vulkan_reflect_uniform` / `vulkan_reflect_uniform_fail` 以及既有 reflect 测试全部通过。
- ASAN/UBSAN：uniform 数值测试与失败注入测试通过。
- Android NDK r29 arm64-v8a API 29 目标构建；exact-head CI 四个任务通过。
