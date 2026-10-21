# NVN 入口、对象、命令与生命周期清点（B0，#11）

English: [nvn-inventory.md](nvn-inventory.md)

这一页清点游戏用到的 NVN 接口面、其后的宿主对象、承载 GPU 状态的命令记录，以及它们之间的生命周期规则，好让第二个
图形后端（Vulkan，#13/#18）按证据搭，而不是按猜。以下全部是**源码层面**的结论：它说的是本仓实现了什么、怎么接的。
它**不是**「游戏在新后端上画对了」的证明，也不是兼容性或可玩性结论。

不涉及任何游戏内容：下面每个输入都是仓内已有文件，每个数字都能用文末「复现」里的命令重算。

## 量了什么，没量什么

| | |
|---|---|
| 量了 | 游戏取址并调用的 NVN 名字（静态证据）、宿主实现了其中哪些、每个实现在哪、对象／命令／生命周期怎么表示。 |
| 没量 | 某个调用点在运行期是否真的走到；宿主 ABI 是否与 Switch 驱动逐位一致；某个 NVN 调用有没有正确的 Vulkan 对应；任何来自游戏的像素结果。 |
| 刻意不含 | 游戏文件、着色器包、转储、以及 `docs/DERIVED.md` 以外的程序内地址。 |

## 复现

```
python3 dm/gfx/tools/nvn_inventory.py                     # 汇总 + 逐条 TSV 到 stdout
python3 dm/gfx/tools/nvn_inventory.py --tsv /tmp/nvn.tsv  # 逐条清单写入文件
python3 dm/gfx/tools/nvn_inventory.py --check             # 一致性闸门（见「已知缺口」）
```

输入均为仓内文件：`dm/gfx/nvn/names.txt`（游戏取址的名字）、`dm/gfx/nvn/nvn_slots.tsv`（`dm/gfx/tools/nvn_slots.py`
从游戏映像算出的静态调用面）、`dm/gfx/nvn/hle_nvn.c`（实现与空实现表 `g_nops`）。

## 入口面

实测（本分支 `nvn_inventory.py --check`）：

| 项 | 数量 | 出处 |
|---|---:|---|
| 游戏经 `nvnBootstrapLoader` 取址的名字 | 523 | `dm/gfx/nvn/names.txt`、`nvn_count.h`（`NVN_NFUNC`） |
| 至少有一个静态调用点的名字 | 216 | `nvn_slots.tsv`（`nuses` > 0） |
| 源码里定义了实现的入口（`hle_nvn.c` 的 `NVN(...)`）；语义与完备性未证明 | 204 | `hle_nvn.c`（其中 203 个有调用点，1 个无原型） |
| 记名的空实现 | 9 | `hle_nvn.c` 的 `g_nops[]`，第 935 行 |
| 报红桩（`nvn_unimpl_entry`） | 310 | 其余全部；每个名字第一次被调到打印一次 `【红】NVN 未实现被调用：<名字>` |

文档漂移说明：`dm/gfx/README.md` 写的是「203 实现 + 13 空实现 + 307 报红桩」。13/307 已与源码不符（当前 9/310）；
`nvn_nops_register()`（`hle_nvn.c:973`）启动时打印的横幅用的就是实测值。根 README 不在本次改动范围，未动；记在这里，
好让以后修文档从实测数字出发。

按名字分组的数量与调用点合计（工具汇总）说明工作量集中在哪：`CommandBuffer` 必需 68 项／196 个调用点，`Texture`
36／128，`Device` 13／26，`Window` 10／10，`Queue` 17／19，`Program` 3／21。录制面在 `CommandBuffer` 组，格式、
排布与上传在 `Texture` 组。

### 有调用点却仍是报红桩的接口

这是「游戏会调」与「宿主实现了」唯一不一致的地方：

| 入口 | 静态调用点 | 当前行为 | 风险 |
|---|---:|---|---|
| `nvnCommandBufferSetPatchSize` | 1 | 报红桩 | 一旦走到，细分面片尺寸被丢掉；绘制状态错，但不响 |
| `nvnCommandBufferMultiDrawArraysIndirectCount` | 2 | 报红桩 | 一旦走到，这些绘制被静默跳过 |
| `nvnCommandBufferMultiDrawElementsIndirectCount` | 2 | 报红桩 | 同上，索引版 |
| `nvnCommandBufferDownsampleTextureView` | 2 | 报红桩 | 少一趟降采样，错的是像素而不是崩溃 |

