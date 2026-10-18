# 有界 indexed+texture 离屏绘制（#115）

在 #112 的有界索引绘制之上，增加一个固定接口的纹理采样图形路径：调用者提供 float2 位置 + float2 UV、uint16 索引和单个 RGBA8 2D 纹理，绘制到 RGBA8 离屏目标并读回。它不是 NVN 格式/纹理 ABI、游戏命令、swapchain、帧调度或通用 shader 接口。

## 固定接口

- 顶点：`dm_vk_indexed_textured_vertex { float position[2]; float uv[2]; }`，stride 16；位置有限且在 NDC `[-1,1]`，UV 有限且在 `[0,1]`。
- `uint16` 索引；`index_count` 是 3 的倍数，每个索引 `< vertex_count`；triangle-list。
- 顶点属性固定为 location 0 `R32G32_SFLOAT` offset 0、location 1 `R32G32_SFLOAT` offset 8。
- 单张 RGBA8 UNORM 2D、单层、单级纹理；调用者给出 `texture_pixels` 与**精确** `texture_size = width*height*4`。
- 固定 nearest/nearest、clamp-to-edge、lod 0 的单个 sampler；自有 `indexed_textured.vert` / `.frag`。
- 目标为 RGBA8 UNORM 2D 单层离屏 image，每帧 clear 为不透明黑。

## 预检与所有权

- 创建任何 Vulkan 对象之前校验：空指针、固定 stride、顶点数 3–65536、索引数 3–196608 且为 3 的倍数、索引边界、顶点有限性与范围、纹理精确字节数、64 位尺寸、目标/纹理维度、`maxImageDimension2D`、viewport/framebuffer/storage-range、RGBA8 color-attachment/transfer/sampled 格式能力。
- 顶点、索引和纹理都是借用输入：同步复制到自有 buffer/image；不保留指针，也不释放调用者内存。
- 失败不修改调用者输入或画布；成功只在 GPU 读回完成后写入画布。
- 自有纹理、sampler、buffer、image/memory/view、render pass、framebuffer、shader、descriptor layout/pool/set、pipeline/layout、command/fence 在成功或失败路径统一回收。

## 验收

- 自有 2×2 非均匀纹理（红/绿/蓝/黄四个不同 texel）映射到索引四边形；独立像素 oracle 检查四个内部点和背景点。
- 改纹理后同一目标必须显示新四色；改尺寸（24×24）重新绘制后四色仍正确，旧内容不残留。
- UV 负控制：所有 UV 固定为 0.25 时四个检查点必须都采样同一个红色 texel；忽略纹理或 UV 的实现无法通过。
- 预检负例：纹理字节短、空纹理指针、零宽纹理、错误 stride、过少顶点、非 3 倍数/超上限索引数、空指针、NaN 位置、越界 UV/索引、目标容量不足、超宽目标；全部 GPU 前拒绝且画布保持哨兵。
- 后创建失败注入：descriptor-pool、pipeline、draw command 结束、readback invalidate 四条路径；跟踪三 draw buffer、2 image/memory/view、sampler、两个 shader、descriptor layout/pool/set、pipeline/layout、command/fence，全部计数归零，调用者顶点/索引/纹理与画布哨兵不变。

## 边界

- 仅宿主 MoltenVK/授权 Vulkan 证据；不声称 Adreno/真机、GPU 完成或镜像回滚。
- 单张固定格式纹理和固定 sampler 子集；无 NVN 纹理格式/拓扑、通用 descriptor registry、swapchain、游戏命令或通用 shader ABI。
- 未修改 peer HUD、touch 或 reflection 实现；父 #18/#13 保持打开。
