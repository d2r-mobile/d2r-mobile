# #125 有界 VS/PS 图形接口反射与索引绘制消费

## 目的

在既有 compute `cs` 反射之外，增加一个独立、严格、可复现的图形接口检查点：从自有 VS/PS SPIR-V 派生 execution model、入口名、location、组件类型和 VS→FS varying 链接；通过检查的 plan 直接驱动既有有界 indexed offscreen 绘制，不让 plan 成为没有消费者的死解析器。

本检查点不是通用 shader 编译器、NVN 图形格式翻译器、pipeline registry 或 Android/Adreno 设备验收。

## 固定子集

- 恰好一个 vertex stage 和一个 fragment stage。
- 入口必须为 `main`，且 `OpEntryPoint` 的函数 id 必须落在 SPIR-V header bound 内，并对应本模块真实定义的 `OpFunction`；该函数必须无参数、返回 `void`、control 为 0，`OpFunction` 与 `OpTypeFunction` 的返回类型一致。
- 类型、变量和函数定义 id 必须唯一且小于 header bound；重复/冲突定义、越界 id、缺失函数定义、函数参数以及不支持的入口形式在创建 Vulkan 对象前拒绝。
- VS 输入恰好 `location 0: vec2 float`（position）和 `location 1: vec4 float`（color）。
- VS 恰好一个 `gl_Position`（直接 BuiltIn 或标准 `gl_PerVertex` 输出块），并恰好一个 `vec4 float` varying 输出。
- FS 恰好一个 `vec4 float` varying 输入，且 location 必须与 VS 输出一致；FS 恰好一个 `location 0: vec4 float` 输出。
- 不支持 descriptor、uniform block、sampled image、push constant、storage buffer、任意图形 ABI、几何/tessellation、多个入口或额外接口。
- VS/FS 的 `OpEntryPoint`、`OpExecutionMode`、类型和装饰都必须落在上述固定集合内；未知/重复/缺失 decoration 直接拒绝。

`gl_PerVertex` 只允许标准 BuiltIn 成员 `Position`、`PointSize`、`ClipDistance`、`CullDistance`，且必须恰有一个 `vec4 float Position`。这不是对任意 struct 输出块的开放支持。

## Plan 与 sidecar

`dm_vk_graphics_plan_load()` 读取调用方给定的 VS/PS 文件，严格解析后复制为 plan-owned 字节，并记录：

- `position_location` / `color_location` / `varying_location`
- `position_components` / `color_components`
- VS/PS SHA-256

可选 metadata 必须是精确 JSON：

```json
{"vs_stage":"vs","vs_sha256":"<64 hex>","ps_stage":"ps","ps_sha256":"<64 hex>"}
```

只允许这四个键；未知键、重复键、缺失键、尾逗号、错误 stage 或 hash 都会在 Vulkan 对象创建前拒绝。`dm_vk_graphics_plan_check()` 在 CPU 上重新扫描 plan-owned 字节并核对字段与 hash，不创建 Vulkan 对象。

## 消费者

`dm_vk_draw_indexed_plan_offscreen()` 先调用 `dm_vk_graphics_plan_check()`，然后使用 plan-owned VS/PS 字节、plan 派生 location 和组件数构建实际 indexed pipeline。它不读取 `dm_vk_indexed_draw` 的 shader 路径；错误/被篡改的 plan 在创建 Vulkan 对象前返回失败，调用方像素缓冲保持不变。

原有 `dm_vk_draw_indexed_offscreen()` 路径和 compute 反射/消费路径保持不变。

## 测试

`dm/android/tests/vulkan_graphics_interface.c` 使用自有编译 SPIR-V 覆盖：

- 合法 plan、location/type/varying 派生和合法 sidecar；
- compute execution model 冒充 VS；
- 错误 `vec3` 和标量 color 类型；
- 错误入口名、越界入口函数、in-bound 但没有 `OpFunction` 的入口、重复/冲突定义；
- 截断/超限模块；
- VS→FS varying 不匹配；
- descriptor/uniform 与 push constant；
- sidecar hash 不匹配和未知键；
- 被篡改的 plan location/组件数在 GPU 前拒绝且不修改画布；
- 实际 plan 代码驱动的 host Vulkan/MoltenVK indexed 像素 oracle（红/蓝/黑）；
- plan free 后字段清零。

测试目标为 `dm_vulkan_graphics_interface_test`；默认 `ctest` 名称为 `vulkan_graphics_interface`，无 Vulkan 设备时返回 77 跳过 GPU 部分。

## 验证边界

- 已验证 host Vulkan/MoltenVK 的固定像素结果；没有 Android/Adreno/真实设备或游戏执行证据。
- 没有 NVN 格式、拓扑、命令流或任意 shader ABI 的支持声明。
- ASAN/UBSAN 和 Android arm64 cross-build 结果记录在 job checkpoint；cross-build 不是 APK、设备安装或运行验证。
- 本检查点只处理固定合成 shader；不授权 phone、game、SDK、rtld 或私有资产执行。