已知：`main` 里确实存在这些调用点（证据：`nvn_slots.tsv`，由 `nvn_slots.py` 产出）。**未知**：运行期这些点是否执行——
本仓没有任何材料能确定这一点，要判定得录一次带插桩的运行，超出本任务范围。在此之前它们的准确说法是「静态调用点存在、
实现缺失」，属清点缺口，不是关于游戏行为的结论。

## 对象与生命周期

每个 NVN 对象都就地存放在游戏给它分配的内存里；`nvn_obj.h` 定死布局，并静态断言每个都不超过 NVN 预留大小。所以录进
命令的东西不能持有「之后会改、会死」的对象指针：录制时宿主取一份不可变**副本**（`snap_tex` `hle_nvn.c:283`、
`snap_prog` `:293`），按对象序号索引；对象 Finalize 或换序号后，副本再过 `SNAP_KEEP`（8，`:253`）次提交才回收。

| 对象 | 创建／初始化 | 初始化后可改 | 录制形式 | 销毁 | 回放层表示 |
|---|---|---|---|---|---|
| `NVNdevice` | `nvnDeviceInitialize` `hle_nvn.c:122` | 标志、窗口原点、深度模式 | — | `nvnDeviceFinalize` `:128` | 全局 `g_dev` |
| `NVNmemoryPool` | `nvnMemoryPoolInitialize` `:177` | — | — | `nvnMemoryPoolFinalize` `:185` | 零拷贝 `MTLBuffer`，GPU 地址 = CPU 地址 |
| `NVNtexturePool` / `NVNsamplerPool` | `:191` / `:196` | 注册项 | `OP_SET_TEX_POOL` / `OP_SET_SAMP_POOL` | `:194` / `:199` | 槽表 `mtl_reg_tex` / `mtl_reg_samp` |
| `NVNtexture` | `nvnTextureInitialize` `:314` | —（builder 定死） | 每次拷贝／清除／绘制都存副本 | `nvnTextureFinalize` `:326` | 每个序号缓存 `MTLTexture`（`tex_mtl` `nvn_mtl.m:1384`），可选块线性 CPU 视图 |
| `NVNsampler` | `nvnSamplerInitialize` `:399` | 仅 builder | 绑定句柄 | `:402` | 打包的 `samp_par` |
| `NVNprogram` | `nvnProgramInitialize` `:406` | `nvnProgramSetShaders` `:410` | `OP_BIND_PROGRAM` | `:409` | `NvShader`：`MTLFunction` + 反射数组 |
| `NVNsync` | `nvnSyncInitialize` `:420` | `nvnQueueFenceSync` `:424` | 不录 | `:421` | 提交栅栏值；`nvnSyncWait` `:427` 真等 GPU |
| `NVNevent` | `nvnEventInitialize` `:435` | GPU／CPU 信号 `:443` | `OP_SIGNAL_EVENT` | `:440` | 值只在 Metal 命令缓冲完成后写 |
| `NVNqueue` | `nvnQueueInitialize` `:465` | — | 提交 handle | `:469` | 单条 Metal 队列；`nvnQueueFinish` `:474` 排空 |
| `NVNcommandBuffer` / handle | `nvnCommandBufferInitialize` `:481`，内存 `:538` | 录制状态 | `OP_*`（见下） | `nvnCommandBufferFinalize` `:484` | 回放遍历记录流（`mtl_exec` `nvn_mtl.m:5463`） |
| `NVNwindow` | `nvnWindowInitialize` `:988` | 裁切 `:998`、取图 `:1013` | 出图 `:1266` | `nvnWindowFinalize`（空实现，`g_nops`） | 交换链贴图 + 出图队列 + 节拍器 |

新后端必须保留的失败与回滚语义：非法拷贝行距在碰内存之前就被拒（`nvn_copy_pitch.h:8`），非法裁切拒绝而不是拉伸
（`t_present_crop.m:61-66`），资源寿命靠上面的序号／副本机制而不是「一般不会出事」。

