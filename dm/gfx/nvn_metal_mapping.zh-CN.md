# NVN → Metal 对象映射

English: [nvn_metal_mapping.md](nvn_metal_mapping.md)

这份文档列出 NVN（Switch 的原生图形接口）的各类对象与命令，在 D2R Mobile 的图形层里分别落到 Metal 的什么上。图形层的整体结构见 [README.zh-CN.md](README.zh-CN.md)。

范围：游戏主程序里有调用点的 216 个 NVN 函数（清单在 `nvn/nvn_slots.tsv`）。其余 307 个接报红桩，被调到时日志打印 `【红】NVN 未实现被调用：名字`。

两条贯穿全表的约定：

- **GPU 地址 = CPU 地址**。NVN 的内存池用的是游戏自己分配的内存，图形层在同一段内存上建零拷贝的 Metal 缓冲。游戏传来的任何缓冲地址，都用内存池的区间表反查成「哪个 `MTLBuffer` + 偏移」（`mtl_addr()`）。
- **先录后放**。命令缓冲的调用只是把记录写进游戏提供的命令内存（格式见 `nvn/nvn_obj.h`），到提交时才由 `nvn/nvn_mtl.m` 回放成 Metal 命令。

## 1. 对象

| NVN 对象 | Metal 实现 | 要点 |
|---|---|---|
| Device | `MTLDevice`（要求统一内存） | `nvnDeviceGetInteger` 返回的设备属性仍按 Tegra X1 的值给游戏（`hle_nvn.c` 的 `g_devinfo`） |
| MemoryPool | 游戏内存上的 `newBufferWithBytesNoCopy` | 苹果芯片的页是 16 KiB，池不保证按它对齐：起点向下、终点向上取整到页边界再建缓冲，相邻池的缓冲允许重叠（`mtl_pool_add()`；做法由 `metal/probe/p_nocopy.m` 验证） |
| 缓冲地址（`NVNbufferAddress`） | （`MTLBuffer`, 偏移） | 按池的区间表反查 |
| Texture | `MTLTexture` | 游戏打包贴图是块线性排布，上传时解排布；格式映射与解排布在 `tex/`。池内存被游戏改写后重新上传，靠 `nvn/nvn_dirty.c` 的写保护脏页跟踪发现。`ReadTexels`：打包贴图直接从池内存解排布读，GPU 写出的贴图从 `MTLTexture` 读回 |
| TextureView | `newTextureViewWithPixelFormat:textureType:levels:slices:` | — |
| Sampler | `MTLSamplerState` | 边框色 Metal 只有三种预设（透明黑、不透明黑、不透明白），按 NVN 给的边框色就近取 |
| TexturePool / SamplerPool | 宿主侧的表：描述符号 → 贴图 / 采样器 | 分离贴图句柄的值就是描述符号 |
| Program | 每个阶段一个 `MTLFunction` | 以着色器控制段的哈希为键，从离线翻译好的 `<键>_b.metallib` 加载；运行时不翻译。翻译链见 README |
| 状态对象（Blend / Color / ChannelMask / Multisample / Polygon / DepthStencil / VertexAttrib / VertexStream） | 渲染管线描述符的字段 + `MTLDepthStencilState` + 编码器状态 | 游戏按值拷贝这些小结构，宿主侧用位域就地打包。渲染管线、深度模板状态按状态哈希缓存 |
| CommandBuffer | 录在游戏命令内存里的记录流 | 提交时回放 |
| Queue | Metal 命令队列 | `SubmitCommands` = 回放并提交 |
| Sync | 提交的栅栏值 | `FenceSync` 记下最近一次提交的栅栏值；`SyncWait` 只在 GPU 真正完成到该值之后放行（`MTLSharedEvent` 的已完成值）。没围栏过的同步对象等待直接返回超时 |
| Event | 游戏内存里的一个 32 位值 | 写值在 GPU 完成之后发生 |
| Window | 游戏自己的交换链贴图 ↔ `CAMetalLayer` | `AcquireTexture` 返回交换链贴图的下标，垂直同步的等待发生在这里；`QueuePresentTexture` 只把出图排进队列立即返回，由出图线程把该贴图按 `WindowSetCrop` 给的裁切区画到 drawable 上 |

## 2. 命令

