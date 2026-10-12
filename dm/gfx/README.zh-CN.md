# dm/gfx — 图形层（NVN → Metal）

English: [README.md](README.md)

游戏的 Switch 版用 NVN（Switch 的原生图形接口）画图。图形层在苹果的 Metal 上把这套接口重新实现一遍：游戏的每一次 NVN 调用落到这里，录成命令，提交时回放成 Metal 命令。游戏的着色器不在运行时翻译，而是在电脑上一次性离线翻译成 Metal 着色器库（电脑上没有苹果的 Metal 编译器时，最后的编译一步改在手机上做，见下文）。

整个项目的构建步骤见仓库根目录的 `README.zh-CN.md` 与 `setup.sh`，依赖见 `docs/DEPENDENCIES.zh-CN.md`；这里只讲图形层自己的事。

## 怎么工作

### 接管 NVN 接口

- 游戏主程序只导入一个 NVN 符号 `nvnBootstrapLoader`，其余 NVN 函数都通过它按名字取地址。宿主强制接管这个导入（`nvn/hle_nvn.c`），原版图形驱动一条指令都不执行。
- 游戏会取址的名字共 523 个（`nvn/names.txt`）。每个名字对应一个跳板（`nvn/nvn_thunks.S`，生成文件）：先给该函数的调用计数加一，再跳到实现。
- 其中游戏主程序里有调用点的 216 个（`nvn/nvn_slots.tsv`）都有实现：203 个按 NVN 语义实现，13 个是空实现（`hle_nvn.c` 的 `g_nops`）。其余 307 个接报红桩，被调到时日志里打印 `【红】NVN 未实现被调用：名字`。
- 每个实现都按 `nvn/nvn_protos.h`（生成文件）做编译期签名检查。苹果 arm64 与 Switch 的调用约定在小于 32 位的整型参数上不同，所以 `NVNboolean` 参数在宿主侧一律按 `uint32_t` 接、只看低 8 位。

### 两层：录制与回放

- **接口层 `nvn/hle_nvn.c`**：宿主侧的对象就地存在游戏给 NVN 对象分配的内存里，布局见 `nvn/nvn_obj.h`，每个都有静态断言保证不超过 NVN 的保留大小。命令按 `nvn_obj.h` 的记录格式录进游戏提供的命令内存。
- **回放层 `nvn/nvn_mtl.m`**：提交时把记录流回放成 Metal 命令。
  - 内存池建成零拷贝的 `MTLBuffer`，GPU 地址等于 CPU 地址，所以只支持统一内存设备。
  - 「GPU 完成」的可见效果（同步对象就绪、事件写值、计数器）只在 Metal 命令缓冲真正完成之后发生。
  - 管线状态、深度模板状态按状态哈希缓存；启动时按预热清单在后台预建着色器与管线。
  - 游戏打包贴图是 Tegra 块线性排布，上传前解排布（见 `tex/README.zh-CN.md`）。
  - 出图：游戏的交换链贴图画到 `CAMetalLayer`，出图节拍由 `nvn/nvn_pace.h` 的节拍器排。

各 NVN 对象与命令落到 Metal 的哪个对象，见 [nvn_metal_mapping.zh-CN.md](nvn_metal_mapping.zh-CN.md)。

### 着色器翻译链

游戏的着色器是 Tegra X1 的 GPU 机器码（Maxwell 指令集）。从这种机器码到 Metal 着色语言的翻译不在手机上做，全部离线完成：

```
Switch 原版着色器包
  → tools/dmsh_xlate        用 Ryujinx 的着色器翻译库出 SPIR-V（C# 工具）
  → spirv-cross             SPIR-V 转 Metal 着色语言（MSL）
  → xcrun metal / metallib  编成 Metal 着色器库
```

没有苹果的 Metal 编译器时（Windows、Linux、没装 Xcode 的 Mac），最后一步做不了。`tools/nometal_run.py` 让同一条链照常跑完前两步，把翻译出的 MSL 源码原样放进库文件；这样做出的包在手机上把着色器编译一次，系统会缓存编好的结果。这条路由 `dm/tools/d2rm_pack.py` 使用，见仓库根目录的 `docs/NO_XCODE.zh-CN.md`。

整条链由 `tools/dmsh_build.py` 驱动，`setup.sh` 会替你跑。要点：

