# #133 纹理索引绘制的严格 UV/采样器接口 plan

## 范围

把既有 #115 的有界 textured indexed offscreen consumer 接到 #125 的 owned VS/PS plan 机制上。只支持当前固定纹理子集：

- VS：`location 0 vec2 float` position + `location 1 vec2 float` UV，一个 `vec2 float` varying；标准 `gl_Position`/`gl_PerVertex` Position。
- FS：一个与 VS 匹配的 `vec2 float` varying 输入，一个 `location 0 vec4 float` 输出。
- FS：恰好一个 combined image sampler，沿用现有 `set = 0, binding = 0` 约定。
- 采样图像必须是 2D、非数组、非 shadow/depth、非 multisample、sampled、Unknown format、float32 sampled type；只接受 `OpTypeSampledImage`，拒绝独立 sampler、数组和额外 descriptor。
- 既有 RGBA8 nearest/clamp 单级纹理、`float2 position + float2 UV`、uint16 索引和有界绘制参数保持不变。

`dm_vk_textured_plan_load()` 复用同一严格 SPIR-V 解析器，只增加 textured 模式；`dm_vk_graphics_plan_load()` 的 colored 行为和 `dm_vk_graphics_plan_check()` 仍保留。

## 拒绝边界

在创建 Vulkan 对象前拒绝：

- 错误执行模型、入口、header bound 或未定义入口函数；
- 错误 UV/position/varying 类型、location、重复/缺失 location 和 VS→FS varying 不匹配；
- 缺失/额外 descriptor、数组 descriptor、错误 set/binding、独立 sampler、非 float32/非 2D/depth/array/MS sampled image；
- push constant、storage buffer、额外接口；
- sidecar stage/hash 不一致、未知键、畸形/截断/超限 SPIR-V、函数参数计数越界；
- plan-owned 字节被修改后字段/hash 不一致。

## 消费与测试

`dm_vk_draw_indexed_textured_plan_offscreen()` 先 `dm_vk_graphics_plan_check()`，再使用 plan-owned VS/PS 代码、UV location 和 sampler binding 构建实际 indexed pipeline。错误的 plan UV location/binding 在 GPU 前失败，画布保持不变。

`dm/android/tests/vulkan_textured_interface.c` 覆盖：

- 合法 plan、metadata hash/stage、字段和 plan check；
- 错误 UV 类型、varying link、set/binding、数组/额外/缺失 descriptor、非 float sampler、错误 stage；
- 实际 plan-owned 2×2 RGBA8 纹理像素 oracle（红/绿/蓝/黄/背景）；
- 常量 UV 负控（本应只采到红色 texel）；
- 错误 plan UV location / sampler binding 的预检拒绝和画布 sentinel；
- plan free 清理。

## 验证边界

仅 host Vulkan/MoltenVK 自有合成纹理证据；没有 Android/Adreno 设备、游戏、SDK、rtld 或性能声明。Android 结果仅是 arm64 cross-build。该子集不是通用 shader 编译器、NVN 图形格式/拓扑支持或 descriptor 框架。
