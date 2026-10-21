# Android 移植 — 小米 15 Ultra / 骁龙 8 Elite

English: [README.md](README.md)

## 当前可玩分支（2026-10-21）

开发版已经在目标手机启动真实游戏、进入营地并正常存盘退出，显示以 Turnip 为基线。战斗、技能、拾取及背包／储物箱拖动已有用户验证；完整触屏等价、广泛生命周期／设备覆盖、持续稳定 60 帧与节能效果仍待验收。当前优先级与限制见[项目首页](../../docs/android/README.zh-CN.md#当前状态)及总任务板。

可玩实现请使用 `main`；默认 `main` 仍是较早的实现，PR 的整体审查尚未完成。当前 APK 构建入口为 `tools/build.py --game-run`，需要对应参考工具链及本地资源。Android 数据 ZIP 导入与驱动选择见[当前使用说明](../../README.zh-CN.md#体验开发版)。

**以下为历史工具说明：** 后续章节保留早期独立运行时、自有样例和合成诊断里程碑。其计数、阻塞及旧设备结果只对应各自检查点，不代表当前游戏前端状态。其中 `app/build.sh` 构建合成诊断包，不构建可玩 APK；下文 `paths.sh` 工作区约定属于这些工具。归档契约任务不因参考运行时首跑就视为已证明，严格验证及失败边界继续保留。

## 存储与游戏文件

在 worktree 中请显式将 `DM_ROOT` 设为外置磁盘工作区。下文 `../d2r-work` 相对路径示例假定仓库与工作区并列；覆盖后改用自己的 `$DM_ROOT`。

`paths.sh` 默认使用仓库旁的 `d2r-work` 作为工作区，可设置 `DM_ROOT` 将其放到其他磁盘。其下 `tools/android` 放 NDK 和工具压缩包，`build` 放构建产物，`tmp` 放编译／测试临时文件。

使用自己的 **Switch 版 1.0.34.0，含「术士君临」资料片**，对应仓库路径里的 **3.3.94012**：

```text
$DM_ROOT/data/switch/3.3.94012/Update/
├── ExeFS/
│   ├── main
│   ├── sdk
│   └── subsdk0
└── RomFS/
    └── data/
        ├── build_manifest.psv
        └── 原样保留导出的全部 .data 文件
```

先有 ExeFS 的三个文件即可开始验证程序解包/改写，后续着色器、资源、游戏流程需要完整 RomFS。已经导出的 ExeFS / RomFS 无需提供密钥。文件只保留本地，不进入 Git。运行 `./dm/android/check_game_files.sh --exefs-only` 检查程序，去掉选项则同时检查清单和 18 个必需运行时档案。检查器沿用上游 `setup.sh` 的三个完整程序 SHA-256；资源只查存在且非空，不查 RomFS 摘要。也可把其他 Update 目录作为最后一个参数传入。上游版本/导出说明见 [GAME_FILES.zh-CN.md](../../docs/GAME_FILES.zh-CN.md)。

## 无需游戏的宿主入口与断网依赖准备

在仓库根目录运行。ARM64 开发机需有 CMake 3.22+、C 编译器和 Capstone 5：

```sh
# 替换为自己的外置磁盘工作区；build/cache/tmp 不放进 checkout。
export DM_ROOT=/path/to/external-disk/d2r-work
./dm/android/test.sh -DDM_ANDROID_VULKAN=OFF
```

这会配置、构建并在 ASAN／UBSAN 下执行自有宿主 CTest 样例，无需游戏文件或手机。关闭 Vulkan 会省略 GPU 检查。缺少 Capstone 时可另传 `-DDM_ANDROID_REWRITE_SCAN=OFF`，省略依赖它的改写审计及测试，但不能算这些检查通过。这些是开发子集，不是完整发布验收。

首次配置会下载 `dependencies.cmake` 声明并固定摘要的 LZ4 1.10.0／Mbed TLS 3.6.4 源码。断网构建前，先取得并验证完全相同的归档，在 Git 外解压，再向**每次** build/test 调用传入源码目录：

```sh
./dm/android/test.sh -DDM_ANDROID_VULKAN=OFF \
  -DFETCHCONTENT_SOURCE_DIR_DM_LZ4="$DM_ROOT/tools/src/lz4-1.10.0" \
  -DFETCHCONTENT_SOURCE_DIR_DM_MBEDTLS="$DM_ROOT/tools/src/mbedtls-3.6.4"
```

上述是预先解压目录的示例，脚本不会自动安装它们。CMake 源码覆盖会绕过归档下载／摘要核验，因此使用前须自行按 `dependencies.cmake` 验证归档。NDK、SDK、编译器及可选 GPU 工具也须在断网前安装。此流程不需要游戏程序、SDK 模块、密钥或存档。

`bash dm/android/app/build.sh` 构建[合成诊断 APK](app/README.zh-CN.md)，需 Vulkan／glslc、NDK、Android SDK platform/build-tools 35、JDK/keytool 与 zip。它启用 Vulkan，宿主缓存缺失时可能运行宿主样例测试，导出自有夹具并用本地调试密钥签名，不自动安装或启动 APK。输出为 `$DM_ROOT/build/android/apk-diagnostic/d2r-diagnostic-debug.apk`，不是可玩的游戏构建。

## 构建与测试

安装 CMake 3.22+ 和 [Android NDK](https://developer.android.com/ndk/downloads)（建议 r28+）。工具最低面向 Android API 29、`arm64-v8a`，ELF 段按 16 KB 对齐。运行时通过 `sysconf` 读取页大小，遵循 Android 的[页面大小适配说明](https://developer.android.com/guide/practices/page-sizes)。

```sh
./dm/android/build.sh
```

默认输出到 `../d2r-work/build/android/arm64-v8a`，可通过 `DM_ROOT` 或 `DM_ANDROID_BUILD_DIR` 修改。脚本会找到 `../d2r-work/tools/android/android-ndk-r29`，其他 NDK 路径请设置 `ANDROID_NDK_HOME` 或 `ANDROID_NDK_ROOT`。现有 `setup.sh` 生成的是 Apple 平台产物，不是 Android 构建入口。

首次配置将固定版本 LZ4 1.10.0、Mbed TLS 3.6.4 下载到构建目录并校验压缩包 SHA-256。只编译 LZ4 和 SHA-256 / 内存清零相关源码，BSD-2-Clause 与 Apache-2.0 许可文本随解压源码保留。

默认 GPU 构建还需要开发机的 `glslc` 编译器和 Vulkan loader/头文件；Android 使用 NDK 头文件与系统 `libvulkan.so`。本机使用原有的 Homebrew Vulkan SDK/MoltenVK，新增着色器和构建产物仍放 T2。没有 Vulkan SDK 的开发机可用 `./dm/android/test.sh -DDM_ANDROID_VULKAN=OFF` 只测 CPU；传入 `-DDM_ANDROID_VULKAN=ON` 恢复 GPU 检查。

| 模块 | 已实现／测试的行为 |
|---|---|
| `nso.c`、`nso_file.c` | NSO0 解析、有界 LZ4 解压、已提供段摘要的 SHA-256 校验、间隙/BSS 清零；拒绝截断、重叠、超限输入（文件/映像上限 512 MiB） |
| `dm_nso_inspect` | 读取模块并报告布局/构建号/摘要状态；不执行、不导出游戏内容 |
| `module.c`、`module_map.c` | 有界 MOD0/ELF64 表、SysV 符号查找、ABS64/GLOB_DAT/JUMP_SLOT/RELATIVE 计划；预留真实基址、重定位副本并封页；装载失败时回滚 |
| `dm_module_inspect`、`dm_module_test` | 离线链接／可选映射；合成跨模块符号、弱／强／绝对／隐藏符号、非法边界、循环、重叠／未对齐目标与失败无部分写入 |
| `image_map.c` | RW 拷入后，将代码设为 RX、只读数据设为 R、数据/BSS 设为 RW、间隙按宿主页不可访问；拒绝 W/X 同页冲突 |
| `gate.S` | 直接复用上游 `host/entry.S`，把 Mach-O 符号名适配为 ELF |
| `dm_image_test` | 从映射内存执行手写 ARM64 小程序，经宿主门调用系统计时后返回；检查代码只读、数据不可执行、BSS 可写、边界与 W^X 拒绝 |
| `dm_image_code_test` | 预留独立 RX 跳板区，安装合成 x18／TLS 补丁，登记实际门地址并原生返回；检查预留／空洞不可访问、RO/RW/NX 隔离及非法布局 |
| `dm_gate_test`、`dm_gate_vectors` | 通用寄存器回写、全部 32 个完整 SIMD 寄存器、FPSR、FPCR、NZCV 与平台 x18 保留 |
| `runtime/context.c`、`heap.c`、`event.c` | 公共 pthread 客体上下文、原始 TLS/TLR 分配析构、稳定地址堆调整/清零、手动/自动复位事件和单调时钟超时 |
| `runtime/patch.c`、`dispatch.c`、`svc.c` | 将支持的 SVC/计时/TLS 指令改写为真实原生门；这些指令中的 x18 虚拟化及 fp/lr 操作数处理；基础堆、休眠、计时、有界调试输出服务 |
| `runtime/input.c` | 完整 Npad 快照（0x28 字节 ABI）、短点击锁存、持续按键、断开清空、摇杆轴范围限制 |
| `vulkan/` | 实际设备/队列/显存操作、SPIR-V 计算管线、描述符/push constants、图像清除/布局转换/拷贝、屏障、栅栏与已核对的 CPU 读回 |

单独的 `dm_image_map` 不进行重定位；`dm_modules_map` 预留所有基址，在提交权限前重定位副本。二者均不保证可以安全进入代码。只读段可按页共享权限，但 W/X 冲突直接失败。合成小程序不是改写后的游戏模块。ARM64 执行/门测试只在 ARM64 开发机上运行，同时会编译成 Android 程序。

系统改写器支持已识别的 SVC、`cntpct/cntvct`、`tpidr_el0`、`tpidrro_el0`。可移植产物装载已实现；完整控制流分类和游戏初始化仍未完成。门表在线程运行时必须保持不可修改。事件句柄及只等待事件的 WaitSynchronization SVC 已实现；线程创建／等待、取消、互斥／条件／地址仲裁仍未完成。调试输出只接受可读堆范围。未知 SVC 返回 `NotImplemented`，未解析导入直接中止。`GetSystemTick` 按 [libnx SVC 封装](https://github.com/switchbrew/libnx/blob/master/nx/source/kernel/svc.s)和[错误码定义](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/result.h)，在 x0 直接返回 19.2 MHz 计数。

GPU 测试证明计算、传输和合成图形绘制／读回，还不是游戏 NVN 渲染或 swapchain 呈现器。macOS 使用 MoltenVK，不能代替 Adreno 验证。队列需外部串行化，离屏辅助函数同步提交。在构建目录的 `vulkan` 下运行 `dm_vulkan_test pattern.spv fullscreen.vert.spv pixels.frag.spv`，这些文件均有 Android 构建。

检查本地原程序，不执行它们：

```sh
../d2r-work/build/host-sanitized/dm_nso_inspect \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/main \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/sdk \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/subsdk0
```

检查跨模块链接，预留真实地址，对私有副本重定位、封页并释放，不执行代码：

```sh
../d2r-work/build/host-sanitized/dm_module_inspect --map \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/main \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/subsdk0 \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/sdk
```

省略 `--map` 时仅用分析地址规划，不映射、不写重定位。缺失强符号返回退出码 3，非法／不支持的元数据或映射错误返回 1。映射器拒绝缺少强符号的映像。只记录初始化器，不调用。暂不支持 ELF TLS 重定位、IFUNC 与 REL/RELR。映射器不安装指令改写或 HLE 接管，不是游戏启动入口。预留／提交接口只用于装载器独占的新映射，不得重新提交正在运行的客体映像。

开启 USB 调试，连接手机并授权此电脑，然后运行（改过输出目录的话请替换路径）：

```sh
adb devices -l
adb push ../d2r-work/build/android/arm64-v8a/dm_android_probe /data/local/tmp/dm_android_probe
adb push ../d2r-work/build/android/arm64-v8a/dm_texture_test /data/local/tmp/dm_texture_test
adb shell chmod 700 /data/local/tmp/dm_android_probe /data/local/tmp/dm_texture_test
adb shell /data/local/tmp/dm_android_probe
adb shell /data/local/tmp/dm_texture_test
```

连接多台设备时，每条命令都使用 `adb -s SERIAL`。无需 root。匿名内存 RW→RX 权限切换失败、Vulkan 1.1 实例创建失败或无法创建任何图形逻辑设备时，探针返回非零。它报告机型、SoC、Android 版本、页大小、GPU/驱动版本、BC/ASTC 支持及 shader-int64 支持。这些是观测数据，不是最终游戏兼容性要求。它不执行测试指令，也不运行着色器。探针 PASS **不表示游戏可运行**：它没有测试呈现表面、各格式的具体用途支持、着色器正确性、客体代码执行和 App 域 SELinux 策略。尤其需要在未来的 APK 进程内重新验证内存权限，不能用 `adb shell` 结果代替。

纹理测试直接复用上游 `nvn_tex.c` 和 `nvn_deswizzle.c`，不复制实现。使用合成 RGBA8、BC1、BC3 和 ASTC 数据，检查 2D、数组、3D 的多级纹理、末尾保护字节、过短行距、非法级/层拒绝，以及独立计算的 GOB 地址。不需要专有样本，也可在开发电脑上运行：

```sh
./dm/android/test.sh
```

宿主测试默认输出到 `../d2r-work/build/host-sanitized`（可用 `DM_ANDROID_HOST_BUILD_DIR` 修改）。CTest 覆盖解码、改写、可移植产物、原生执行、链接、输入、RomFS 导入、事件 SVC 和真实 GPU 作业。完整日志在构建目录的 `Testing/Temporary/LastTest.log`。

## 可选解包与验证工具

`./dm/android/tools.sh <command> ...` 选择外置工作区的 Python／hactool 路径，不自动读取游戏或密钥。所需工具须自行安装，wrapper 不是安装器。用户自备输入见 [GAME_FILES.zh-CN.md](../../docs/GAME_FILES.zh-CN.md)，上游工具／许可见 [DEPENDENCIES.zh-CN.md](../../docs/DEPENDENCIES.zh-CN.md)。工具、解包文件与诊断日志均留在 Git 外，共享证据前先脱敏。

## 历史路线图：剩余移植工作，按依赖顺序

目前已验证的基础包括支持取消及虚拟优先级查询的线程／同步句柄、堆与别名映射、精确栈租约、原生宿主服务栈桥，以及生产构建中有界的 4–32 KiB 汇编客体。这些是受支持的合成执行路径，尚不能证明完整 SDK 或游戏兼容。新增纹理后端也仅是宿主侧 RGBA8 资源子集。新增 `runtime/lm_wire.c` 仅解码有界 Log 请求形状（type6、一个 static、一个 map-alias buffer，长度 1–1024，指针低于 2^39）；只返回数值字段，不解引用客体内存。`runtime/lm.c` 另为活动子对象的 Log 请求按宿主保留最多四个已复制 1 KiB 数据包，并按 FIFO 取出；它不是 sink，不实现 SetDestination，也不代表完整日志服务。安装按宿主串行化，只有真实 lm 后端可拥有该端口；Log 源必须通过自有可读范围 pin。

1. **安全 SDK 入口与必需服务。** 在不放宽严格扫描器的前提下，解决 SDK 监视器返回／间接入口证明；补齐其余必需服务／导入约定、有序退出和受控模块初始化；有界同步取消、优先级查询及可选进程别名预留区现已实现。严格自有后备记账现支持 GetInfo 21/22，统一自有预留区已将模块及 TLS 映射纳入 GetInfo 12/13；其余 Horizon 区域策略／服务规则、虚拟核心掩码及后续 IPC／服务初始化仍待完成。已有改写、链接和重定位检查不意味着未解决的 SDK 路径可以执行。
2. **从 NVN 资源到呈现。** 将客体 NVN 资源生命周期和更新接入 Vulkan；补齐所需纹理格式／布局、翻译着色器、描述符反射／绑定、渲染目标／状态及绘制命令。复用 `gfx/tools/dmsh_xlate/Program.cs` 的 SPIR-V／反射阶段，再加入真正的 Vulkan swapchain 呈现、帧同步和表面丢失处理。RGBA8 图像采样仅是一个前置资源环节，不是完整渲染器。
3. **真实 HID、音频及持久存档。** 将 Android 触控／手柄事件接到客体 HID 导入，实现音频后端与焦点处理，并加入有持久性和重新读取验证的可写存档。已测试的 Npad 快照和只读 RomFS 是这些路径的基础。
4. **App 生命周期集成。** 将资源导入、受控初始化、运行时所有权、退出和恢复接入现有诊断 NativeActivity 外壳。诊断暂停／恢复日志尚不能证明游戏生命周期恢复。玩家可见文案继续复用共享字符串表。
5. **设备与游戏验收。** 设备可连接后，在小米上运行新增路径，再验证真实 APK 场景、输入、音频、存档读写和生命周期；条件允许时覆盖 4／16 KiB 页，并测量持续性能／温度。此前设备 CLI／诊断结果不能验证此次纹理后端或完整游戏启动。

游戏运行与视觉验收需要用户按 [GAME_FILES.zh-CN.md](../../docs/GAME_FILES.zh-CN.md) 准备自己的文件。安全 SDK 入口与服务覆盖是受控初始化的前置条件；图形开发可同时通过自有样本推进。目前没有证据支持统一完成百分比或交付日期。

## 2026-10-16–17 验证记录

基于上游提交 `source snapshot`：

- NDK r29 / Clang 21：Android ARM64 工具交叉编译成功，包括运行时/系统改写/输入测试和 Vulkan 程序/合成着色器；编译不代表 Android 运行兼容。
- ARM64 macOS 宿主 CTest，开启 AddressSanitizer 和 UndefinedBehaviorSanitizer：25/25 通过。GPU 测试实际运行在 Apple M4/MoltenVK，逐像素核对输出；原生改写覆盖虚拟 TLS、普通 x18、fp/lr 写入、暂存／平台 x18 保留、NZCV、SP 前后索引、32 位清高位、计数器和 SVC 计时。
- 独立 ThreadSanitizer 构建的运行时/输入测试通过：四个客体线程、1,000 次事件握手、50,000 次并发输入发布。这是宿主并发证据，不是 Android 调度或驱动验收。
- Shell 语法、无效 NDK 路径错误处理、`git diff --check`：通过。
- `dm_i18n.py --check`：372 行通过。未修改的文案扫描器用系统 Python 3.10 会报告两条基线诊断，使用已准备的 Python 3.12 则正常退出；但它仅处理 STRING token，没有覆盖 Python 3.12 独立的 f-string token，不能视为完整字面量覆盖保证。本轮没有修改扫描器。
- 开发机 Mac 已解析、链接并映射真实 main/subsdk0/sdk：116,034 条重定位，强符号缺失为零，16 处未解析弱符号引用置零。九个 NSO 段摘要全部通过。这是本地检查，不是游戏执行。
- 未连接手机。未验证 Android 设备/APK 进程执行、游戏启动、完整游戏改写等价性或游戏 Vulkan 渲染。

## 普通 x18 改写与完整 text 审计

`runtime/x18_patch.c` 使用真实的 256 字节栈帧，通过一次宿主门取得客体 x18 槽地址。替换后的 ARM64 操作原生执行，暂存寄存器和平台 x18 保持原值。覆盖普通算术、访存、32 位清高位、fp/lr 写入、NZCV、有界常量 SP 前后索引写回。拒绝索引或过深负偏移 SP 访存、动态 SP 赋值、PC 相对指令、分支和独占窗口。每条指令一次宿主门是优先保证正确性的实现，不表示接近零开销或已通过设备性能验收。

宿主解码器复用上游 Capstone 操作数替换方法，逐字比较替换后的反汇编，包括立即数。宿主审计需要 Capstone 5 头文件／库（本机使用已有 Homebrew 安装）；Android 目标不链接 Capstone。没有该库的宿主可设 `-DDM_ANDROID_REWRITE_SCAN=OFF`，省略审计及相关测试。

```sh
../d2r-work/build/host-sanitized/dm_rewrite_scan \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/main \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/subsdk0 \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/sdk
```

退出码 3 表示存在延后／不可解码点，1 表示输入／工具错误，2 表示用法错误。工具不生成可运行映像。线性反汇编可能遇到填充／内嵌数据，不能证明代码与数据分类。独占窗口沿用上游保守的 16 条指令界限；找不到配对存储的加载，以及识别到窗口内全部 x18／系统指令均被报告，不逐条打补丁。完整改写器必须整体处理窗口及分支出入口，不得在独占加载／存储区域中调用宿主。真实审计结果留在工作区 `build/dump-inspection/rewrite-scan.log`。

## x18 PC 相对地址与条件分支

`runtime/x18_control.c` 新增 96 字节原生跳板，支持 ADR/ADRP、W/X CBZ/CBNZ、TBZ/TBNZ，复用普通跳板的寄存器保存／客体槽前缀。ADR/ADRP 按迁移后的 PC 重编码为 ADRP+ADD，映射基址须按 4KB 对齐。条件指令判断保存客体 x18 的暂存寄存器，跳转／顺序两条路径各自恢复寄存器和栈，再回到原目标；NZCV 和客体 x18 不变。拒绝间接 BR/BLR/RET，发布输出前核对全部边界和分支位移。

`x18_control` 测试在 sanitizer 下原生执行合成模块：正负跨页地址、前后跳转的两条路径、W/X 宽度差异、第 0/31/32/63 位与 NZCV 保留。Android 交叉编译也生成 `dm_x18_control_test`。真实模块审计通过 373 个 PC 相对位置和 787 个条件分支位置。此前延后的间接跳转、窗口位置和 SP 读取别名现由下文区域实现覆盖；未配对的独占加载及无法解码的 text 字还需要分类。审计继续返回 3，不生成完整游戏可运行映像。最新本地日志：`../d2r-work/build/dump-inspection/control-scan.log`。

## 跳转表、独占窗口与 SP 读取别名

`runtime/x18_jump.c` 接受已验证的无符号字节／半字索引跳转表。`rewrite/jump_table.c` 核对前八条指令、ADR/ADD 来源、无符号加载宽度及中途寄存器覆盖。运行时跳板检查索引范围／对齐，通过等距落地分支跳转，在进入原目标前恢复借用的寄存器和栈。仍不支持任意间接跳转，不使用红区。

`rewrite/window_decode.c` 选择整个窗口内未使用的寄存器，核对替换，拒绝 SP 访问、调用、PC 相对数据操作及嵌套独占加载，并检查整个 text 的直接分支是否从外部进入窗口中部。`runtime/x18_window.c` 整段复制并重定位分支，顺序落出使用出口零。独占加载／存储之间及重试循环内没有宿主调用；宿主门只在第一次加载前执行。直接入口检查是保守检查，不能证明全部间接入口或完整代码／数据分类。

新增的手写与解码驱动原生测试覆盖字节／半字表边界和非法目标陷阱、四线程 4,000 次自增争用、用 CLREX 强制首次独占存储失败后重试成功，以及成对 CAS 比较相等／不等的出口。另一项解码后原生测试证明 `mov x18,sp` 读取原客体 SP；解码器处理其 ADD/MOV 别名，不改变栈。这些此前延后的实际位置不需要动态切栈指令。

区域审计通过 20 个跳转表位置、12 个独占窗口中的全部 24 个 x18 位置，以及 13 个 SP 读取别名。三个模块延后的 PC 相对、分支、其他及窗口位置计数均为零。**这不表示游戏可以启动：**仍有 48 个未配对独占加载和 1,047 个无法解码的 text 字待分类。审计仍返回 3。本地证据：`../d2r-work/build/dump-inspection/regions-scan.log`；没有执行游戏代码。下文新的共用准备扫描还会报告未支持的系统寄存器。

## 附加可执行跳板与模块链接

`dm_image_reserve_with_code`／`dm_image_commit_with_code` 接收独立跳板缓冲区和映像内偏移。跳板首页必须位于原映像所有页面（包括 BSS）之后，并按宿主实际页大小对齐；合计预留上限仍为 512 MiB。地址预留时不可访问；提交时清零填充区，在 RW 状态复制映像／跳板，刷新指令缓存，再将 text／跳板封为 RX、rodata 为 R、data／BSS 为 RW，中间空洞不可访问。不产生可写且可执行的页面。这些函数不负责生成或验证指令改写。

`dm_modules_map_with_code` 将各模块跳板区接入实际基址符号解析与私有副本重定位。全零描述项表示没有附加区域。非法布局、缺少跳板字节或后续失败均回滚全部预留，不发布输出、不改变输入映像。原有 `dm_modules_map` 调用保持原行为。测试覆盖单模块附加跳板、原生跳入该区域、混合无跳板模块，以及后续模块提交失败。独立的 `image_code_mapping` 测试覆盖原 text 与附加代码区之间的 x18／TLS 跳板及宿主门往返。

本地证据均在 `../d2r-work` 下：`build/image-code-host-build.log`（20/20 sanitizer 测试）、`build/image-code-android-build.log`（17 个 Android ARM64 程序）、`build/image-code-android-elf.log`（新增程序 LOAD 段 16 KiB 对齐）和 `build/dump-inspection/image-code-module-map.log`（116,034 次真实重定位，未执行客体）。两个映射实现的静态分析通过。Android 执行仍需要手机。

额外本地诊断将 1,047 个无法解码字细分为：1,027 次相同的 `0xe7ffdefe` 编码、MOD0 元数据内 18 个字，以及另外两个字。48 个未配对独占加载包含 CLREX、后续／嵌套独占操作的控制流；其中 40 个在后续 24 条线性指令内出现 CLREX。这些观察不能证明安全的代码／数据边界或独占状态存活路径，因此没有屏蔽任何阻塞。详细输出保留在 Git 外的 `build/dump-inspection/remaining-all-shapes.log`。分类及安全初始化约定仍未解决；游戏尚未启动。

## 共用审计与内存改写准备

`rewrite/prepare.c` 是 `dm_rewrite_scan` 与宿主准备 API 共用的扫描实现。`dm_rewrite_prepare` 可以只审计，也可以生成独立的已补丁 NSO 映像、附加跳板缓冲区与映像相对门表。它始终解码未修改的源映像，复用所有现有跳板生成器，将映像／跳板合计限制为 512 MiB，仅在整次扫描通过后发布输出。任何未支持位置、未配对独占加载、无法解码字或生成失败都会释放部分缓冲区，保持调用方输出及源映像不变。这是当前进程的内存产物：宿主门地址不是可移植的 Android 磁盘格式。调用方必须给门条目加实际映射基址，合并／排序，并在线程使用期间保持不可修改。

`rewrite_preparation` 测试两个完整的合成 MOD0/RELA 模块：生成改写产物，按预留基址链接，复制并封闭映像／跳板页面，登记合并门表，然后原生执行重定位后的跨模块调用。其他原生路径覆盖系统／TLS、普通 x18、ADR、条件分支两条路径、独占重试窗口和字节索引跳转表。审计与实际生成的报告相同；测试还拒绝非法参数、未知字／寄存器／间接分支、未配对加载和超范围跳板分支，且不发布部分输出。不执行游戏初始化器。

```sh
../d2r-work/build/host-sanitized/dm_rewrite_scan --prepare-check \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/main \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/subsdk0 \
  ../d2r-work/data/switch/3.3.94012/Update/ExeFS/sdk
```

此 CLI 使用占位门地址，丢弃成功的内存输出，不写文件、不映射、不执行产物。返回 3 表示覆盖阻塞，1 表示输入／工具失败，2 表示用法错误。系统寄存器检查允许原生 NZCV/FPCR/FPSR，现会拒绝其他未处理的特权操作，包括真实 SDK 中四处 `CTR_EL0` 读取。真实准备检查返回 3：仍有 48 个未配对加载、1,047 个无法解码字，再加上四处寄存器读取。不发布真实模块产物、不进入游戏代码。线性覆盖仍不能证明代码／数据分类、间接入口、Horizon 服务或初始化安全。

证据位于 `../d2r-work`：`build/prepare-host-build.log`（21/21 ASAN/UBSAN 测试）、`build/prepare-android-build.log`（现有 17 个 Android 目标构建通过；Capstone 准备工具仍只用于宿主）、`build/static-analysis/prepare`（Clang 分析），以及 `build/dump-inspection/prepare-check.log`／`unsupported-systems.log`（真实文件只读检查）。下一步是剩余分类／缓存寄存器语义及 Android 产物绑定／初始化；尚无 APK 或设备执行。

## 检查点：可移植产物、服务、绘制与诊断 APK

`rewrite/exclusive.c` 从未配对加载沿全部有界直接路径检查到存储／CLREX／下一次独占加载，只接受途中无需宿主改写、没有未知调用／返回的路径；上限是 256 个不同指令、起点前后 4 KiB。此前 43 处已确认可保持原生执行，剩余五处 SDK 路径未解决。完整验证的 MOD0 结构按元数据处理，拒绝直接分支进入。精确未分配编码 `0xe7ffdefe` 原样保留、单独计数；原生子进程测试确认仍会陷阱（本机为 SIGTRAP）。依据是 [Arm 架构手册 C4.1](https://cs140e.sergio.bz/docs/ARMv8-Reference-Manual.pdf) 顶层 `0011` 未分配组，不是把任意未知字当作填充。

四处 `CTR_EL0` 读取保持原生，使缓存维护使用目标 CPU 实际缓存行大小，依据 [Android compiler-rt 缓存刷新实现](https://android.googlesource.com/toolchain/compiler-rt/+/release_36/lib/builtins/clear_cache.c)。测试中 Mac 直接访问触发异常，因此 Darwin 测试改为核对保留字节，不执行 CTR；Android 探针会报告实际值。Android CTR 访问及缓存维护仍须设备验证。

最新准备结果：main 没有扫描阻塞；subsdk0 剩一个未分类字；SDK 剩一个未分类字和五条未解决独占路径。main 已导出为 `../d2r-work/build/android/artifacts/main-v1.dma`，通过产物读取／摘要检查。它包含用户本地游戏代码，保留在 Git／APK 外。**没有执行真实入口或初始化器；扫描通过不代表完整改写等价、服务覆盖或游戏启动许可。** 日志：`build/dump-inspection/services-prepare-check.log`、`main-artifact-export.log`。

`artifact.c` 把未链接产物保存为 v1 小端格式：160 字节头、映像字节、跳板及 16 字节相对门记录。头／负载 SHA-256 只检查损坏，不验证来源真实性；文件及映射映像上限均为 512 MiB。跳板偏移按 16 KiB 对齐；磁盘中的宿主门字面量清零，装载时按已验证修补位置在私有副本中重绑定，再封为 RX。读取端检查长度、段／页重叠、保留字段、门类型／参数／顺序与修补边界；读取／映射失败不发布部分输出。写入使用独占创建，不覆盖已有路径。仅接受可信的可执行输入，不构成沙箱。

```sh
../d2r-work/build/host-sanitized/dm_rewrite_scan --export NEW.dma ExeFS/main
../d2r-work/build/host-sanitized/dm_artifact_inspect NEW.dma
# 只检查／链接／映射，不进入代码：
# Inspect/link/map only, never enter code:
../d2r-work/build/host-sanitized/dm_artifact_inspect --map first.dma second.dma
```

链接时可以显式绑定宿主导入，仅优先替换未定义符号的重定位，不改变本地定义或直接调用。只读 `runtime/romfs.c` 适配六个上游 nn::fs 导入（缓存查询、挂载、打开、读取、大小、关闭），提供精确大小写、禁止符号链接／`..` 的根目录 openat 遍历、pread／EOF 语义，以及 128 个带代际的句柄。仅支持原始解包资源，不支持 `.holes`、overlay、目录枚举、可写存储或存档事务。客体缓冲区必须是调用方有效内存，不构成客体内存沙箱。测试覆盖 4,000 次并发读取、过期／满句柄拒绝；真实 RomFS 前缀读取通过，真实模块 116,034 次重定位中应用十次宿主导入写入，未执行游戏。日志位于 `build/dump-inspection/romfs-import-read.log`、`romfs-import-link.log`。

`runtime/sync.c` 实现 CreateEvent、SignalEvent、ClearEvent、CloseHandle、ResetSignal 和仅等待事件的 WaitSynchronization SVC。读写端权限分离，事件手动复位，等待时保留对象引用以承受句柄关闭，按单调时钟处理下标／超时，代际溢出时停用句柄槽。上限是 64 个对象／128 个端点、每次等待 64 个句柄。线程等待、取消及互斥／条件／地址仲裁尚未实现，禁止 pthread_cancel 正在等待的线程。宿主 ASAN/UBSAN 测试通过，事件及 RomFS 的独立 ThreadSanitizer 测试通过（`build/services-tsan.log`）。

`vulkan/vk_draw.c` 加入真实顶点／片段管线、图像视图、render pass／framebuffer、全屏 draw、颜色写入→传输→宿主屏障及读回，依据 [Khronos 同步指导](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。M4/MoltenVK 上 1×1、13×11、37×19 的全部像素及尾部保护字节通过；这是合成绘制，不是 NVN 回放、游戏着色器或 Adreno 结果。

[NativeActivity 诊断 App](app/README.zh-CN.md) 已构建、调试签名，通过 APK 签名及 16 KiB ZIP 对齐检查。它只打包自有合成 ARM64／TLS 与 GPU 样例，处理窗口／重绘／低内存回调，通过 CPU 读回显示 GPU 诊断图案；不是后续游戏 swapchain 呈现器或游戏 UI。按照用户先完成离线部分的范围，App 进程执行、生命周期、截图／可访问性和 Adreno 验收仍待手机验证，不宣称视觉 QA 通过。

当前验证：宿主 ASAN/UBSAN 25/25（`build/services-host-build.log`）；21 个 Android 命令行程序及诊断动态库；已签名诊断 APK（`build/app-apk-build.log`）；静态分析在 `build/static-analysis/services`；文案表 372 行。所有产物及 SDK 包仍在 `../d2r-work`，改动仍只在本地、未提交。

剩余顺序：解决五条 SDK 路径／两个字及间接／数据入口问题；实现线程生命周期、互斥／条件／地址仲裁、内存／SDK 服务与安全初始化；扩展目录及事务存档；实现 NVN 资源／状态／着色器／绘制语义及游戏 swapchain 呈现；接资源导入、游戏输入／音频／UI；最后做小米 APK 进程验收。诊断 APK 不代表这些任务已完成，也不能支撑帧率／温度承诺。

## 检查点：监视器语义与文字池分类

SVC 跳板现在明确在返回前执行 CLREX，对应 [Arm A64 同步指导 §3.2.4](https://documentation-service.arm.com/static/68c223238a337a2bc6645c0a) 描述的异常返回清除监视器。MRS/MSR 跳板保持原布局；SVC 的门返回／修补偏移及总计 32 字节大小不变。有界独占路径检查接受直接 SVC 和首指令就是 SVC 的直接 BL 目标，仍拒绝其他调用、间接调用及带活跃预留返回。这解决此前五条 SDK 路径中的四条，不代表已实现相应 Horizon 服务。

准备器在改写前标记两种受限的单字数据：正向 32 位 LDR-literal 引用、位于 RET 后且下一字为 NOP 的常量；以及寄存器一致、相对目标位于 data/BSS 的 BL 跨单字／从 LR 加载／SXTW／加 LR 模式。保留原字节，包括能解码为 x18 指令的数值。已知直接分支／ADR 入口及 ADRP 页引用与分类冲突时，阻止发布产物。检查宽度、边界、MOD0 重叠及寄存器。这是有数据证据的模式识别，不能排除所有间接代码／数据入口；不把展开表缺口或任意解码失败当作数据证据。

真实模块结果：main 和 subsdk0 均无扫描阻塞；SDK 未知字归零，剩一条 +0x34f390 的独占路径，可能带活跃监视器返回。两个辅助模块各分类一个文字池字，文字池入口冲突均为零。main 与 subsdk0 已导出并通过摘要校验，位于 `../d2r-work/build/android/artifacts/monitor-literal/`；新的 main 替代此前 `main-v1.dma` 检查点。这些仍是本地游戏派生产物，不进入 Git 或 APK。没有执行真实初始化器或游戏指令。日志位于 `build/dump-inspection` 的 `monitor-literal-final-check.log`、`monitor-main-export.log`、`monitor-subsdk0-export.log`、`monitor-artifact-inspect.log`。

SVC／文字池回归在修复前失败、修复后通过。当前宿主 ASAN/UBSAN：25/25（`build/monitor-literal-host-tests.log`），包括原生执行合成文字池读取，以及原有 TLS／计时／SVC 门执行。另有专门的合成宿主门重新建立独占预留，经过 SVC 跳板返回后 STXR 失败、内存不变。反例覆盖入口冲突、64 位加载、错误 BL 距离、寄存器不一致及无效相对目标。Android ARM64 重编译通过（`build/monitor-android-build.log`）；生产 C 改动通过 Clang 分析（`build/static-analysis/monitor-literal`）。最新 SVC 改动未在手机运行。

此前诊断 APK 已在小米 / SM8750、Android 16 API 36、4 KiB 页通过合成 ARM64 门执行、Vulkan 256×256 像素校验／窗口提交及基本暂停恢复日志，见 [App 检查点](app/README.zh-CN.md)。完整游戏执行、NVN 图形及视觉／生命周期压力验收仍待完成。下一步先确定剩余监视器返回路径的安全处理，再补齐线程／同步服务与受控初始化。

## 检查点：32 位地址等待／唤醒服务

`runtime/arbiter.c` 实现 SVC 0x34 WaitForAddress 与 0x35 SignalToAddress 的 32 位类型 0/1/2，依据 [libnx ABI](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/kernel/svc.h) 及 [公开内核语义](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/kern_k_address_arbiter.cpp)。支持有符号小于／相等谓词、先减一再等待（包括零超时）、仅唤醒、相等则加一、按等待者数量修改。使用原子操作和明确的无符号回绕。谓词检查、队列登记与通知共享宿主锁，避免丢失唤醒；已选中的等待者不会在恢复前被后续信号重复计数。正超时使用加锁前捕获的单调时钟截止点，负超时无限等待。最多 128 个活跃等待，同址 FIFO；线程优先级排序、取消及 64 位相等等待尚未实现，禁止 pthread_cancel 等待线程。

仲裁只接受已提交堆或明确登记的可写段中的对齐字。`dm_host_register_writable` 在附着线程／开始等待前登记最多 16 个不重叠可信范围，拒绝堆重叠及无效／溢出范围，不转移内存所有权。只登记模块的可写数据映射，保持其有效直到宿主销毁。这是装载器契约，不是系统权限探测或沙箱。地址等待活跃时拒绝缩堆，防止地址失效；允许扩堆，等待结束前销毁宿主返回 EBUSY。非零等待在减计数前检查资源／队列容量，避免被拒绝的等待留下无对应等待者的计数变化。

当前验证：宿主 ASAN/UBSAN 26/26（`build/arbiter-host-tests.log`），RomFS／事件／仲裁的 ThreadSanitizer 3/3（`build/arbiter-tsan-tests.log`），Android ARM64 交叉编译（`build/arbiter-android-build.log`），Clang 分析（`build/static-analysis/arbiter`）。测试覆盖内存登记／边界、谓词／回绕、FIFO／计数／地址隔离、截止时间、堆生命周期、全部 128 个等待槽、资源不足时计数不变、500 轮请求应答，以及通过原生 ARM64 门执行合成 SVC34/35。服务实现前运行时回归失败（`arbiter-red-test.log`）；队列容量回归也在修复前单独失败（`arbiter-capacity-red.log`）。本轮没有重新连接手机运行。

SDK 的监视器返回阻塞仍保留。只读调用方调查支持它是原生 ticket-lock 轮询辅助函数，三个已知直接调用方的后续路径均不再使用该预留，但仍未闭合计算地址或跨模块间接调用方。轮询指令及严格返回拒绝规则保持不变。托管线程创建／启动／退出／等待、优先级／取消、互斥／条件服务、安全游戏初始化及 NVN 图形仍待实现。没有执行真实游戏代码；此前 main／subsdk0 仍是最新已接受的导出产物。

## 检查点：托管原生线程生命周期

`runtime/sync.c` 现支持 CreateThread（0x08）、StartThread（0x09）、ExitThread（0x0a）、GetThreadId（0x25）、CloseHandle 及事件／线程混合 WaitSynchronization。托管线程使用独立 pthread、客体 TLS/TLR、稳定的预留 ID 与代际句柄；TLR 自有线程对象的 +0x1b0 写入线程句柄。关闭句柄不会使运行线程或已固定的等待失效。线程完成为手动复位信号，不能通过事件端点清除／触发。未启动线程在最后引用关闭时释放栈租约；已启动线程先回到宿主栈、分离上下文，再释放租约并发布完成。已完成但未关闭的线程对象仍占用一个槽。

`runtime/thread_stack.S` 在切栈时保存宿主非易失 GPR/SIMD、浮点状态及平台 x18。ExitThread 恢复宿主栈，不在客体栈上调用 pthread_exit 或展开宿主帧。保留 x18 也遵守 Android 的 [ShadowCallStack ABI](https://source.android.com/docs/security/test/shadow-call-stack)。ASAN／TSAN 验证构建分别通过 fiber 接口描述切换。客体入口未调用 ExitThread 就返回，仍按致命契约错误处理。

附着前用 `dm_host_register_executable` 登记可信可执行段，由装载器保证 RX 权限及寿命；可写／可执行登记不可重叠。CreateThread 前用 `dm_host_register_thread_stack` 登记每个精确栈范围：至少 64 KiB、16 字节对齐，完整位于已提交堆或已登记可写段。这些 API 不猜栈大小、不探测系统页权限。已租用栈不能复用／取消登记，也不能被缩堆移除。空闲登记可用 `dm_host_unregister_thread_stack` 删除。上限为 16 个可执行段、16 个可写段、64 个栈登记、64 个托管线程对象及 128 个共享事件／线程句柄。已创建线程仍可能执行时，门表／代码须保持有效且不可修改。已创建未启动线程在关闭或完成前阻止销毁宿主。

StartThread 等待原生上下文初始化结果再报告成功，启动操作引用与运行引用分离。初始化失败返回资源错误，不发布完成信号，保留未启动句柄供重试／关闭。回归在没有客体运行时，故意注入无效的宿主门元数据，验证失败传播、入口未执行、同句柄重试及回收；握手修复前失败（`build/thread-startup-red.log`）、修复后通过。

当前证据：宿主 ASAN/UBSAN 27/27（`build/thread-host-tests.log`）；TSAN 4/4（`build/thread-tsan-tests.log`）；Android ARM64 交叉编译（`build/thread-android-build.log`），新可执行文件的 LOAD 按 16 KiB 对齐；Clang 分析（`build/static-analysis/thread`）。原生测试在实际登记的客体栈上运行自有 C 入口，并通过真实调用门执行自有改写 ExitThread SVC。覆盖八个并发客体／TLS ID、事件／线程混合等待、等待／运行中关闭、未启动关闭、100 次启动／关闭竞争、初始化失败／重试、64 个完成对象容量／复用、栈／堆生命周期及无效登记／创建参数。临时调查文件已清理；日志／构建仍留在 T2。

这些是后端及合成测试结果，没有执行真实游戏，也未重新连接手机运行。外部附着上下文有当前线程伪句柄 ID，但真实主线程句柄／引导登记仍待接入。游戏 SDK 的栈分配尚未连接到显式登记 API。虚拟优先级／核参数只做合法性检查；优先级、亲和性、取消、互斥／条件服务及小栈支持仍待完成。SDK 最后一条监视器返回扫描阻塞保持不变。下一步是主线程引导／栈分配接入、互斥／条件服务，执行契约补齐后再做受控游戏初始化。

## 检查点：主线程登记、SDK 式栈及互斥／条件服务

`dm_guest_bootstrap_main(host,gates,&handle)` 现以虚拟优先级 44 附着当前 pthread 并登记真实线程句柄，写入自有 TCB 的句柄／ID。登记失败会撤销附着，错误时不修改输出。`dm_guest_register_current_thread` 可为已附着原生线程按指定优先级登记。detach 或 pthread TLS 析构会发布完成信号、释放执行引用，并唤醒所有者已结束的互斥等待者。关闭句柄保留已有执行／等待引用。外部原生线程不能再次 StartThread，也不能使用托管 ExitThread 切栈。这些登记共享现有 64 个线程对象上限；不会调用 `nnosInitialize` 或初始化器。

可选 `dm_host_set_sdk_thread_abi` 接收由装载器验证的入口、ThreadType 大小、64 位栈基址／大小字段偏移和顶部保留区；必须先验证 SDK 映像／布局，并在附着前配置。没有自动版本猜测或内置游戏地址。匹配的 CreateThread 会检查完整 ThreadType／分配区均位于可信可写映射，检查对齐／溢出、精确的 `SP=base+size-reserve`，以及 SP 处回调指针位于已登记可执行段。运行时自动租用完整分配，并单独保存初始 SP；关闭未启动句柄或完成线程会删除自动登记。碰撞、未映射别名及无效字段在创建前拒绝。原有显式栈登记仍受支持。

`thread_stack.S` 在 sanitizer 设置结束后再次恢复指定初始 SP，再尾跳进入客体。设置函数中普通 C 调用会占用栈空间，违反 SDK 入口契约。自有汇编测试记录精确入口 SP，并从顶部保留区读取回调后尾跳；80 次未启动和 81 次实际执行的 SDK 式生命周期验证自动槽位复用。回退为旧 C 调用时回归失败（`build/bootstrap-sp-red.log`），恢复汇编尾跳后通过。测试使用自定字段布局，不含游戏字节。

`runtime/mutex.c` 实现 ArbitrateLock/Unlock、WaitProcessWideKeyAtomic/SignalProcessWideKey（SVC 0x1a–0x1d），使用原子客体锁字和共享宿主锁。依据[公开内核条件变量实现](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/kern_k_condition_variable.cpp)：先发布条件键再释放所有权，将收到信号的等待者转入互斥队列，保持优先级／FIFO，转队列后继续使用有限的单调时钟截止点。超时不隐式重新取锁，由 SDK 重试；超时可能留下等待标志／条件键，直到 unlock/signal。所有者引用可承受句柄关闭，并在移交时转移；所有者结束会以错误唤醒依赖它的互斥等待者。互斥／条件等待固定客体地址期间禁止缩堆。

验证：宿主 ASAN/UBSAN 29/29（`build/bootstrap-host-tests.log`）；TSAN 6/6（`build/bootstrap-tsan-tests.log`）；Android ARM64 交叉编译（`build/bootstrap-android-build.log`）；Clang 分析（`build/static-analysis/bootstrap`）；文案表保持 372 行并通过检查。覆盖引导失败回滚／容量、真实客体初始 SP、自动栈租约回收、优先级／FIFO（含条件转队列）、关闭／结束所有者、无效锁字／标签、零／有限超时、单个／全部唤醒和 100 次条件键发布竞争。已附着线程的互斥／条件场景通过实际改写的自有 ARM64 调用门运行全部四种 SVC。内部等待上限为 128，但共享的 64 线程对象上限使公开登记接口无法达到它；不宣称验证了完整 128 个互斥等待者。

以上仍是后端及合成验证。SDK ABI 适配器必须显式启用，尚未用于真实游戏执行。它验证已映射的 SDK 分配，不实现 SDK 别名栈分配器、Horizon MapMemory/UnmapMemory 或小于 64 KiB 的栈。主线程 SDK 初始化、优先级继承／宿主调度、Oryon 亲和性及取消仍待实现。SDK 监视器返回扫描阻塞未变；没有执行真实游戏指令／初始化器，也没有新一轮手机运行，NVN 图形仍待完成。全部日志／构建仍在 T2，不进入 Git／APK。

## 检查点：有界自有栈别名

`dm_host_create_layout(heap_capacity,stack_capacity)` 可选启用连续的自有堆／栈预留，并以共享文件页支撑堆。Android 使用带 close-on-exec 的 memfd 公共系统调用接口（兼容 API 29）；macOS 使用 `TMPDIR` 下创建后立即 unlink 的临时文件。原有 `dm_host_create` 继续使用匿名堆。MapMemory/UnmapMemory（0x04/0x05）最多保存 64 个完整目标／来源／大小记录；来源仅限已提交的自有堆，目标仅限自有栈预留。映射期间来源不可访问，别名写入同一组底层页，解除映射后内容保留。地址和大小必须同时满足 Horizon 4 KiB 与宿主页对齐：4 KiB Android 可覆盖一般 4 KiB 请求，16 KiB 宿主仅覆盖对齐子集，不向相邻页扩大保护范围。

QueryMemory（0x06）报告自有空闲／堆／栈区间、锁定且不可访问的来源、已登记模块范围，填充完整 40 字节结果并在 x1 返回 PageInfo。输出必须完全落在自有或显式登记的可写范围内；将来查询原生主线程栈须显式登记，bootstrap 不探测操作系统栈映射。GetInfo（0x29）实现堆 4/5、ASLR 12/13、栈 14/15，以及按宿主保存、由系统密码学随机源生成的熵 11，并核对 handle/subid；不支持的项目返回失败。这些能力服务于自有分配器形状的测试，并不代表完整 SDK 初始化或 Vamm／物理内存服务已就绪。

映射变更拒绝重叠的线程租约或内存 pin，并保守拒绝存在地址／互斥等待者时的操作。堆收缩受存活别名、相关 pin 和现有线程／等待租约保护；文件截断保证重新提交时清零，失败时回滚权限。仍有别名或 pin 时不得销毁宿主。WaitSynchronization 在宿主互斥锁内检查并复制自有句柄数组。RomFS 在释放锁执行 I/O 期间 pin 住自有读写范围；非自有的有效原生调用方指针继续遵守既有可信约定，QueryMemory 输出则严格检查。操作系统回滚失败属于致命宿主不变量破坏。本检查点不含部分解除映射、权限变更 SVC、小于 64 KiB 的栈、自动启用 SDK 或真实游戏初始化。

实现者在 16 KiB ARM64 Mac 上验证：`dm_memory_test` 通过共享字节、来源及失效目标的子进程权限故障、查询边界／保护区、非法参数／64 项耗尽、真实无效 fd 映射／截断失败回滚、收缩再提交清零、pin、并发冲突、四个真实改写 SVC 门的 x0/x1 ABI，以及 100 次 SDK 式映射／创建／释放／解除映射循环（99 次原生入口，SP 精确匹配）。故障子进程安装信号处理器并用约定状态退出，不故意产生原生崩溃报告。加入最终原生门测试后，8 项相关回归全部通过。持久证据位于 `../d2r-work/.omo/evidence/alias-backend-20261017/`。本工作未执行游戏指令或初始化器。

主会话验收通过：宿主 ASAN/UBSAN 30/30、TSAN 7/7；最终原生门测试及 API29 测试声明修复后，memory/RomFS 在两种 sanitizer 下均重新构建并以 2/2 通过（`build/alias-host-tests.log`、`alias-tsan-tests.log`）。完整 NDK API29 最终构建通过（`build/alias-android-build.log`）。在小米 SM8750、Android16/API36、4KiB 页设备上，memory、RomFS、thread、bootstrap、mutex、arbiter、sync 共七个自有 CLI 测试全部通过（`build/alias-device-*.log`）；memory 测试在手机实际执行了四个改写后的 SVC 门。宿主与设备端二进制 SHA-256 一致，新 memory 可执行文件的 ELF LOAD 对齐为 0x4000。五个变更运行时单元的 Clang 静态分析诊断为零，既有 372 行 i18n 检查通过。这是 CLI 后端验收，不是新 APK 或游戏初始化结果。SDK monitor-return 扫描阻塞、完整 SDK 服务／初始化准备、小栈宿主调用桥以及 NVN／游戏图形仍未解决。

## 检查点：原生宿主服务栈桥

运行时版本的 `dm_gate` 及六个 RomFS 导入入口现均在已附着 pthread 的原生栈上进入宿主 C；独立调用门测试仍使用通用分派器。附着时通过 128 槽注册表发布不透明的原生线程标识和公共 pthread 栈边界。Android 使用 TPIDR_EL0；已验证的 Darwin 路径使用清除低三位后的 TPIDRRO_EL0，不读取私有 TLS 字段，也不借用 x18。缺少登记、栈域错误及重入会在宿主分派前陷阱。注册表附着或栈桥准备失败时，StartThread 在发布启动成功前返回失败，未启动句柄仍可重试。

汇编先查注册表再调用 C，在保存的原生 RUN 帧下方放置过渡帧，保留原位的 800 字节客体调用门帧及外层 FP/LR 溢出槽。从 RUN 恢复原生 x18、浮点控制／状态，检查实际服务 SP 位于原生边界内，并保持客体返回状态；读取完所有返回帧字段后才解除注册表忙状态。托管 ExitThread 服务先完整返回原生 C，再回到客体栈销毁客体 sanitizer fiber，最后在原生栈离开 RUN，绝不执行客体后续指令。仅原生附着的调用者继续使用当前原生栈。重入、信号处理器调用导入及活动服务内嵌套客体回调不在支持契约内。显式 `dm_romfs_query_cache_size` 保留唯一的附着前原生查询用途；客体导入表始终返回栈桥入口。

ASAN 在正常往返中跟踪两种栈，并在终止时销毁当前活动的客体 fake stack。TSAN 的切换使用 `no_sync`，同时在每次同步交接（含初始发布及最终完成）围绕该上下文记录执行 release/acquire，不使用全局交接令牌。纯过渡／引导辅助函数禁用 sanitizer 插桩，避免编译器钩子跨越 fiber 切换；独立且禁止内联的宿主服务函数保持插桩。sanitizer 构建在调用过渡钩子前要求来源 SP 下方至少剩余 32 KiB 客体空间；汇编在进入 C 前拒绝不足的空间。这是当前 sanitizer 运行库的已测试策略，不是适用于所有运行库的通用栈上界。原有 64 KiB 分配下限不变，生产构建不采用此 sanitizer 专属余量限制。

实现者在 16 KiB ARM64 Mac 上验证：启用 fake-stack 检测的已配置 ASAN/UBSAN 测试 25/25 通过，九项相关 TSAN 测试全部通过。新核心测试以八个并发自有客体运行真实改写的 GetThreadId、TLS、阻塞 WaitSynchronization 及终止 ExitThread 门，检查 SP、x18、非易失寄存器／SIMD、客体与原生浮点状态；填满 128 槽注册表，验证失败、同句柄重试、复用及清理；通过使用备用信号栈的子进程验证 sanitizer 来源余量为零、32752 及精确 32768 字节的边界。Android 最终共享库链接也检查六个导入入口：公共入口必须绑定到本地，禁止会破坏 w17 导入编号的 PLT/JUMP_SLOT。日志及实现者报告位于 Git 外的 `../d2r-work/.omo/evidence/bridge-core-20261017/` 和 `build/bridge-owner-*`。

此栈桥尚不能证明更小的客体栈分配、任意采用 SCS 插桩的客体 C 回调、PAC 签名客体代码、完整 SDK 初始化或游戏就绪。原生服务 C 保持普通编译器 SCS/PAC 行为；支持的客体 ABI 仍为现有改写 ARM64 契约。SDK monitor-return 阻塞、后续服务／初始化准备及 NVN／游戏图形仍待完成。本工作未执行游戏指令或初始化器。

主会话对冻结源码的最终验收通过：启用 stack-use-after-return 检测的完整宿主 ASAN/UBSAN 测试 32/32（`build/bridge-host-tests.log`），TSAN 9/9（`build/bridge-tsan-tests.log`；关闭符号化以避开本机 atos 报告停滞）。导入测试包含真正经过插桩的客体 C 写入输入并读取原生服务输出。完整 NDK API29 构建及最终共享库六入口本地绑定检查通过（`build/bridge-android-build.log`）；四个运行时单元的 Clang 分析诊断为零。本轮无线 ADB 无法连接，因此没有新的手机运行结果，也没有新的 APK 或游戏执行结果。验收源码清单位于 `build/bridge-source-freeze.json`，共 116 个文件，不含文档。

## 检查点：有界小栈执行

生产构建现允许可信、已改写的 ARM64 汇编客体使用至少 4096 字节的分配。只读 `dm_host_thread_stack_minimum()` 和 `dm_host_thread_stack_entry_headroom()` 返回当前运行时的编译策略。生产模式要求精确初始 SP 下方至少有 1056 字节：受支持改写的最大固定帧为 256 字节 x18 跳板加 800 字节调用门；系统指令跳板为 16+800 字节。此预算不含客体存活帧、原指令的栈调整／访存语义和 SDK 顶部保留区。调用方须在每处改写边界保留足够空间。准入并非全程序栈深度分析或沙箱，也不允许任意宿主／插桩 C 回调在小客体栈上运行。

显式栈登记与 SDK 式自动租约共用分配下限。SDK 适配器在接受精确初始 SP 前额外检查 `size-top_reserve >= entry_headroom`，仍租用完整分配。生产 RUN 直接通过汇编进入客体，不再在客体栈调用 C 引导函数；正常服务继续先切到原生 pthread 栈再进入宿主 C。生产 ExitThread 直接从原生汇编离开 RUN，不调用客体侧 C 退休辅助函数。托管入口返回会陷阱。未解析导入先保存既有的 96 字节有界帧，再通过汇编陷阱；旧式入口返回路径也陷阱。运行时 ELF 终止目标绑定到本地，避免在客体栈进入动态链接器。独立调用门对象保留原分派行为；共享的 `host/entry.S` 未修改。

ASAN/TSAN 构建仍要求至少 65536 字节分配，初始 SP 下方至少有 33824 字节，即 1056 字节固定改写帧加既有 32768 字节 sanitizer 来源钩子策略。sanitizer 引导、平衡的正常／终止 fiber 切换及原生服务插桩均保留。这些是当前运行库的已测试准入限制，不是 sanitizer 用栈的通用上界。堆／别名页规则未变：16 KiB Mac 测试在带原生页保护的分配内登记精确 4/8 KiB 逻辑栈，以哨兵检查其余字节；不宣称该宿主提供 4 KiB 物理保护。

实现者发布构建矩阵实际执行 4/8/16/32/64 KiB 客体，运行改写的 SVC/TLS/普通 x18 指令及六种导入，两个并发阻塞读取分别位于不同的原生服务栈。显式与 SDK 式栈保持精确 SP、虚拟／物理 x18、寄存器、向量和浮点状态；完整分配租约覆盖阻塞期，终止完成后释放。此测试中所有生产场景均恰好触及 1056 字节，包括 SDK 顶部保留区使可用空间恰为 1056 字节的场景；1040 字节被拒绝。ASAN 拒绝 4–32 KiB 且不修改输出，执行 64 KiB（含精确 33824 字节可用空间），拒绝 33808 字节。实现者发布及 ASAN/UBSAN 测试各 26/26 通过。独立 4 KiB 返回／未解析导入／旧式返回子进程产生 SIGTRAP。共享链接检查覆盖全部 4096 个未解析桩及两条终止路径；外部默认可见性负对照按预期失败。证据位于 `../d2r-work/.omo/evidence/small-stack-core-20261017/` 和 `build/small-stack-owner-*`。

上述结果覆盖有界改写汇编 ABI，不包括任意 PAC 签名／SCS 插桩客体回调、重入、SDK monitor-return 放行、完整 SDK 初始化或 NVN／游戏图形。没有执行真实游戏代码或初始化器，本轮也没有新的设备／APK 运行结果。

冻结源码的最终验收通过：发布构建 26/26（`build/small-stack-release-tests.log`）、启用 stack-use-after-return 检测的完整 ASAN/UBSAN 33/33（`build/small-stack-host-tests.log`）、TSAN 10/10（`build/small-stack-tsan-tests.log`）。完整 NDK API29 构建通过，含最终共享库六导入入口及 4096 个未解析桩／终止路径的两项检查（`build/small-stack-android-build.log`）。bridge、heap、sync 三个静态分析报告的诊断数均为零。验收清单为 `build/small-stack-source-freeze.json`，共 118 个文件，不含文档。无线 ADB 仍无法连接；Android 证据仅为交叉构建及共享 ELF 验证，没有新的手机、APK 或游戏执行结果。

## 检查点：NVN RGBA8 Vulkan 纹理资源

`dm_vk_texture_upload` 现可接收经过验证的 NVN 块线性 RGBA8 UNORM（`0x25`）2D／2D 数组描述，通过现有共享辅助代码逐 mip／层解块，按需刷新非一致宿主可见暂存内存，再上传到最优平铺 Vulkan 图像。返回的图像／视图以着色器读取布局覆盖所需 mip／层，2D 与数组保持各自视图类型。格式特性、图像用途限制、分配类型以及全部源／存储大小均有检查。描述准入要求尺寸／深度／级数为正、级数不超过自然 mip 链、flags／stride 为零、samples 为 0 或 1、打包块高为 0–6；在上游布局运算前先限制每维最多 16384 纹素、最多 2048 层，再受设备上限约束。不支持的格式、sRGB、压缩、3D／立方体、多重采样及其他标志明确拒绝。

调用仍由宿主外部串行化，并同步完成。失败时输出不变，确认 GPU 完成后释放已取得资源；返回纹理的后续 GPU 使用也必须在销毁前完成。共享提交辅助函数现区分提交失败与完成等待失败：无法确定完成时回退到设备空闲等待，并保留原始错误。如果回退仍不能确认完成，则终止进程，避免销毁可能仍在使用的资源；尚无延期清理队列。提交返回设备丢失仍须执行完成操作，等待返回设备丢失则允许清理。

Apple M4／MoltenVK 的真实 GPU 测试逐一比较独立构造的 1×1、13×11 mip 链、65×37 三层和 37×19 打包块高样本，共 11,509 个 RGBA 纹素。在第 2 层／第 6 级破坏一个字节，会在预期位置产生精确像素不匹配。生命周期样本执行真实上传／缓冲／提交代码，逐一注入 16 处失败，检查输出不变、资源所有权归零、非一致内存刷新及两个图像屏障。独立提交测试覆盖提交前错误、OOM、设备丢失、未知提交失败和终止顺序；开启 NDEBUG 的 Release 反例能检测到先清理后终止的错误。实现者 Release 测试 3/3 通过（`.omo/evidence/vulkan-texture-core/tests.log`）。宿主未安装 Khronos 验证层，因此这些结果不声称具备验证层覆盖。

这仍是使用自有合成着色器和源字节的宿主后端 API。客体 NVN 对象／导入绑定、更多格式与更新、翻译游戏着色器／描述符／绘制集成和真正的 swapchain 呈现仍待完成。此次没有向小客体栈加入直接 C 调用。未执行游戏指令或初始化器，未安装 APK，无线 ADB 仍不可用；当前纹理运行证据是 Mac GPU 执行与 Android 交叉编译，尚非 Adreno／设备验收。

集成验证先通过完整 Release 29/29（`build/texture-release-tests.log`）和完整 ASAN/UBSAN 35/35（`build/texture-host-tests.log`）。最终回滚样本与提交检查加入后，重新构建全部四项 Vulkan 测试，在 Release 和 ASAN/UBSAN 下均 4/4 通过（`build/texture-final-release.log`、`texture-final-sanitized.log`）。最终完整 NDK API29 构建通过，包含新增纹理与故障测试 CLI 目标以及既有共享入口检查（`build/texture-final-android.log`）。改动的 Vulkan 核心分析报告均为零诊断。最终源码清单为 `build/texture-source-freeze.json`（125 个文件，不含文档）；此前 TSAN／运行时结果仍属于前轮证据，不是本轮纹理驱动并发认证。

## 检查点：同步等待取消与线程优先级

`CancelSynchronization`（SVC `0x19`）现可取消已登记线程正在进行的 WaitSynchronization，或保留一次待取消标志，供下次阻塞同步等待使用；重复请求合并。已就绪对象和零超时轮询优先于待取消标志。首次完成结果在宿主互斥锁内固定，后续 signal／reset／cancel 不能覆盖已选结果；稍后的取消可留给下一次等待。取消不终止线程，也不中断地址仲裁／互斥／条件等待；已测试的地址仲裁场景会保留请求，直到随后进入同步等待。句柄引用使阻塞等待状态在 close 后仍保持有效，过期句柄和事件句柄则拒绝。

`GetThreadPriority`（SVC `0x0c`）在句柄仍有效时返回已登记、未启动或已完成线程对象的存储虚拟优先级。当前线程伪句柄要求匹配且已登记的上下文。查找使用 w1 的低 32 位，改写门以 x0 返回 Result、x1 返回优先级；错误时保留完整输入 x1 是本宿主已测试的策略。此 getter 不增加优先级继承或物理调度控制。独立测试覆盖优先级 0／17／63、过期代次、错误宿主上下文及实际原生服务 SP。取消测试使用真正改写门和纯汇编客体，在生产 4 KiB 栈及 sanitizer 64 KiB 栈上执行，覆盖启动前／活动等待取消、精确 SP／x18 和终止退出；没有增加客体栈上的宿主 C 路径。

完整集成测试通过 Release 31/31、ASAN/UBSAN 37/37 和 TSAN 11/11（`build/sdk-startup-{release,host,tsan}-tests.log`）。新增优先级目标后，`horizon_priority` 在三种模式下分别 1/1 通过（`build/sdk-startup-final-{release,sanitized,tsan}.log`）。完整 NDK API29 构建和最终优先级目标构建均通过（`build/sdk-startup-android-build.log`、`sdk-startup-final-android.log`），ELF LOAD 按 16 KiB 对齐。两个改动 C 单元的分析诊断均为零；i18n 检查通过 372 行表及 460 条白名单中文字面量（`build/sdk-startup-i18n.log`）。源码冻结清单为 `build/sdk-startup-source-freeze.json`（128 个文件，不含文档）。

SDK 启动仍被阻止。新增展开／重定位元数据将监视器返回的证明边界缩小到运行时对象／虚表目标，但不能封闭间接入口，也不能支持放宽扫描器（`.omo/evidence/sdk-entry-proof/report.md`）。只读启动清单明确列出 GetInfo ID 2／3／16／21／22、虚拟核心掩码行为、SetThreadCoreMask 及后续服务／IPC 初始化等仍待定义的有界约定（`.omo/evidence/sdk-startup-audit/startup-readiness.md`）。优先级 getter 是已确认的构造函数依赖；支持取消本身不证明启动前必需该服务。本轮未执行真实游戏指令或初始化器，也没有当前手机／APK 运行结果。


## 检查点：进程别名地址预留区

`dm_host_create_process_layout(heap_capacity, stack_capacity, alias_capacity)` 新增可选的连续堆／栈／别名预留布局。三个容量均非零；堆按 2 MiB，栈与别名区同时按宿主页和 4 KiB 对齐，完整跨度的运算均检查溢出。GetInfo 2/3 报告真实别名区，ASLR 大小 13 包含三段。别名区保持 PROT_NONE：QueryMemory 报告精确空闲范围，可写／可执行登记、读写 pin、线程栈登记及 MapMemory 目的地均拒绝该区。已有堆到栈映射保留保护与租约语义；两个旧构造器维持原有额外 ID 拒绝及空闲区合并行为。

GetInfo 16 返回零字节，因为此模型没有专用客体进程系统资源池。SDK 只读元数据确认，该函数的零输出分支跳过池管理器构造并重新进入解锁路径；这不能证明完整 SDK 初始化或所有下游行为。ID 21/22 继续明确不支持，直到客体预算、模块／主栈所有权、借用登记、TLS／TLR 计费及共享后备去重形成一致的记账约定。不会将堆提交量或宿主 RSS 代替进程物理内存用量。此次预留不增加 MapSharedMemory 或 MapPhysicalMemory 服务。GetInfo 使用低 32 位 ID／句柄及完整 64 位子类型；错误时保留完整输入 x1，沿用已验证的宿主策略。

独立样本检查精确区间边界、受控子进程中的读写保护故障、部分重叠拒绝、已有栈别名、堆调整回滚／归零及整个预留区的释放。真正改写的 SVC 0x29／0x06 调用验证 x0／x1 结果和原生服务栈范围。最初的释放检查误以为 Darwin mincore 会拒绝未映射地址，现改用只读 Mach 区间查询，Linux 仍用 mincore；此次测试修正不需要修改运行时。实现者 Release 进程内存与旧内存测试 2/2 通过（`.omo/evidence/process-memory-core/tests.log`），逐场景输出见同目录 `direct.log`。

严格 SDK 监视器返回拒绝仍保持不变。本轮仅验证自有合成运行时，没有执行游戏指令／初始化器，没有当前手机执行或 APK 安装。约定与重放命令记录在 `.omo/evidence/process-memory-core/`；主要语义与精确禁用资源池分支审查见 `.omo/evidence/process-memory-review/`。

冻结后的完整集成测试通过 Release 33/33、启用 stack-use-after-return 检测的 ASAN/UBSAN 39/39，以及 TSAN 13/13（`build/process-memory-{release-tests,host-tests,tsan-tests}.log`）。完整 NDK r29／API29 构建通过，含新增按 16 KiB 对齐的 CLI，以及六导入跳板和 4096 个未解析陷阱的两项共享库检查（`build/process-memory-android-build.log`）。heap／memory 静态分析均为零诊断（`build/process-memory-static-analysis/`）。冻结清单记录 129 个非文档文件（`build/process-memory-source-freeze.json`）。这些交叉构建结果不代表当前 Android 设备验收。


## 检查点：自有客体后备预算

`dm_host_create_accounted_process(heap, stack, alias, budget)` 新增独立严格模式，强制执行按页对齐的客体预算。GetInfo 21 返回该上限；22 统计已提交堆、纳入所有权的模块后备，以及仍存活或因 pin 保留的退休 TLS／TLR 映射。别名共用原堆计费。地址预留、宿主实现元数据、原生服务栈和私有装载缓冲区不计入已用客体后备；这不是 RSS，也不声明宿主页是否驻留。旧构造器继续拒绝 21/22。

宿主拥有的 `dm_host_map_modules` 包装器调用真实多模块链接／映射代码，复用准确的页面布局计算来预检预算并记录权限。映射器会先将整个跨度归零，再保护空洞和各段，因此计费覆盖完整映射及附加跳板。只有链接／保护全部成功后才原子发布；失败保持输出和用量不变，并将已尝试的子区恢复为 PROT_NONE。返回视图只可借用至宿主销毁，调用方不得自行取消映射或更改保护。严格模式拒绝未计费的外部客体区域登记和未知指针 pin；自有 RO 读取、RW／RX 页面及不可访问空洞维持真实权限。客体栈必须使用已提交堆或其栈别名。

客体 TLS／TLR 使用专用的按页取整分配，QueryMemory 将其报告为 ThreadLocal（`0x0c`）。附着失败回滚分配和计费。拥有者分离时若另一线程仍持有 pin，映射及计费保留到最后一个 pin 释放；退休映射拒绝新 pin。TLS 支持显式读写 pin 和 QueryMemory，但不支持通用可写检查服务，例如以 TLS 为后备的互斥／仲裁字或客体栈。双线程固定缓冲区测试覆盖分离、保留计费、载荷访问及最终完整跨度释放；这是 pin 原语测试，不是 RomFS `pread` 集成测试。

初版记账实现的完整集成测试通过 Release 34/34、ASAN/UBSAN 40/40 和 TSAN 14/14（`build/block-one-{release,asan,tsan}-tests.log`），完整 NDK API29 构建及已有共享库入口检查也通过（`build/block-one-android-build.log`）。针对性样本链接两个自有合成模块、执行改写后的 GetInfo 21/22，并检查 RO／空洞边界、预算耗尽、别名去重和并发 TLS 生命周期（`.omo/evidence/accounting-core/ctest.log`）。针对初版记账实现的独立外部反例去掉 TLS pin 保留后，会在保留计费断言处失败（`.omo/evidence/block-one-review/tls-control/`）。

最终布局将同一个自有预留区扩展为堆 | 栈 | 别名 | arena，arena 容量等于强制预算。模块占用不可变前缀，最多 128 个 TLS 槽使用剩余部分。GetInfo 12/13 现包含全部自有客体映射；QueryMemory 在报告空闲区域前先查询 arena 权限记录。借用模块视图拒绝公开模块 unmap。提交失败或 TLS 退休后，仅将精确子区重置为 PROT_NONE，持续保留父区所有权，绝不先取消映射留下内部空洞。重置失败会终止，避免返回不一致预留状态；最终销毁一次释放完整预留区。针对性测试覆盖全部地址包含关系、TLS 槽归零复用，以及十一处逐一注入的保护失败和成功重试（`.omo/evidence/accounting-core/unified-test.log`）；独立模块测试仍通过（`unified-module-test.log`）。针对统一布局源码，独立 pin 保留反例也已重跑：正例退出 0，变异版本在工作线程释放前失败（`.omo/evidence/block-one-review/tls-control-unified/`）。初版源码／验证来源另存于 `build/block-one-accounting-initial-source`。

这些结果尚未完成安全 SDK 启动。严格监视器返回阻断、已验证的启动 ABI／前置条件、剩余服务以及真实初始化／生命周期验收仍是独立要求；包含全部自有映射并不等于满足所有 Horizon 区域策略。记账样本为合成样本；下述另行批准的真实 SDK 诊断里程碑仅覆盖一个初始化阶段。Android 证据仍仅为交叉构建。记账约定和重放命令见 `.omo/evidence/accounting-core/`。

最终统一布局集成通过 Release 34/34、启用返回后栈访问检查的 ASAN/UBSAN 41/41，以及 TSAN 运行时 14/14（`build/block-one-final-release-tests.log`、`block-one-fixed-asan-tests.log`、`block-one-final-tsan-tests.log`）。此前组合 ASAN 测试发现诊断备用信号栈清理问题，修正后的完整运行才是验收证据。完整 NDK API29 编译及共享入口检查通过（`block-one-final-android-build.log`）；六个变更核心／映射单元静态分析均为零诊断。137 文件源码冻结及精确 QA 索引为 `build/block-one-source-freeze.json` 和 `block-one-final-qa.json`。这些结果不包含本轮 Android 设备或 APK 执行。

独立宿主诊断的准备结果保持不透明、不可导出。它将由通用验证 FDE 确定、包含未证明独占根的整个范围逐条隔离，再对私有副本应用未改动的严格准备器。自有三模块样本验证受控入口／返回、每个隔离入口停止、未知 SVC／子类型拒绝、低 SP 信号处理、有界跟踪溢出及看门狗终止。样本在仅允许 scratch 写入、禁止网络的限制下通过；反例控制确认非 scratch 文件创建、bind 和 connect 均被拒绝（`build/block-one-restricted-{diagnostic,controls}.log`）。这种诊断变换不是生产验收，也不是保持语义的独占监视器修复。

第一次真实输入检查仅作准备：main/subsdk0 通过严格准备；SDK 仍有一个未匹配独占根并保持严格拒绝。其私有副本经一个范围隔离后诊断准备成功，前后输入哈希一致（`.omo/evidence/sdk-stage-diagnostic/actual-prepare.log` 及 `actual-input-{before,after}.sha256`）。该检查没有真实模块映射、指令执行或产物序列化。该准备结果本身并未授权执行。

经过独立 ABI／隔离审查后，受限 macOS 诊断依次执行 `_init_libc0` 和 `nnosInitialize(own_handle, 0)` 并返回。子进程退出 0，产生 19 条跟踪记录及返回、清理标记；托管 ExitThread 和宿主清理均完成，没有隔离范围停止、不支持服务停止、信号或超时（`build/block-one-real-stage-run.log`）。三个原始输入的运行前后哈希检查全部通过（`block-one-real-stage-input-{precheck,postcheck}.log`）。观察到的 GetInfo ID 为 2、3、11、12、13、14、15、21、22；该运行不证明 ID 16 或后续服务覆盖。

这仅是在私有诊断映射中执行一个真实 SDK 阶段。生产严格准备仍拒绝原始 SDK 输入。后续 libc／SDK 模块初始化、必需 IPC 与其他服务、完整生命周期及 Android／设备验收仍未验证；第一大块尚未完成。

## 检查点：有界本地命名端口与同步 IPC 传输

对未修改 SDK 映像的只读分析定位了停止边界：SDK 中只有一个 `svc #0x1f` 站点（sdk+0x39fb80），唯一调用者 `nn::sm::Initialize`（sdk+0x38736c）通过 x1 传入 rodata 字符串 `sm:`。上次诊断记录的 r0 是调用方桩保存的 C 出参指针，不是端口名。下一步传输调用是 sdk+0x385040 的 `nn::sf::hipc::SendSyncRequest`：它把消息指针与 `tpidrro_el0` 比较后使用 `svc #0x21`，因此隐式缓冲区就是线程 TLS 槽。

`runtime/ipc.c` 只实现有界原始寄存器传输。`svc #0x1f` 消费 x1（已登记本地端口名）并返回 w0/w1；x0 留给库桩，运行时从不解引用。`svc #0x21` 在宿主锁内复制调用线程的 0x100 字节 TLS 缓冲，只接受两种已由 SDK 数据流证明的无描述符形式：type4/CMIF v0，以及真实首包——type6/CMIF v1 加 send-PID 特殊头（PID 字节永不可信）。成功时原子发布完整构建且零填充的 8 + num_data_words*4 字节响应；其他形式返回 0x4201 且不发布任何字节。类型7/CMIF v1、8 数据字的控制消息支持两条已证明命令：3 QueryPointerBufferSize 返回本宿主真实的 u16 容量 0（当前没有指针缓冲分配）；0 ConvertSessionToDomain 为每个会话分配一个真实的 32 位根对象 ID，重复转换返回同一 ID，且仅在响应成功写入后提交；16 字节对齐填充中的非零字节不被拒绝，未知命令、版本和描述符形式保持明确 unsupported。转换后，在域请求路由实现之前，普通未包装请求返回 0x4201，且不会触达原端口处理器。本地 `lm` 域端口已安装真实的每会话根对象与最多八个有界 logger 子对象：OpenLogger（命令 0、14 数据字 type6/send-PID 帧）返回 52 字节 SFCO 响应和一个 32 位子对象 ID，域关闭（type 2、8 数据字帧）只回收该子对象；失败会回滚未发布的子对象。根与子对象共享每会话唯一且不复用的 ID 命名空间；根关闭只退休服务根（存活子对象仍可关闭，拒绝重新转换/OpenLogger），内核 `CloseHandle` 才销毁会话及全部子对象。Log 与 SetDestination 保持明确 unsupported，因此这是资源获取/关闭，不是完整日志服务。会话复用既有 128 槽代际句柄表并新增种类；`CloseHandle` 在访问同步对象之前甄别会话；畸形请求按 `SendSyncRequest` 错误的既有规则关闭会话。`runtime/sm.c` 提供真实 sm:：RegisterClient（命令 0）与 GetServiceHandle（命令 1）；登记状态属于各自宿主，GetServiceHandle 为本地已登记端口返回经 HIPC move handle 发布的真实会话句柄，未知服务保持明确 unsupported，发布失败会回滚句柄。这是传输加两条真实 SM 命令，不是 SDK 启动。

自有夹具覆盖原始 x0/x1 ABI、带不可信 PID 字节的真实 type6/send-PID RegisterClient 包、type7/SFCI-v1 控制（查询、转换、重复转换、错误命令／版本／描述符／数据字数、关闭句柄）、相同代际句柄的双宿主隔离（销毁一个宿主不影响另一个的登记）、GetServiceHandle 向真实已登记端口发布 move handle 并随后完成请求/响应、未知服务 unsupported、真实请求/响应与空负载、确定性响应填充与有效长度、畸形／描述符／错误对齐包、非法与非客体名称指针、未登记端口、错误种类操作（ClearEvent、ResetSignal、优先级、线程 ID、等待）、关闭与陈旧代际、重入 BAD_STATE、派发期间关闭、32 会话耗尽／回滚以及宿主销毁 EBUSY。Release 与 ASAN/UBSAN 通过，既有同步与计费夹具仍通过，运行时可在 Android arm64-v8a 编译。重新引入 x0 消费、错误 CMIF 偏移或不可达会话关闭缺陷的负向对照都会让同一夹具失败。没有真实 SDK／游戏执行，也没有设备运行。RegisterClient 与 GetServiceHandle 已实现并通过夹具；其他服务与真实启动仍未实现。