- **键**：每个着色器以它的控制段（0x880 字节）的 sha256 前 16 位十六进制为键。运行时游戏把控制段交给 `nvnProgramSetShaders`，回放层算出同一个键去找库。
- **产物**：`<键>_b.metallib`（着色器库）、`<键>_b.json`（回放用的反射：各绑定的种类、槽号、在参数缓冲里的序号等）、`<键>_b.metal`（MSL 源）。部分片元着色器另有 `<键>_bf.metallib`，是「可读 tile」版本，供通道合并用（见 `tools/dmsh_fetch.py` 文件头）。目录里的 `_b_build.json` 记本次每个键的成败。
- **`nvn/shader_map.tsv`**：着色器映射表，每个着色器变体一行。运行时只用其中「键 → 阶段（顶点 / 片元 / 计算）」。
- **`nvn/shader_facts.tsv`**：翻译查询的作答表。翻译器需要知道一些机器码里读不出的事实（贴图的维度类型、图像格式、顶点属性与片元输出的分量类型、计算着色器的线程组大小与共享内存字节数），这张表按键给出答案。表随仓库，出着色器库只读这张表；表由 `tools/dmsh_facts.py` 维护，文件头有说明。
- **翻译链版本与 `b-<V>/` 目录**：版本 V 是一串 12 位十六进制，由翻译链涉及的源文件内容、翻译选项和工具指纹（`tools/dmsh_chain.lock`）一起哈希得出，`tools/dmsh_chain.py` 负责计算。着色器库写进 `<输出目录>/b-<V>/`，每个反射 json 里也记着 V。宿主编译时从 `nvn/dmsh_chain.h` 取 V，只从 `b-<V>/` 加载，并逐个核对 json 里的版本；对不上就报红、不加载。所以改了链上任何一个文件、或换了工具，都要重新出库并重编宿主。
  - `dmsh_chain.py`：打印 V。
  - `dmsh_chain.py --check-tools`：核对本机工具与锁文件一致。
  - `dmsh_chain.py --update-lock`：按本机工具重写锁文件。
  - `dmsh_chain.py --header`：重写 `nvn/dmsh_chain.h`。
  - 本机的 Xcode 或 spirv-cross 版本与仓库登记的不同是正常的，`setup.sh` 会自动按本机工具重新登记。
- **`nvn/c1_full.tsv`**：着色器自带常数段（c1）的下标清单，由 `tools/c1_guard.py` 扫描翻译出的 MSL 生成，列出「运行时下标没能证明有界」的着色器键。首行记着它对应的 `b-<V>`。回放层对清单里的着色器给 c1 单独分配整块 64 KiB 缓冲；清单缺失或版本对不上时报红，并对全部着色器都这样保守处理。
- **`nvn/pso_manifest.tsv`**：预热种子清单，记启动后最先用到的着色器与管线，行序就是预建顺序，不要排序。
- **程序本体内嵌的着色器**：主程序里另有 4 个计算着色器不在着色器包里，按反汇编手写成 MSL，在 `nvn/nvn_builtin.h`。

翻译时遵循的绑定约定（`tools/maxwell_refs.py` 文件头）：

- uniform 缓冲 r 在机器码里是常量段 `c[3 + r]`；
- 分离贴图 r 的句柄在 `c[2][0x168 + 8r]`，分离采样器 r 的句柄在 `c[2][0x568 + 8r]`；
- 存储缓冲 r 的地址在 `c[0][基址 + 16r]`，基址：顶点 0x110、计算 0x310、片元 0x510；
- 窗口原点在左上、深度范围 0 到 1，与 Metal 一致，翻译时不翻转。

## 目录结构

| 位置 | 内容 |
|---|---|
| `nvn/hle_nvn.c` | 接口层：接管 NVN、对象实现、命令录制、出图节拍 |
| `nvn/nvn_mtl.m`、`nvn/nvn_mtl.h` | 回放层：Metal 设备、内存池、贴图、着色器加载、管线、绘制、出图 |
| `nvn/nvn_obj.h` | 宿主侧对象布局与命令记录格式，两层共用 |
| `nvn/nvn_entry.S`、`nvn/nvn_thunks.S` | 未实现桩与计时包装的入口、逐函数跳板 |
| `nvn/nvn_tex.c`、`nvn/nvn_tex.h` | 块线性贴图的存储尺寸、对齐、各级偏移（移植自 deko3d） |
| `nvn/nvn_dirty.c`、`nvn/nvn_dirty.h` | 用写保护跟踪游戏内存的脏页，判断贴图内容是否变了需要重传 |
| `nvn/nvn_refl.m`、`nvn/nvn_refl.h` | 着色器反射 json 转成定长 C 数组，绘制时只遍历数组 |
| `nvn/nvn_hmap.h`、`nvn/nvn_cover.h`、`nvn/nvn_pace.h`、`nvn/nvn_pso_skey.h` | 哈希表、全覆盖绘制的几何证明、出图节拍器、管线清单键 |
| `nvn/nvn_builtin.h` | 主程序内嵌的 4 个计算着色器的手写 MSL |
| `nvn/include/nvn.h` | NVN 接口声明（第三方，许可证见文件头） |
| `nvn/*.tsv`、`nvn/names.txt`、`nvn/dmsh_chain.h` | 上文讲的各张表与翻译链版本头 |
| `tex/` | 贴图格式映射、解排布、BC 解压，见 [tex/README.zh-CN.md](tex/README.zh-CN.md) |
| `metal/` | 独立的最小 Metal 离屏示例，不编进宿主，见 [metal/README.zh-CN.md](metal/README.zh-CN.md) |
| `tools/` | 离线工具，见下 |
| `tests/` | 单元测试 |
| `hle_vi.c`、`hle_nv.c` | 显示服务与图形驱动初始化的胶水层：返回宿主静态对象或空操作 |

