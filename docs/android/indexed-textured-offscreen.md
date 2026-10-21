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

## #117 应用侧接入（工程诊断壳）

- `app/diagnostic_scene.c` 持有接入所用的全部场景数据：非均匀 4×4 RGBA8 纹理（16 个两两不同的 texel）、NDC ±0.75 且 UV 0..1 的四边形、六个 `uint16` 索引；`app/native.c` 把它复制进本接口要求的 16 字节顶点布局并渲染 256×256 读回。未新增绘制 API、未改 peer HUD/touch/reflection 代码。
- 检查点由同一场景几何独立推算：16 个 texel 中心 + 4 个清屏角点（20 个）；不符时记录首个坏点的期望/实测 RGBA 且不呈现该帧。
- 绘制门 `dm_diag_scene_ready()` 复用 `app/lifecycle.c`：仅 `owned-diagnostic` 且控制器持有该 Surface 时绘制；暂停、停止中、已停止、`failed`、陈旧令牌与停止后的迟到回调都抑制绘制。
- 呈现接缝 `app/present_policy.c`：要求 RGBA_8888、`stride >= width`、容量覆盖 `width*height*4`，64 位尺寸运算；只清零可见行区、居中复制（窗口更小时从源居中裁剪），行距填充不读不写，拒绝路径零写入。
- APK 只打包 `indexed_textured.vert.spv` / `indexed_textured.frag.spv` 两个固定着色器资源，由 `app/build.sh` 从交叉编译输出取；不再打包旧的片段坐标图案着色器。
- 新增测试：`app_diagnostic_scene`（场景字面值 + 检查器 + 真实生命周期抑制，含停止后迟到回调）、`app_present_policy`（格式/行距/容量/溢出、拒绝零写入、居中与填充）、`vulkan_app_diagnostic_scene`（宿主 GPU 渲染同一场景 + 改 texel 负控）。前两个不依赖 Vulkan/Android，在 CI 的 `DM_ANDROID_VULKAN=OFF` 配置下也构建并运行。
- 本机已执行：宿主 71/71（2 个验证层用例按设计 SKIP）、CI host 模式、ARM64/API29 交叉编译与诊断 APK 打包（未安装）。

未验证：真机 `ANativeWindow_lock` 的格式/行距行为、Adreno/Android Vulkan 驱动、swapchain 与帧调度；不声称 Android/设备验收。

## 边界

- 仅宿主 MoltenVK/授权 Vulkan 证据；不声称 Adreno/真机、GPU 完成或镜像回滚。
- 单张固定格式纹理和固定 sampler 子集；无 NVN 纹理格式/拓扑、通用 descriptor registry、swapchain、游戏命令或通用 shader ABI。
- 未修改 peer HUD、touch 或 reflection 实现；父 #18/#13 保持打开。
