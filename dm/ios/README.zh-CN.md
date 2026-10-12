# dm/ios — iOS 外壳

English: [README.md](README.md)

D2R Mobile 的 iOS App：把宿主（`dm/host`）、图形层（`dm/gfx`）和改写后的游戏程序装进一个 App，负责启动、找游戏资源、把触摸变成游戏能懂的输入，并在原版界面上摆出触摸控件。

先读这几份，本文不重复它们：

- 需要什么、怎么从零构建：[../../README.zh-CN.md](../../README.zh-CN.md)、[../../setup.sh](../../setup.sh)
- 依赖与版本：[../../docs/DEPENDENCIES.zh-CN.md](../../docs/DEPENDENCIES.zh-CN.md)
- 游戏文件怎么准备、放哪：[../../docs/GAME_FILES.zh-CN.md](../../docs/GAME_FILES.zh-CN.md)
- 改写器、宿主、图形层，以及「从你的游戏文件生成资源」：[../README.zh-CN.md](../README.zh-CN.md)

文中 `$DM_ROOT` 指工作区根目录（缺省 `~/dm-work`）。命令里的相对路径以 `dm/ios` 为准，除非另有说明。

## 怎么工作

### 构建时把什么装进 App

iOS 不允许普通 App 执行运行时才写进内存的代码，所以游戏程序不能像在 Mac 宿主上那样启动时再装载，而是在构建时就编进 App 的可执行文件：

- `tools/gen_embed.py` 把改写后的模块映像（`main`、`sdk`）生成一份内嵌汇编 `Gen/embed.S`，按原来的相对布局放进可执行文件的段里。宿主的装载器在 iOS 上只取这些段的地址并核对布局，不拷贝代码、不改页权限。
- `tools/gen_build.py` 从 `dm/host/Makefile` 实际展开的编译命令里取宿主的源文件、头文件路径和框架，生成 `Gen/dm.xcconfig` 与 `Gen/src/`。宿主的 `main.c` 以 `-Dmain=dm_main` 编入，由 App 在自己的线程上调用。
- 着色器库（`setup.sh` 翻译好的 iOS 版）、着色器映射表、装载用的重定位表与门表、覆盖层 RomFS（见下）作为资源随 App 走。

工程文件不进仓库：`project.yml` 是 XcodeGen（由一份 YAML 生成 Xcode 工程的工具）的输入，`run.sh build` 每次重新生成 `DmIOS.xcodeproj` 和 `Gen/`。

### 启动

入口在 `Sources/main.m`。App 用启动参数 `-mode <名>` 选要跑的内容，缺省是 `boot`（起游戏）。`boot` 模式的顺序：

1. 把进程的文件描述符软上限提到 4096（游戏进局后同时打开的文件数接近 iOS 的缺省上限）。
2. 开日志：`Documents/boot.txt`，逐行落盘；上一轮的日志改名成 `Documents/boot.previous.txt`。宿主写到标准错误的内容也进这个文件。
3. 查显卡是否支持 BC 压缩贴图。不支持的机型需要配套的 ASTC 补充包（`dm/tools/astc_pack.py` 的产物），找不到就弹框说明，不起游戏。补充包怎么做、怎么放进手机，见下文「不支持 BC 贴图的机型」。
4. 找游戏资源（下一节）。找到就准备环境：`boot_prepare` 设好 `DM_ROMFS`（资源根）、`DM_ROMFS_OVERLAY`（覆盖层）、`DM_SAVE`（存档目录 `Documents/save`）、`DM_TEMP`、`DM_MSL_DIR`（着色器库）等宿主要读的环境变量，把工作目录切到 `Documents`，并调 `dm_host_preload_start` 让宿主在后台线程先开始装载，与 UIKit 的启动并行。
5. 场景建好后，根视图换成 `front_ios_view` 返回的 Metal 视图（`Sources/front_ios.m`，宿主前端接口 `dm/host/front.h` 的 iOS 实现）。
6. `boot_thread` 在一条 64 MB 栈的线程上先做 iCloud 存档同步（`Sources/cloud_ios.m`），再调 `dm_main` 进入宿主，从此游戏开始运行。