## 命令

命令流是写进游戏给的命令内存里的 `op_hdr {op,size}` 记录字节流（`nvn_obj.h:88`）；`nvnCommandBufferEndRecording`
返回不透明 handle（`hle_nvn.c:564`）。共 **40 个录制操作**；本页与工具里的操作名单是**人工转写**，工具启动时解析
`nvn_obj.h` 的 op 枚举逐名核对，不一致即报错（不靠这份手写名单自证）：

| 类别 | 操作 | CPU 可见效果 |
|---|---|---|
| 控制 | `OP_END OP_JUMP OP_BEGIN OP_CALL` | 无（嵌套 handle 重跑） |
| 同步／计数器 | `OP_SIGNAL_EVENT OP_REPORT_COUNTER OP_RESET_COUNTER` | 仅 GPU 完成后 |
| 内存 | `OP_COPY_B2B OP_UPDATE_UB` | 仅 GPU 完成后 |
| 渲染目标 | `OP_SET_RT OP_CLEAR_COLOR OP_CLEAR_DS OP_CLEAR_TEX` | 回放前无 |
| 固定状态 | `OP_VIEWPORTS OP_SCISSOR OP_DEPTH_RANGE OP_STENCIL OP_POLY_OFFSET OP_BLEND_COLOR OP_BARRIER` | 无 |
| 绑定 | `OP_BIND_PROGRAM OP_BIND_STATE OP_BIND_VATTR OP_BIND_VSTREAM OP_BIND_VBUF OP_BIND_UBO OP_BIND_SSBO OP_BIND_TEX OP_BIND_SAMP OP_BIND_IMG OP_SET_TEX_POOL OP_SET_SAMP_POOL` | 无 |
| 绘制／派发 | `OP_DRAW OP_DRAW_IDX OP_DISPATCH` | 无 |
| 拷贝 | `OP_COPY_B2T OP_COPY_T2B OP_COPY_T2T OP_COPY_STRIDE OP_COPY_IMAGE_STRIDE` | 无 |

两条对 #18 重要的性质：(a) 录制出来的不是 Metal/Vulkan 对象图，而是扁平记录流，所以 Vulkan 后端可以回放同一条流，
不用改接口层；(b) CPU 能观察到的一切（事件值、计数器、缓冲内容）都推迟到 GPU 完成之后——验收场景里的读回与事件期望
正是靠这条。

## 呈现

`nvnWindowAcquireTexture`（`hle_nvn.c:1013`）给出下一张交换链序号和一个同步对象；`nvnQueuePresentTexture`（`:1266`）
把当帧裁切副本入队，并把交换链贴图采样到 drawable。裁切在入队时就拷好，之后改 window 不会影响已入队的帧
（`t_present_crop.m:67-80`）。采样走 `present_src`（`nvn_mtl.m:5903`）：sRGB 交换链建一个非 sRGB 视图，读到的是存的字节。
节拍是纯函数 `nvn_pace`/`nvn_pace_hz`（`nvn_pace.h`），所以出图时序策略不需要设备就能测。

## 着色器

离线链（不在运行期翻译，见 `dm/gfx/README.md`）：Maxwell 机器码 → `dmsh_xlate`（C#，Ryujinx 翻译库，
`dm/gfx/tools/dmsh_xlate/Program.cs`）→ SPIR-V → spirv-cross → MSL → metallib。着色器键取控制段 0x880 字节 sha256 的
前 16 个十六进制位；反射由 `nvn_refl_from_json`（`nvn_refl.m:22`）每个 program 转一次，成定长数组（`nvn_refl.h`），
机器码里读不出的事实放在 `nvn/shader_facts.tsv`。这条契约的接口层一侧现在由 `t_accept_shader.m` 覆盖（见下）；翻译器
一侧属 #12 范围。

## 本次交付的验收场景

`dm/gfx/tests/t_accept_scene.m` 与 `dm/gfx/tests/t_accept_shader.m` 在当前 Metal 后端上跑，只用自己生成的数据，每个
期望值都由公式算出。它们是**现在就能执行的**，已接进 `make -C dm/gfx/tests run`。