| NVN 命令 | Metal | 说明 |
|---|---|---|
| SetRenderTargets | 结束当前渲染编码器，开新的渲染通道 | 通道里第一条命令之前的清除并进附件的读入动作（load = Clear） |
| ClearColor / ClearDepthStencil | 同上 | 通道中途的清除、带部分掩码或部分剪裁的清色没有实现，遇到报红 |
| ClearTexture / ClearTextureui | 单独清一张贴图 | 部分形态没有实现，遇到报红 |
| BindProgram 与各状态 Bind* | 记进当前状态，下一次绘制前按需取管线 | — |
| BindUniformBuffer(s) / BindStorageBuffer(s) / BindSeparateTextures / BindSeparateSamplers / BindImages | 按着色器的反射 json 填参数缓冲（argument buffer） | 槽号与着色器里常量段的对应见 README「着色器翻译链」 |
| BindVertexBuffers | 顶点缓冲 = （`MTLBuffer`, 偏移） | — |
| SetViewports / SetScissor / SetDepthRange / Stencil* / SetBlendColor / SetPolygonOffsetClamp | 编码器上对应的设置 | — |
| DrawArrays / DrawElementsInstanced | 绘制 / 带索引绘制 | Metal 没有四边形图元：不带索引的四边形用共享的索引 0,1,2,0,2,3 画成两个三角形；带索引的四边形、8 位索引没有实现，遇到报红 |
| DispatchCompute | 计算编码器，线程组大小取反射 json | 主程序内嵌的 4 个计算着色器用 `nvn/nvn_builtin.h` 里手写的版本 |
| CopyBufferToBuffer / UpdateUniformBuffer | 命令流里按序插入的缓冲拷贝 | `UpdateUniformBuffer` 的数据在录制时就拷进命令内存，因为 NVN 允许调用方录完后改原数据 |
| CopyBufferToTexture / CopyTextureToBuffer / CopyTextureToTexture | 贴图拷贝 | 贴图到贴图的部分形态没有实现，遇到报红 |
| ReportCounter / ResetCounter | 计数器的值在 GPU 完成之后写回 | 「通过采样数」这一类用 Metal 的可见性结果缓冲实现 |
| SignalEvent | 事件写值，GPU 完成之后发生 | — |
| SetTexturePool / SetSamplerPool | 录进命令流，记下当前用的描述符池 | — |

下面这些是空实现（`hle_nvn.c` 的 `g_nops`），调用不报红也不产生效果：

`SetPatchSize`、`MultiDrawArraysIndirectCount`、`MultiDrawElementsIndirectCount`、`DispatchComputeIndirect`、`SetDepthBounds`、`DownsampleTextureView`、`WaitSync`、`SetShaderScratchMemory`、`PushDebugGroupDynamic`、`PopDebugGroup`、`InsertDebugMarkerDynamic`、`WaitEvent`，以及 `nvnWindowFinalize`。

## 3. 着色器绑定

Switch 的着色器通过驱动维护的几个常量段拿到资源，图形层在回放时把同样的内容填进 Metal 的参数缓冲。反射 json 里每个绑定带一个种类（`nvn/nvn_refl.h` 的 `NVN_RB_*`）：

| 种类 | 含义 |
|---|---|
| `CBUF` | uniform 缓冲槽 r |
| `TEX` / `SAMP` / `SAMPT` | 分离贴图槽、分离采样器槽；`SAMPT` 表示采样器取自贴图槽的合并句柄 |
| `IMG` | 可写贴图（图像槽） |
| `TBUF` | 类型化缓冲（缓冲贴图） |
| `SBUFP` | 存储缓冲槽的设备指针 |
| `UBOP` | 计算阶段 uniform 槽的设备指针 |
| `C1` | 着色器自带的常数段 |
| `DRV0` / `DRV2` | 驱动常量段 c[0]（存储缓冲地址表）与 c[2]（贴图、采样器句柄表），由回放层按当前绑定现填 |
| `SUPPORT` | 翻译器自己用的辅助常量 |

没有绑定的贴图槽给一张哑贴图，类型与像素类别取自反射。

## 4. 加一个还没实现的函数

1. 在 `nvn/hle_nvn.c` 里用 `NVN(名字, 返回类型, 参数…)` 宏写实现。宏会按 `nvn/nvn_protos.h` 做签名检查，并把实现登记进槽表；名字不在 `nvn/names.txt` 里启动即中止。
2. 如果是命令：在 `nvn/nvn_obj.h` 里加记录格式，录制写在 `hle_nvn.c`，回放写在 `nvn/nvn_mtl.m`。
3. 在 `tests/` 里加测试，`make -C dm/gfx/tests run`。