切后台、回前台、场景断开等生命周期事件由 `main.m` 的 `SceneDelegate` 转给 `front_ios.m`；App 终止时调 `dm_host_shutdown` 让宿主有序收尾。

其余模式是诊断用的：`anon`、`mapjit`、`file` 三个探针分别试「匿名页写入后执行」「MAP_JIT 映射」「沙箱内文件映射成可执行」，用来确认上面那条限制；`tsd` 探针试线程局部存储的固定槽位；`msl` 逐个加载 App 内的着色器库；`blank` 只跑一个空白 Metal 清屏页（发热对照）；`thermal` 读一次系统温控档位就退出。

### 找游戏资源

`main.m` 的 `romfs_locate` 按下面的顺序找，先到先得；每一处都用 `dm_romfs_validate`（`dm/host/romfs_validate.c`）核对 18 个档案文件齐全、字节数对得上：

1. App 容器的 `Library/Application Support/romfs`。开发时用这一处，从 Mac 推进去：

   ```bash
   xcrun devicectl device copy to --device <设备标识> \
     --domain-type appDataContainer --domain-identifier <包名> \
     --source $DM_ROOT/data/switch/3.3.94012/Update/RomFS \
     --destination 'Library/Application Support/romfs'
   ```

2. App 的 `Documents`（在「文件」App 里可见）里直接放着档案文件的文件夹。文件夹叫什么都行，外面多套几层也认（4 层以内）；不完整时把缺的文件列在屏上。
3. 苹果托管资源包（Background Assets，随 App 安装下载的大文件包），包名 `romfs`。`RomfsDownloader/` 是它要求的下载扩展。
4. 都没有：不起宿主，显示资源界面（`main.m` 的 `AssetVC`），给出进度或原因，资源到位后点重试就地进游戏。

资源只核那 18 个档案文件：配置文件用 App 里覆盖层自带的那份，密钥与档案清单游戏不读，都不用拷。

`Sources/asset_dl.m` 实现了从一个 HTTP 地址直接下载资源的通路（清单由 `tools/asset_manifest.py` 生成）。仓库里的 `asset_pointers.txt` 没有登记任何地址，构建时也不传 `DM_ASSET_BASE` 的话，这条通路不会生效。

### 把触摸变成输入

游戏的 Switch 版只认手柄和触摸屏两种输入，外壳把手指动作翻译成这两样，再加上直接对游戏界面的点击。

- **收集**：`front_ios.m` 的 `DmMetalView` 统一接收全部 `UITouch`。
- **判定**：`Sources/dm_touch_controls.c` 是一份不依赖 UIKit 的纯 C 状态机（`dm_tc_*`）：哪根手指属于摇杆、技能钮、腰带格还是界面，点按 / 双击 / 长按 / 拖放各算什么，输出当前该按下的手柄键和左右摇杆的值。它可以脱离设备做单元测试（见「单测」）。
- **发布**：每次状态变化，`DmMetalView` 的 `pushFrom:phase:` 把结果一起交给宿主：`dm_touch_set`（原始触点，供游戏读触摸屏）、`dm_cursor_set_screen`（界面光标位置）、`dm_input_set`（手柄键与摇杆）。宿主在游戏读手柄时把它们落地（`dm/host/hle_hid.c`、`dm/host/dm_cursor.c`）。

按游戏当前所处的界面分三种情况，由游戏自己发出的界面消息驱动切换（`dm_tc_navigation`），不靠猜：

| 情况 | 外壳怎么处理触摸 |
|---|---|
| 前端页面（主菜单、选角、建角） | 不显示摇杆和技能钮；触点经 `dm_ui_touch` 变成对游戏界面的鼠标点击 |
| 局内、没开面板 | 左侧摇杆、右侧技能钮、底栏按钮各自变成手柄键与摇杆；点在世界里（怪、物品名、NPC、箱子）经 `dm_ui_world_tap` 交给宿主解析目标，点空地不发任何输入 |
| 局内、开着面板或对话 | 手指直接点界面：物品格上单击看说明、双击使用、长按、拖放；页签、按钮、面板外点按关面板。命中的是什么由宿主按游戏自己的面板树回答，前端据此决定发鼠标点击还是手柄键 |

