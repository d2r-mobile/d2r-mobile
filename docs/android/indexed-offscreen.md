# 有界 typed indexed 离屏绘制（#112）

本切片提供一个小型、固定接口的原生 Vulkan 离屏绘制路径：调用者提供 float2 位置 + float4 颜色顶点和 uint16 索引，绘制到 RGBA8 目标并读回。它不解析 NVN 格式/拓扑，不是游戏命令、swapchain、帧调度或通用 shader ABI。

## 固定接口

- 顶点：`dm_vk_indexed_vertex { float position[2]; float color[4]; }`，步长 `sizeof = 24`；位置为有限 NDC `[-1,1]`，颜色为有限 `[0,1]`。
- 索引：`uint16_t`，`index_count` 必须是 3 的倍数；每个索引必须 `< vertex_count`。
- 图元：`VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`；单顶点绑定、两个属性：
  - location 0：`R32G32_SFLOAT`，offset 0；
  - location 1：`R32G32B32A32_SFLOAT`，offset 8。
- 目标：单个 RGBA8 UNORM、2D、单层、无 mip 的离屏 image；每帧 clear 为不透明黑。
- 自有 `indexed.vert` / `indexed.frag` 固定接口；不提供任意顶点格式、动态状态、descriptor、swapchain 或帧调度。

## 预检与所有权

- 在创建任何 Vulkan 对象之前检查：空指针、顶点数（3–65536）、索引数（3–196608 且为 3 的倍数）、固定 stride、每个索引边界、每个顶点的位置/颜色有限性与范围、`uint64` 字节数、目标尺寸/容量、`maxImageDimension2D` / viewport / framebuffer / storage-range 上限、RGBA8 attachment + transfer-src 格式能力。
- 顶点/索引是**借用**内存：函数同步复制到自有 host-visible buffer 后提交；不保留指针，也绝不释放调用者输入。
- 失败不修改调用者顶点/索引或输出画布；成功只在 GPU 读回完成后写入画布。
- 所有自有 buffer、image/memory/view、render pass、framebuffer、shader、pipeline/layout、command/fence 在成功或失败路径统一回收。

## 验收

- 自有双三角形夹具：左侧红色、右侧蓝色，索引顺序故意跨越两组顶点；独立像素 oracle 检查内部点和背景点。第二次绘制使用不同颜色并验证旧帧不残留；第三张目标换成 24×24 重新绘制。
- 负控制：只保留右侧索引时左侧必须回到背景，证明索引内容实际参与绘制（不是忽略索引的顶点顺序绘制）。
- 预检负例：错误 stride、过少顶点、非 3 倍数/超上限索引数、空指针、NaN 位置、越界颜色、越界索引、容量不足、零宽目标；全部在 GPU 前失败且画布保持哨兵。
- 后创建失败注入（mock 真实模块）：pipeline 创建、command 结束、读回失效三条路径；跟踪三 buffer、image/memory/view、render pass、framebuffer、两个 shader、layout、pipeline、command/fence，全部计数归零，调用者输入和画布哨兵不变。
- 宿主 MoltenVK 通过不等于 Adreno/真机、GPU 完成或镜像回滚；mock 失败不证明设备丢失或驱动安全。