`tools/` 里的脚本，用法都写在各自文件头：

| 脚本 | 用途 |
|---|---|
| `dmsh_xlate/` | 着色器翻译工具（C#），引用 `dm/third_party/ryujinx` 里的翻译库 |
| `dmsh_build.py` | 驱动整条翻译链出着色器库；设环境变量 `IOSOUT=目录` 时同时出 iOS 版 |
| `dmsh_chain.py`、`dmsh_chain.lock` | 翻译链版本与工具指纹 |
| `dmsh_facts.py` | 作答表 `shader_facts.tsv` 的生成与核对 |
| `dmsh_fetch.py` | 生成片元着色器的「可读 tile」版本 |
| `c1_guard.py` | 生成 `c1_full.tsv` |
| `dmsh_dryrun.py` | 全库干转，只量「能不能转」 |
| `shader_map.py`、`maxwell_refs.py` | 生成着色器映射表；从机器码读资源引用 |
| `nvn_slots.py`、`gen_protos.py` | 生成 `nvn_slots.tsv` / `names.txt`、`nvn_protos.h` / 跳板 |
| `tbdr_budget.py`、`tbdr_load_dead.py`、`tbdr_store_dead.py`、`tbdr_pass_merge.py` | 读 `DM_NVN_PASSDUMP` 的日志，分析每帧各渲染通道附件的读入 / 写回量 |
| `passgpu_top.py` | 读 `DM_NVN_PASSGPU` 的输出，按通道列 GPU 时间 |
| `nometal_run.py` | 在没有苹果 Metal 编译器的机器上跑这条翻译链：着色器以源码进库，到手机上再编 |

## 构建

图形层没有单独的库。`dm/host/Makefile` 按通配把 `dm/gfx/*.c`、`dm/gfx/nvn/` 与 `dm/gfx/tex/` 下的源文件编进宿主 `dmhost`；iOS App 由 `dm/ios/run.sh build` 构建，同时把映射表、`pso_manifest.tsv`、`c1_full.tsv` 和 `b-<V>/` 着色器库拷进 App。`tests/` 与 `metal/` 不进宿主。

着色器库由 `setup.sh` 生成：

- Mac 版在 `$DM_ROOT/build/cache/metal_shader_cache/b-<V>/`；
- iOS 版在 `$DM_ROOT/build/ios/dmsh_b/b-<V>/`。

改了翻译链上的文件后，手动重出的顺序与 `setup.sh` 里相同：`dmsh_chain.py --header` → `dmsh_build.py` → `c1_guard.py` → 重编宿主。`$DM_ROOT` 缺省是 `~/dm-work`。

## 测试

```sh
make -C dm/gfx/tests run        # 图形层单测
make -C dm/gfx/tex/tests run    # 贴图子模块单测；真数据项要 Switch 与 PC 两版的贴图样本，见 tex/README.zh-CN.md
make -C dm/gfx/metal run        # 离屏示例自测，见 metal/README.zh-CN.md
```

`dm/gfx/tests` 的测试直接编入生产代码，需要本机有 Metal 设备；其中几项在 `MTL_DEBUG_LAYER=1`（Metal 校验层）下运行。多数测试不读工作区；例外是 `t_refl` 与 `t_rt_range_ro` 要读 `$DM_ROOT/build/cache/metal_shader_cache` 下翻译好的着色器反射 json，`test_tbdr_budget.py` 会在 `$DM_ROOT/run/gfx` 下建临时目录。主要内容：

