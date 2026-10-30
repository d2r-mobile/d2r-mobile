# Linux 构建机与首批发现（2026-10-16）

English: [2026-10-16-linux-build.md](2026-10-16-linux-build.md)

第二位贡献者从 `main` `记录版本` 起步的记录：Windows 11 + WSL2（Ubuntu 18.04）+ Docker，分支 试验构建，时间为新西兰夏令时（UTC+13）。本构建尚未上真机；以下都是构建或读码结论，不是可玩性结论。

## 在 Linux / WSL 上用 Docker 构建

`dm/android/docker/` 新增钉死版本的构建镜像与启动脚本，不必在 Windows 侧装 Python、NDK、CMake 即可出 APK：

```sh
docker build -t dm-android-build:1 dm/android/docker
DM_GEN_DIR=/绝对路径/宿主表头目录 dm/android/docker/build.sh
```

- 镜像钉死：Ubuntu 24.04（按摘要）、apt 按 2026-10-10 快照、JDK 17、命令行工具 16111833（SHA-1 取自 Google 仓库清单）、`platforms;android-36`、`build-tools;36.0.0`、`ndk;29.0.14206865`、`cmake;3.31.6`、Gradle 9.8.1（SHA-256）。约 3.6 GB。
- 容器以宿主用户身份运行。Gradle 缓存放在具名卷，调试签名钥匙放在宿主目录（`~/.android-dm`）。首次运行时，若 Windows 版 Android Studio 有 `debug.keystore` 就拷来，两边出的 APK 可以互相覆盖安装。
- sdkmanager 解出的可执行文件权限是 744（只有 root 能跑），镜像在同一层补 `chmod -R a+rX`。不补的话，第一次构建报 "Cannot run program …/cmake: Permission denied"。

**在 Linux 上准备的输入**（容器内 Python 3.12、Pillow 12.1.1、lz4、capstone 5.0.x），取 `记录版本` 的 `git archive` 来跑，工作树不受影响：

| 步骤 | 用时 | 结果 |
|---|---|---|
| `make -C dm/host dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc` | 2 分 54 秒 | 46,551 / 1,250 / 8,010 字节 |
| `DM_TARGET=android DM_RULESET=touch-run-v4 patch_nso.py` 改写 `main`、`sdk`、`subsdk0` | 2 分 21 秒 | 代号 `3.3.94012-r44`，每个 `fixups.tsv` 都有 `# TARGET 0x1` |

改写计数，供其他机器用同一份原版 ExeFS 对比：

| 模块 | slot_refs | 门 | cuts | obs | patched.bin SHA-256 |
|---|---:|---:|---:|---:|---|
| main | 25,127 | 36 | 48 | 21 | `27c5c83d29096d5eb365f4067e7c0e702bacf201fc42f04ec48166330949c49a` |
| sdk | 12,085 | 110 | 0 | 0 | `9b33cf9850ce5bc648cd0932ddb79103ecc50b98350bf42f286a63a242d99196` |
| subsdk0 | 10,486 | 0 | 0 | 0 | `ecc9d697f568ce16500c1b41bdabfce621e70610fad49ab0084bd0b4185d5f3a` |

`main` 记录了缺少启动预取清单（公开树本来就没有，属预期）。

**构建结果（15:07）：** 用上述表头跑 `:app:assembleDebug :app:lintDebug`：BUILD SUCCESSFUL。冷构建约 3.5 分钟，此前建镜像 2 分 47 秒。
- APK 20,468,833 字节，SHA-256 `f359339fa2efd92133b74f50b472c39ce4144bf259ab3d2b489e7a20d11954e3`，用本机调试钥匙签名。`lib/arm64-v8a/libdm.so` 为 16,659,904 字节。
- Lint：0 错误、38 警告，主要是小组件布局（`SmallSp` 16 条、`RtlSymmetry` 4 条）。
- C/C++：128 条警告，其中 111 条是 `uint64_t` 配 `%llx`/`%llu`，在 arm64 上宽度相同。
- 有一条是真的（只影响日志）：`loader.c:240` 用 `%lu` 打印 `pthread_key_t`，该类型在 bionic 上是 `int`，所以打印出的四个键值高 32 位不确定。

**不需设备的检查：**
- `dm/android/tests/lifecycle_dispatch_test.c` 用 x86-64 Linux 的 gcc 7.5 编译运行通过。
- `TextInputRulesTest` 与 `SaveSummaryReaderTest`（27 项合成检查）用镜像内 JDK 17 运行通过。

## 发现