### 触摸控件与原版界面的关系

原则是尽量用游戏自己的界面部件，外壳只补游戏没有的那一层：

- **底栏**的血球、蓝球、腰带、回城、功能入口是游戏原版的 HUD 部件，只通过覆盖层里的布局文件改了位置与缩放；经验条和格子上的数字由外壳画。详见 [NativeHUD/README.zh-CN.md](NativeHUD/README.zh-CN.md)。
- **摇杆与技能钮**由 `Sources/DmTouchOverlay.m` 用 UIKit 画在游戏画面上面。这一层只画不收触点。技能钮上的图标取自游戏的技能图集，当前绑的是哪个技能由 `Sources/dm_touch_game_bridge.c` 从运行中的游戏只读地取出来。
- **面板**全部是游戏原版的面板。覆盖层把原版布局里的手柄键位提示挪出屏幕、给面板加上关闭按钮、把提示文字里的手柄说法换成触摸说法（`tools/touch_layout.py`、`tools/touch_strings.py`）。

### 覆盖层 RomFS

覆盖层是 App 自带的一小份 RomFS：宿主读游戏文件时先查它，查不到再读完整的游戏资源（`DM_ROMFS_OVERLAY`，`dm/host/hle_fs.c`、`dm/host/hle_tact.c`）。这样改界面布局和贴图不用动玩家的游戏文件。

`run.sh build` 调 `tools/gen_romfs_overlay.py <原版 RomFS> Gen/romfs_overlay` 生成它：读你的游戏文件里的原版布局与字符串，套上仓库里的改动，写成一份新的 `.build.config` 加一个新数据档。

- 仓库里只存「改了什么」，不存原版内容：布局用结构补丁（`tools/layout_patch.py`），字符串表按键名替换并用原文的校验值核对版本。
- 新贴图写在几个游戏不再读取的旧贴图路径上，登记在 `dm/data/casc_overlay/borrowed_paths.tsv`。
- 有些功能开关要换一份覆盖层才生效：脚本会把这些开关的各种组合各出一份到 `Gen/romfs_overlay_variants/`，App 启动时按玩家已打开的开关挑对应的那份。
- 构建时设了 `DM_ASTC_PACK`（ASTC 补充包目录）才会另出 `Gen/romfs_overlay_astc*`，给不支持 BC 贴图的机型用。

### 不支持 BC 贴图的机型（A16 及更早的芯片）

这些机型的显卡不支持游戏贴图用的 BC 压缩格式，要另做一份 ASTC 补充包，出包时带上配套的覆盖层，再把补充包放进手机。补充包用你自己的游戏文件在 Mac 上生成，仓库里不带。完整流程写在 `dm/tools/astc_pack.py` 的文件头，下面是它的步骤（在仓库根目录执行，`D` 是你选的输出目录）：

1. 准备编码器 astcenc（ARM 的开源编码器，从源码编），路径用 `--astcenc` 或环境变量 `ASTCENC` 给。
2. 转码：`python3 dm/tools/astc_pack.py <Update/RomFS 目录> D/work`。
3. 写成数据档：`python3 dm/tools/casc_overlay.py pack <Update/RomFS 目录> D/pack D/work/repl.tsv 0-8`。
4. 收尾：`python3 dm/tools/astc_pack.py finalize D/pack`。它算出大小与校验值，并在 `D/pack` 下生成给手机用的那个文件夹（里面是 `manifest.json` 与 `data/`）。
5. 出包：把 `DM_ASTC_PACK` 设成 `D/pack` 的绝对路径，再照常 `cd dm/ios && ./run.sh build`。
6. 把第 4 步生成的那个文件夹整个拷进手机上这个 App 的文件夹（用「文件」App 或 Finder，和游戏资源文件夹放在一起）。

## 目录结构