| 测试 | 核什么 |
|---|---|
| `t_nvn` | 接口层：照游戏的用法从 `nvnBootstrapLoader` 取址，一路走到出图 |
| `t_builtin` | 4 个内嵌计算着色器 |
| `t_refl`、`t_hmap`、`t_cover`、`t_pace`、`t_pso_skey`、`t_dirty_slots` | 反射解析、哈希表、全覆盖证明、节拍器、管线清单键、脏页跟踪 |
| `t_deswz_gpu` | GPU 解排布 |
| `t_present_crop` | 出图裁切 |
| `t_mip_extent` | 渲染到非第 0 级 mip 时的有效尺寸 |
| `t_fuse_check`、`t_reorder_chain`、`t_defer_chain`、`t_zskip`、`t_xv`、`t_ub`、`t_pfv`、`t_rt_range`、`t_rt_range_shape`、`t_rt_range_ro`、`t_rt_depth_ro` | 直接编入回放层 `nvn_mtl.m`，核回放层内部的各项优化与诊断逻辑 |
| `test_tbdr_budget.py` | `tools/tbdr_budget.py` 的解析 |

## 常用运行时开关

都是环境变量。iOS App 启动时自己设好 `DM_NVN_SHMAP` 与 `DM_MSL_DIR`。

资源位置：

| 开关 | 作用 |
|---|---|
| `DM_NVN_SHMAP=路径` | 着色器映射表。缺省是可执行文件目录下的 `../gfx/nvn/shader_map.tsv`；`c1_full.tsv` 与 `pso_manifest.tsv` 取同目录 |
| `DM_MSL_DIR=目录` | 着色器库的上级目录，宿主从其下的 `b-<V>/` 加载。在 Mac 上跑宿主时设为 `$DM_ROOT/build/cache/metal_shader_cache` |
| `DM_PSO_MANIFEST=路径` 或 `off` | 换一份预热清单，或关掉预热 |
| `DM_PSO_RECORD=路径` | 把本次运行用到的着色器与管线追加记到文件，用来重录预热清单 |

日志与抓取：

| 开关 | 作用 |
|---|---|
| `DM_NVN_LOG=1` | 记录 NVN 对象的创建 |
| `DM_NVN_FRAMES=N` | 前 N 帧逐帧打印调用表（缺省 3） |
| `DM_NVN_TSV=路径` | 每帧调用表与计时写成 TSV；再加 `DM_NVN_TSV_CALLS=0` 只写计时行 |
| `DM_SLOWFRAME=1` | 只把掉帧那几帧的计时分项写进日志；归因脚本是 `dm/ios/tools/slowframe.py` |
| `DM_NVN_SHOT=帧号[,帧号…]:路径` | 在指定帧把交换链贴图存成图片，路径里的 `%d` 换成帧号 |
| `DM_NVN_SHOT_OUT=宽x高` | 上面的截图改截放大到该分辨率后的画面 |
| `DM_NVN_TEXDUMP=帧号:路径` | 把该帧用过的颜色渲染目标逐张存图 |
| `DM_NVN_PASSDUMP=帧号` 或 `起-止` | 把这些帧每个渲染通道的附件、采样、出图逐条写到标准错误，供 `tools/tbdr_*.py` 分析；再设 `DM_NVN_PASSDUMP_ALL` 则每次绘制都记 |
| `DM_NVN_PASSGPU=文件` | 逐通道 GPU 时间（需要设备支持阶段边界的计数采样） |
| `DM_NVN_PROF=1` | 给已实现的 NVN 函数套计时包装，统计录制期各函数耗时 |

对照与测试：

| 开关 | 作用 |
|---|---|
| `DM_NVN_TBDR=0` | 关掉回放层全部通道优化，走「附件全挂、全读入、全写回」的最朴素路径，用来做等价对照 |
| `DM_NVN_UNWRITTEN_DC=0` | 关掉「本通道没写的附件不写回」这一项 |
| `DM_NVN_FUSE_CHECK=1` | 通道合并的同帧核对，逐次把比对结果记进日志 |
| `DM_TEST_NO_BC=1` | 在任何机器上强制走「显卡不支持 BC 压缩贴图」的解压路径 |
| `DM_FPS=30` 或 `60` | 钉住目标帧数 |
| `DM_PRESENT_HZ=N` | 出图节拍的频率（缺省 60）；0 表示不限帧，只用于性能剖析 |

## 已知限制

下面这些路径没有实现，走到会在日志里报红（`nvn/nvn_mtl.m` 里的 `RED_ONCE`）：

- 渲染通道中途的清除、带部分掩码或部分剪裁的清色；
- 带索引的四边形图元、8 位索引；
- 部分形态的贴图到贴图拷贝与清贴图。

`MultiDraw*IndirectCount`、`DispatchComputeIndirect`、`SetDepthBounds`、`DownsampleTextureView` 等列在 `g_nops` 里的函数是空实现，调用不报红也不生效。