| 场景 | 检查 | 期望输出 |
|---|---|---|
| 资源／状态 | 8×4 RGBA8 线性贴图，从行距 64（紧排 32）的缓冲上传 | 32 个纹素全等于 `PX(x,y)`，即行尾填充被跳过 |
| 资源／状态 | 贴图→缓冲回写，行距 48 | 32 个纹素等于 `PX(x,y)`，每行末尾 16 字节未被碰 |
| 提交 | 命令缓冲里发事件 | 提交前 0；围栏等待返回 1 后为 42 |
| 生命周期 | Finalize 后在同一池偏移重建并清色 | 32 个纹素等于清色值（没有旧内容） |
| 呈现 | 向同一 8×4 目标连续两次整帧呈现；第二帧换源贴图、换清色值 | 第一帧：32 像素等于 `{0,255,128,64}`（RGBA8 下清 0/1/0.5/0.25）；第二帧：32 个全等于 `{255,64,0,128}`（清 1/0.25/0/0.5），且没有任何像素仍是第一帧的值 |
| 着色器界面 | 自写的 B 路线反射 json，10 种绑定 | kind/reg/arg_id/哑贴图类型按断言；c1 字节 `0x0A 0x0B`；5 个负输入被拒 |

本分支实测（macOS 27.0.1，Apple M4）：`t_accept_scene` 26/26，`t_accept_shader` 28/28，均退出 0。该场景销毁自己创建的
贴图、事件、同步对象、命令缓冲与队列之后，才销毁其内存所属的池，最后销毁设备；没有刻意留到进程退出的对象。

这些场景是**宿主 Metal 夹具，不是 Vulkan 或 Android 验收结果**。它们跑在 macOS 的 Metal 后端
（`dm/gfx/nvn/nvn_mtl.m`）上；本基线提交里根本没有 `dm/android` 树，所以这里没有任何一句涉及 Android 行为、Vulkan
验证层或 Adreno 目标，也不主张 Android 兼容性。这些场景能确立的是：NVN 入口语义与期望数值在宿主上定义清楚并能被满足。
把它们（同样的公式、同样的输入）搬到 Vulkan 后端属 #13（资源）与 #18（提交／呈现），并且需要 A0 基线（#2）。

## 拟议测试（本次刻意不实现）

| 拟议测试 | 现在为何不能执行 | 后续 |
|---|---|---|
| 在 Android/Vulkan 后端跑同样三段场景，从交换链读回像素 | `origin/main` 没有公开的 Vulkan 后端；需要 A0 基线验收 | #13、#18 |
| 翻译后着色器的数值对照（SPIR-V/MSL 输出 vs 参考值） | 需要着色器包与翻译器输出；两者都由游戏派生，不能进仓 | #12 |
| 多帧呈现：resize、暂停恢复、Surface 销毁重建 | 需要 Android App 外壳与真机 | #18（配合 #14） |
| 带插桩的运行，判定 4 个报红桩入口是否被走到 | 需要录一次真实游戏运行；本任务禁止 | #13（绘制状态）、#18（呈现） |

## 已知缺口（按优先级）

1. **P1——4 个有静态调用点的入口仍是报红桩**（见上表）。要么实现，要么证明走不到。
2. **P1——文档漂移**：根 README 的实现／空实现／桩计数为 203/13/307，实测 204/9/310。
3. **P1——公开树里没有 Vulkan 后端**：上面任何场景都还没有 Vulkan 对应物；#13/#18 依赖 A0（#2）发布基线。
4. **P2——反射上限是定死的**（`NVN_REFL_MAXB` 64 个绑定、`NVN_REFL_MAXV` 32 个顶点输入、c1 4 KiB）；超限会被明确拒绝，
   但 Vulkan 描述符布局可能需要不同上限。在 #12 内重估。
5. **P2——单队列假设**（`hle_nvn.c:938-946` 的 `g_nops` 论证）：WaitSync/WaitEvent 空跑只在一切都在一条队列上时成立。
   有第二条队列的 Vulkan 后端必须把它们改成真等。这是给 #18 的设计约束，今天不是 bug。