| 路径 | 内容 |
|---|---|
| `run.sh` | 构建、装机、按模式启动（见下） |
| `project.yml` | XcodeGen 工程定义：App、资源下载扩展、界面测试三个目标，以及签名能力 |
| `local.env.example` | 本机配置样例 |
| `Sources/main.m` | App 入口：模式、日志、找资源、启动宿主、生命周期 |
| `Sources/front_ios.m` | 宿主前端的 iOS 实现：Metal 视图、触摸收集与发布、触点脚本、截图等测试开关 |
| `Sources/dm_touch_controls.[ch]` | 触摸判定状态机（纯 C） |
| `Sources/DmTouchOverlay.[hm]` | 摇杆、技能钮等自绘控件 |
| `Sources/dm_touch_game_*.c` | 从运行中的游戏读取技能绑定（只读） |
| `Sources/asset_dl.[hm]`、`cloud_ios.m`、`dm_update.m`、`dm_sharelog.m` | 资源下载、iCloud 存档同步、版本提示、分享诊断日志 |
| `Sources/TouchArt/` | 自绘控件的素材。仓库里只有摇杆图和技能图集的来源清单（原版路径与校验值）；技能图集本身在构建时从游戏文件生成 |
| `NativeHUD/` | 底栏说明与自己画的关闭按钮图 |
| `RomfsDownloader/` | 苹果托管资源包的下载扩展 |
| `TouchSmokeTests/` | XCTest 界面测试（真实触点、生命周期） |
| `docs/` | 触摸布局与技能绑定的规格文件（JSON）。生成脚本和单测按校验值引用它们，不要改 |
| `tests/` | 单元测试（见下） |
| `tools/` | 构建期生成脚本（`gen_*.py`）、冒烟、回归、测量工具 |
| `tools/uireg/` | 触摸界面脚本化回归，见 [tools/uireg/usage.zh-CN.md](tools/uireg/usage.zh-CN.md) |
| `Gen/`、`DmIOS.xcodeproj/` | 构建时生成，不进仓库 |

## 本机配置：local.env

签名信息和设备号不进仓库，放在本机配置文件里：

```bash
cp local.env.example local.env
```

| 变量 | 含义 | 什么时候要 |
|---|---|---|
| `DM_TEAM_ID` | 开发者团队号 | 出 App |
| `DM_BUNDLE_ID` | App 包名，自己起（反域名写法）。App 组与 iCloud 容器由它派生：`group.<包名>`、`iCloud.<包名>` | 出 App、所有冒烟与回归脚本 |
| `DM_DEVICE` | 真机的 devicectl 设备标识（`xcrun devicectl list devices` 的 Identifier 列） | 装机与真机脚本 |
| `DM_IOS_UDID` | 真机的硬件 UDID | 真机冒烟的电量检查、`tools/lifecycle.sh`、`tools/xtrace.sh` |
| `MINI_SSH` | 第二台 Mac（专门用来跑测试）的 ssh 目标 | 只有 `dm/tools/l3run.sh`、`dm/tools/mini_sync.sh` 用；没有第二台 Mac 就留空 |

脚本通过 `tools/local_env.sh` 读配置，按这个顺序找，**只读第一个存在的文件，不合并**：环境变量 `DM_LOCAL_ENV` 指的文件 → `dm/ios/local.env` → `~/.config/dm-ios/local.env`。后一个位置方便同一台机器上的多个工作树共用；注意工作树里一旦有了 `local.env`，共用的那份就整份不读了。

## run.sh

```bash
./run.sh build              # 生成 Gen/ 与工程，编译、签名；不碰设备
./run.sh install            # 把上一次 build 的 App 装到 DM_DEVICE
./run.sh <模式> [上限秒=60]  # 在真机上以 -mode <模式> 启动，拉回 Documents/<模式>.txt 并打印
```

前提：`../../setup.sh` 已跑完（改写后的程序与 iOS 版着色器库就绪），本机配置里填了 `DM_TEAM_ID` 与 `DM_BUNDLE_ID`。