1. **本仓库无法生成 `.xgs` 着色器包。** 引擎从着色器目录读取 `<key>.xgs`（`xbg_game.cpp:1110-1125`），格式为 `xbg_shader_header`（magic `XGS1`、版本 1、阶段、绑定、输入、c1 字节、SPIR-V；`xbg_game.h:81-87`）。仓库里没有任何脚本、工具或文档会写出这种文件，`dm/android/README.md` 只写了"与游戏和 Vulkan 后端匹配的外部 `.xgs` 包"。新贡献者能编出 APK，却无法仅凭本仓库给设备备齐运行文件。请问团队：生成工具在哪里（鸿蒙那边的工具？），能否把它或格式说明提交到本仓库？
2. **改写校验不覆盖 `DM_TARGET=android`。** `dm/verify/structck.py`（第 ⑥、⑧ 项）和 `equiv.py:84` 认的是苹果的槽序列（`mrs tpidrro_el0` + 键页），安卓产物是 `mrs tpidr_el0; ldur [x,#-8]`（`patch_nso.py:286`）。所以 `regress.sh` 无法校验安卓模块，目前只能靠上面的哈希互相对照。
3. **引擎只要 Vulkan 1.1，清单和 README 却要 1.3。** 实例请求 1.1（`vulkan_present.cpp:139`），引擎只要求 1.1（`xbg_vulkan.cpp:154`，"SPIR-V 1.3"）；`AndroidManifest.xml:6` 声明的 `android.hardware.vulkan.version` 为 `0x403000`，即 1.3。此前在 Redmi K50 Ultra（骁龙 8+ Gen 1、Adreno 730、HyperOS 3、Android 15）上的探针报告为 Vulkan **1.1.128**、驱动 512.615、子组 64 路、无 `shaderFloat64`、无片元着色器互锁。这台手机是下一个测试目标。为 Adreno 830 翻译的着色器包可能声明了在那里会被 `validate_capabilities` 拒绝的能力。
4. **App 在 USAP 下启动时间线零点错误。** `main.c` 从 `/proc/self/stat` 读进程创建时刻。安卓可能预先分叉 App 进程（USAP 池），此时该字段是预分叉时刻。在 Redmi 上它读出 **8,422 秒**，而 Java 的 `Process.getStartUptimeMillis()` 为 **3.3 秒**。所有 `[x.xxs]` 日志时间和启动剖析的 ⓪ 段都偏了这么多。修复见 PR（分支 试验构建）。
5. **App 不链接 `.holes` 区段表，上游给测试者的剔零资源包会出错。** `jni_android.c` 的 `link_romfs` 只链接 `*.data` 和 `build.config`。`hle_fs.c` 在 `romfs/data/` 下找 `<档案>.holes`，找不到就按物理偏移读那四个剔零档案。修复见 PR（分支 试验构建），设备证据见下。

## Redmi K50 Ultra 上机（15:15–15:35）

Redmi K50 Ultra，骁龙 8+ Gen 1、Adreno 730，HyperOS 3 / Android 15（API 35），USAP 池已开。APK 由本分支构建（已并入 改动），用本机调试钥匙签名。

| 步骤 | 结果 |
|---|---|
| `adb install`（首次安装） | 成功。清单里的 `android.hardware.vulkan.version` 1.3 并不阻止往这台 Vulkan 1.1 设备侧载 |
| 所有文件访问 | `appops set --uid com.xiaoboluo.d2r MANAGE_EXTERNAL_STORAGE allow` |
| 游戏文件 | 上游剔零包（18 个档案、4 个 `.holes`、`build.config`）推到 `Download/data`，用时 2 分钟 |
| 模块 | r44 安卓 `out/`（24 个文件，79 MB）用 `run-as` 拷入 `files/dm/out/` |
| 无着色器启动 | 链接 23 个文件，停在 `boot: no shader bundles`（报错界面，符合设计） |

为了在真着色器到来前检查宿主，有两次启动放了一个**空占位** `.xgs`，事后已删除。它只能骗过启动器的目录检查，第一次真正加载着色器时必然失败。
- **带 改动：**
  - Vulkan 引擎在 Adreno 730 上初始化成功：`NVN caps api=1.1.128`、交换链 2712×1220、`mirror-clamp-to-edge` 已启用。
  - 模块用 bionic 键表槽装载（`bionic 键表偏移 0`），重定位完成，264 条强制接管全部命中。
  - SDK 初始化与 `nnMain` 运行，客体堆 3,728 MB。
  - RomFS、临时目录与事务式存档挂载成功，`hle_fs.c` 登记了全部四个 `.holes` 表。
  - NVN 窗口 2000×900，停在游戏第一次请求着色器处：`Missing genuine shader bundle …/a1217a07d94c1184.xgs`。
- **不带 改动**（同样数据，先删掉残留链接）：链接 19 个文件，三次 `tact：zlib 解码失败`，随后在 `main+0x1b692dc` 处 `CRASH sig=11`，此时还没请求任何着色器。
- **启动时刻（改动）：** 按 `/proc/<pid>/stat`，本 App 自己的进程被记成启动前 21,668 秒（约 6 小时）创建，因为它是 USAP 池预先分叉的。

手机上现为 改动 的构建，没有占位文件；存档里有这几次运行生成的缺省 `Settings`。

## 下一步

the project 正在为 Redmi K50 Ultra 生成 `.xgs` 包（发现 1）。拿到后放进 `files/dm/shaders/`，再按 `dm/android/README.md` 的设备验收清单逐项验证（出图、建角、进局、NPC、背包 / 技能、声音、后台 / 恢复、存档 / 读档），并测 Adreno 730 上的帧率。