`build` 依次做：从游戏文件生成原版衍生资源（`tools/gen_assets.py`，输入没变就跳过）→ 生成宿主的三个表文件（`dm_vault_tab.h`、`dm_hud_exp.h`、`dm_bright_levels.inc`） → `gen_build.py`、`gen_embed.py` → 拷着色器库与各张清单进 `Gen/` → 生成覆盖层 → `xcodegen generate` → `xcodebuild`（Release，自动签名）。这些步骤调用 `python3`，要在 `setup.sh` 建的 Python 环境里跑（Pillow、numpy 等装在里面）：先执行 `source ~/dm-work/build/cache/dm/venv/bin/activate`（设了 `DM_ROOT` 就把 `~/dm-work` 换成它）。构建日志在 `$DM_ROOT/run/ios/build.log`，App 在 `<派生数据目录>/Build/Products/Release-iphoneos/DmIOS.app`。

`install` 与按模式启动会先取设备锁（`tools/devlock.sh`，锁目录 `$DM_ROOT/run/ios/device.lock`），保证同一时刻只有一个脚本在操作手机。

常用的环境变量：

| 变量 | 作用 | 缺省 |
|---|---|---|
| `DM_OUT` | 改写产物目录 | `../tools/product.sh path touch` 的输出 |
| `DM_IOS_DD` | Xcode 派生数据目录 | `$DM_ROOT/run/ios/dd` |
| `DM_ROMFS_SRC` | 原版 RomFS（`Update/RomFS`） | `$DM_ROOT/data/switch/3.3.94012/Update/RomFS` |
| `DM_GEN_ONLY=1` | 只生成 `Gen/` 与工程，不编译 | 不设 |
| `DM_ASTC_PACK` | ASTC 补充包目录，设了才出配套的覆盖层 | 不设 |

App 里的测试存档也在 `build` 时带上：`$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/` 存在就拷进 App。启动时带环境变量 `DM_SAVE_FRESH=warlock`，这一局就用它的新副本，放在 `Documents/save.test`，不碰 `Documents/save` 里的真实存档。`DM_SAVE_FRESH=empty` 从空存档起；`DM_SAVE_TEST=1` 沿用上一局留下的 `Documents/save.test`。

## 冒烟

冒烟是把 App 真的起起来走一小段，确认它起得来、进得了局、有声音。仓库里有两套，判据相同：一套把 iOS 包直接放在 Mac 上跑，一套在真机上跑。

### 第一步：生成冒烟存档

```bash
tools/make_fixture.sh
```

冒烟的第一段是「读档进局」，要一份存档。这个脚本在 Mac 上把 iOS 包从空存档起一局，用触点脚本先后建两个角色、各进一次营地并保存退出，再把结果收到 `$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/`。之后 `run.sh build` 出的 App 就会带上它。

- 只需做一次；目标目录已存在时不覆盖，要重做先删掉那个目录。
- 它顺带把你的游戏资源克隆进 Mac 上这个 App 的容器（APFS 克隆，不另占空间），后面的 Mac 冒烟直接可用。
- 存档只在你的工作区里，不进仓库。

### 在 Mac 上跑 iOS 包

```bash
tools/smoke_mac.sh 1        # 参数是「读档进局」重复的次数
```

Apple 芯片的 Mac 可以原样运行 iOS 的 arm64 包。脚本做的事：

1. 构建：`run.sh build` 只做生成，再以「Designed for iPad」为目标编译，派生数据放在仓库根目录的 `.iosdd_mac/`（源码不是用 git 克隆来的话，是运行脚本时所在的目录）。
2. 包装：直接打开构建产物会报格式不对，要包成 `.iosdd_mac/DmMacSmoke.app/Wrapper/DmIOS.app`，外加一个 `WrappedBundle` 软链。
3. 读档进局 N 次：每次带 `DM_SAVE_FRESH=warlock` 和一段按键脚本（主菜单 → 选角 → 进局）启动，通过 App 的测试日志端口（`DM_LOG_PORT`，由 `tools/logport.py` 读）依次等到主菜单、进局、读取画面撤下。
4. 玩家路径一局：不带 `-mode`、不带看门狗和存档变量，等同于玩家点图标；进局后开暂停菜单再静置。要求日志里音频输出是真出声的、读得到玩家状态、没有带「【红】」的行。
5. 触摸玩家路径一局：从空存档起，只用触点脚本 `tools/smoke_touch.txt` 建角进局，点三下空地人不动，点脚下的小站能开小站面板。

末行是 `SMOKE-MAC OK <成功数>/<次数> 用时 … 秒` 或 `SMOKE-MAC RED …`。结果目录 `$DM_ROOT/run/ios/smoke_mac/<时间>_<进程号>/`，每次运行还往 `$DM_ROOT/run/ios/smoke_ledger.tsv` 追加一行记录。

要点：

- 游戏资源从 Mac 上这个 App 的容器里读（`Library/Application Support/romfs`）。容器里没有时，脚本从 `SMOKE_MAC_ROMFS` 指的目录克隆；这个变量要指到 RomFS 的 `data` 目录，例如 `SMOKE_MAC_ROMFS=$DM_ROOT/data/switch/3.3.94012/Update/RomFS/data`。先跑过 `make_fixture.sh` 就不用管这一条。
- 同一个包名在一台 Mac 上只能有一个实例。脚本开跑前发现已有实例就报红，不替你结束它。同一台 Mac 上的 Mac 冒烟、触屏用例、界面回归共用一把锁 `$DM_ROOT/run/ios/macsmoke.lock`，自动排队。
- 缺省带上一组「真机档位」参数（画面分辨率与左右安全区），让布局和触点换算与手机一致；`DM_MAC_DEVICE_PROFILE=0` 改回按 Mac 窗口原样跑。
- 包在运行时，系统把它的 `Wrapper` 目录挂载出去。这时删掉或整体替换这层目录会留下卸不掉的失效挂载点，那个路径从此打不开，只有重启能清。所以包在跑的时候不要动它。

可调项：

| 变量 | 作用 |
|---|---|
| `SMOKE_MAC_HOST` | 缺省 `local`，全部在本机做。设成 ssh 目标（`用户名@主机`）时，本机构建并包装，把包传到那台 Mac 上跑，结果拉回本机。这是给第二台专门跑测试的 Mac 准备的，没有就不用设 |
| `SMOKE_MAC_PKG_ONLY=1` | 只构建和包装，不跑；末行打印包的路径 |
| `SMOKE_PLAYER=0`、`SMOKE_TOUCH=0` | 跳过玩家路径 / 触摸玩家路径 |
| `SMOKE_KEYS` | 换一段按键脚本。这样的运行在记录里标成「反例」，并且不跑后两段 |
| `SMOKE_MAIN_MENU_TIMEOUT`、`SMOKE_GAME_SCREEN_TIMEOUT` | 等主菜单 / 等读取画面撤下的上限秒，缺省 40 / 30 |
| `SMOKE_UIREG=1` | 冒烟通过后在后台加跑一局界面回归，见 [tools/uireg/usage.zh-CN.md](tools/uireg/usage.zh-CN.md) |
| `SMOKE_OWNER` | 排队和记录里显示的名字 |

Mac 冒烟能验的是：外壳起得来、资源定位、进局、生命周期、出声。验不了的是：安装与签名、真实手指、手机的发热与 GPU 时间。

### 真机冒烟

```bash
# 在仓库根目录执行
dm/ios/tools/smoke.sh 3
```

前提，比 Mac 冒烟多这些：

- 本机配置里有 `DM_DEVICE` 与 `DM_IOS_UDID`，手机用线连着并已信任这台 Mac。
- 命令行工具 `timeout`（GNU coreutils）与 `iproxy`、`ideviceinfo`（libimobiledevice）在 `PATH` 里。
- 游戏资源已经推到手机上（「找游戏资源」第 1 种的那条命令）。
- 已有冒烟存档（`make_fixture.sh`）。

脚本先读手机电量（低于 30% 拒绝开跑，并留下暂停标记，充到 60% 才放行），取设备锁，然后构建（派生数据在仓库根目录的 `.iosdd/`；源码不是用 git 克隆来的话，是运行脚本时所在的目录）、装机、按次数启动。日志同样走 App 的测试日志端口，经 `iproxy` 的 USB 端口映射读回。判据与 Mac 上的前两段相同（读档进局 N 次 + 玩家路径一局），末行 `SMOKE OK <成功数>/<次数> 用时 … 秒` 或 `SMOKE RED …`。结果目录 `$DM_ROOT/run/ios/smoke/<时间>/`，记录写进同一份 `smoke_ledger.tsv`。

## 触点脚本

冒烟和回归里的「触点脚本」是一份文本，每行「秒 动作 参数」，由 App 按时间执行。脚本手指走的是和真手指相同的 `touchesBegan / Moved / Ended` 路径，之后的判定与发布完全一样。

- 用法：把脚本放进 App 的 `Documents`，启动时带 `DM_TOUCH_SCRIPT=<文件名>`。缺省从首次进局起计时，带 `DM_TOUCH_SCRIPT_START=menu` 则从主菜单出现起计时。
- 动作有 `down` / `move` / `up`（手指）、`tap_ui`（按界面控件名点）、`tap_unit`（点世界里的某类单位）、`walk_to`、`type`、`shot`（截图）、`note`、`pos`、`await` 等，完整格式见 `Sources/front_ios.m` 的 `touch_script_start`。
- 位置可以写点数坐标，也可以写 `@控件名`。带 `DM_TOUCH_REF=<宽>x<高>` 时，裸坐标按这块参考画面换算到当前屏幕。

单独跑一份触点脚本：Mac 上用 `tools/touch_e2e_mac.sh <脚本> <结果目录> [秒]`，真机用 `tools/touch_e2e.sh`（同参数）。

## 单测

```bash
make -C dm/ios/tests test
```

不需要设备，也不启动 App。内容：

- 触摸判定状态机 `dm_touch_controls.c` 的各项行为（`t_touch_controls.c`、`t_skill_aim.c`、`t_joy_travel.c`、`t_portal_sequence.c`）。
- 技能绑定的解析与读取（`t_native_binding.c`、`t_game_bridge.c`、`t_binding_*.c`、`t_native_hud_*.c`），用到 [tests/fixtures/native_binding/](tests/fixtures/native_binding/README.zh-CN.md) 里的两份夹具。公开仓库不带这两份夹具：缺了的话，读它们的三个测试自动跳过，其余照常。
- 技能图集索引（`t_native_art.c`、`test_native_art_capture.py`）。这一项需要本机有游戏文件：技能索引 `Sources/dm_touch_native_art.c` 不进仓库，缺了会从 `DM_ROMFS_SRC`（缺省 `$DM_ROOT/data/switch/3.3.94012/Update/RomFS`）现生成。
- 几份 Python 脚本，核对 `front_ios.m` 里触摸通路的接线和诊断日志格式。

`tools/` 下还有几个脚本自带的自测：`devlock_test.sh`、`devlock_max_test.sh`、`devlock_locale_test.sh`（设备锁）、`devbattery_test.sh`（电量检查）。

## tools/ 里的其它脚本

| 脚本 | 用途 |
|---|---|
| `gen_assets.py` 及它调用的 `gen_*.py`、`native_art/generate.py` | 从游戏文件生成原版衍生资源，说明在 [../README.zh-CN.md](../README.zh-CN.md) |
| `check_hud_gen.sh` | 检查生成物没有被提交进仓库；本机有游戏文件时把底栏实际生成一遍 |
| `devlock.sh`、`devbattery.sh`、`local_env.sh`、`logport.py` | 被其它脚本引用的公共部分：设备锁、电量检查、本机配置、日志端口 |
| `touch_e2e.sh`、`touch_e2e_mac.sh` | 跑一份触点脚本并收回日志与截图 |
| `charsel_retap.sh` | 选角页的触点回归（Mac 上的 iOS 包） |
| `lifecycle.sh` | 真机上切后台再回前台的一轮生命周期测试 |
| `screen_check.py`、`fullscreen_check.py` | 按整窗截图核对方向、黑边、拉伸 |
| `bootprof.sh`、`heat_ab.sh`、`gpu_read.sh`、`xtrace.sh` | 真机测量：启动时间线、发热对照、GPU 时间、Instruments 命令行录制 |
| `asset_manifest.py` | 给资源直接下载通路生成清单 |
