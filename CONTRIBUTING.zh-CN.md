# 参与开发

English: [CONTRIBUTING.md](CONTRIBUTING.md)

这一页讲三件事：改动之前要能跑通什么，仓库的三条规矩，报问题时附什么。各部分怎么工作不在这里重复，见 [dm/README.zh-CN.md](dm/README.zh-CN.md) 与 [dm/verify/README.zh-CN.md](dm/verify/README.zh-CN.md)。

## 改动之前：先跑通测试

先在没动过的代码上把下面能跑的都跑通，改完再跑一遍。命令都在仓库根目录执行。

### 不需要游戏文件

只读源码，有 `python3` 就能跑。

| 命令 | 验什么 |
|---|---|
| `python3 dm/i18n/scan_literals.py` | 字面量闸门：玩家看得到的中文有没有绕过中英表，见 [dm/i18n/README.zh-CN.md](dm/i18n/README.zh-CN.md) |
| `python3 dm/ios/tools/dm_i18n.py --check` | 中英表 `dm/i18n/ui_text.tsv` 每一行合不合规则，入库的 `dm/host/dm_i18n_tab.h` 是不是由当前的表生成的 |

### 需要游戏文件与备好的工作区

前提：你自己的游戏文件已放到位，`./setup.sh` 已跑完（它建好 Python 环境、出好改写产物、翻译好着色器），并且已经进入那个 Python 环境：`source ~/dm-work/build/cache/dm/venv/bin/activate`（设了 `DM_ROOT` 就把 `~/dm-work` 换成它）。缺什么用 `./setup.sh check` 查。

| 命令 | 验什么 | 额外前提 |
|---|---|---|
| `make -C dm/host/tests run` | 宿主单元测试。同时跑上面两条文字检查，以及两条只读源码的扫描：不许写原版界面控件（`native_write_scan.py`）、触屏 / 手柄两种模式的登记（`mode_scan.py`） | 其中几项直接读游戏的程序文件与资源目录 |
| `make -C dm/gfx/tests run` | 图形层单元测试，在本机的 Metal 上真跑 | 其中几项读 `./setup.sh` 翻译出的着色器缓存 |
| `make -C dm/ios/tests test` | 触摸判定、技能绑定解析等 iOS 外壳的单元测试，不需要设备，也不启动 App | 技能索引不进仓库，缺了会从游戏文件现生成 |
| `dm/verify/regress.sh` | 改写等价回归：改写器的输出结构对不对，每一处改写与原指令行为是否一致，宿主门功能是否正常。末行 `REGRESS OK` 或 `REGRESS FAIL` | `llvm-objdump` 与 `llvm-objcopy`，目录用 `LLVM_BIN` 指定 |
| `cd dm/ios && ./run.sh build` | iOS 包能不能编出来（只构建，不碰设备） | Xcode，`dm/ios/local.env` 里的签名配置 |
| `dm/ios/tools/smoke_mac.sh 1` | 冒烟：把 iOS 包放在 Mac 上真起一遍，读档进局、有声音、日志里没有带「【红】」的行。末行 `SMOKE-MAC OK` 或 `SMOKE-MAC RED` | 先跑一次 `dm/ios/tools/make_fixture.sh` 生成冒烟存档 |
| `dm/ios/tools/check_hud_gen.sh` | 由游戏文件生成的东西有没有被加进 git；本机有游戏文件时再把底部界面实际生成一遍 | 在 git 仓库里执行 |

改了改写器 `dm/tools/patch_nso.py` 时，用 `dm/verify/regress.sh --fresh`：它先用你工作树里的改写器重出一份产物再验。

各项的判据、输出位置和环境变量见 [dm/verify/README.zh-CN.md](dm/verify/README.zh-CN.md) 与 [dm/ios/README.zh-CN.md](dm/ios/README.zh-CN.md)。

### 你跑不了的那部分

整套验收 `dm/verify/accept.sh` 里的存档对拍、画面闸门、逻辑闸门要用「真值数据」——原版游戏的运行记录。这些数据不在仓库里，也不会放进来；录制它们用的模拟器驱动脚本同样不随仓库提供。真值是什么、格式怎样，见 [dm/verify/README.zh-CN.md](dm/verify/README.zh-CN.md)。

提交改动时写明你实际跑了哪些、结果如何；没跑的直接说没跑。

## 三条规矩

### 一、不提交游戏内容

仓库不含、也不分发任何游戏内容：游戏程序、美术、文字、着色器、存档，以及由它们生成的图、表、代码，都不进仓库。这些东西在每个人自己的机器上、用自己的游戏文件现做出来。仓库里带着的是为适配而记录的接口信息（着色器的接口描述、图形接口函数表、程序内的地址等），逐项列在 [docs/DERIVED.zh-CN.md](docs/DERIVED.zh-CN.md)；要往里加这一类东西，先开 Issue 商量。

下面这些是构建时生成的，已经写在各目录的 `.gitignore` 里，不要强行加进来：

| 位置 | 是什么 |
|---|---|
| `dm/host/dm_vault_tab.h`、`dm/host/dm_hud_exp.h`、`dm/host/dm_bright_levels.inc` | 由游戏的数据表生成的表头（`dm/host/Makefile` 构建时生成） |
| `dm/ios/Gen/` | iOS 包的生成物：由游戏文件做出的界面图、内嵌的游戏程序、覆盖层 |
| `dm/ios/Sources/TouchArt/dm_native_*.png`、`dm/ios/Sources/dm_touch_native_art.c` | 技能图集与技能索引 |
| `dm/ios/DmIOS.xcodeproj/`、`dm/ios/Sources/Info.plist`、`dm/ios/Sources/DmIOS.entitlements`、`dm/ios/RomfsDownloader/` 下的 `Info.plist` 与 `RomfsDownloader.entitlements` | 由 `dm/ios/project.yml` 生成的工程与配置 |
| `dm/ios/local.env` | 你自己的签名配置 |
| `dm/host/dmhost`、`*.dSYM/`、`*.bin`、`*.pkl`、`__pycache__/`、各 `tests/` 下编出的测试程序 | 编译产物与中间文件 |
| `.iosdd/`、`.iosdd_mac/`、`.iosdd_*/` | Xcode 的派生数据目录 |

另有三个文件在仓库里，但 `./setup.sh` 会按你本机的工具改写它们：`dm/gfx/tools/dmsh_chain.lock`、`dm/gfx/nvn/dmsh_chain.h`、`dm/gfx/nvn/c1_full.tsv`。这是预期的，不要提交这些改动。

工作区 `$DM_ROOT`（缺省 `~/dm-work`）下的东西——游戏文件、改写产物、缓存、运行日志——都在仓库之外，本来就不会进来。

报问题和提改动时同样：不贴游戏文件、密钥，不上传自己做出来的安装包。

### 二、注释中英双语

代码里的注释都是中文、英文各一份，内容相同。现有的写法有两种，照着写：

- 文件开头和成段的注释：先写中文各行，紧接着写英文各行。

  ```c
  // 导入解析（已实现接宿主、其余接记名桩）与宿主门分派。
  // Import resolution (implemented ones go to the host, the rest go to named stubs) and host gate dispatch.
  ```

- 行尾注释，以及 Python 文档串里的单行：同一行里中文在前、英文在后，中间用 ` | ` 隔开。

  ```c
  int dm_lang_zh(void);   // 1 = 我们的字用中文 | 1 = our text uses Chinese
  ```

每个源文件开头有一段注释，说明它做什么、依据是什么；`dm/host` 里的功能文件还写明用到游戏程序里的哪些地址（都是 3.3.94012 原映像的偏移）。新文件照此写。

说明文档同样中英各一份，内容对应：`README.md` 是英文，`README.zh-CN.md` 是中文。

`dm/third_party/` 下是别人的代码，保持上游原样，不适用这一条。

### 三、屏上新增的文字进中英表

玩家看得到的、我们自己加的文字，一律登记在 `dm/i18n/ui_text.tsv`（键 / 中文 / English / 用在哪），代码里只写键，不写字面量。改了表要重出并提交 `dm/host/dm_i18n_tab.h`：

```
python3 dm/ios/tools/dm_i18n.py --header > dm/host/dm_i18n_tab.h
```

字面量闸门 `python3 dm/i18n/scan_literals.py` 会拦住没进表的中文字面量。它只认含中文的字面量，直接写死的英文它拦不住，同样要进表。只进日志、不上屏的中文不用进表。表的规则、加一条文字的完整步骤、闸门的白名单怎么写，见 [dm/i18n/README.zh-CN.md](dm/i18n/README.zh-CN.md)。

## 提交改动

- 说明里写清改了什么、为什么。
- 写明跑了上面哪些测试、结果如何。
- 屏上有可见变化的，附改动后的截图。
- 确认没有带进任何游戏内容。

提交时会有一份模板列出这几项。

## 报问题

用 Issue 模板。要附的东西：

- 哪一步出的问题：准备游戏文件、`./setup.sh`、`./run.sh build`、不用 Xcode 的工具、装机、启动，还是游戏里。
- 你的环境：电脑与系统、Xcode 版本、手机型号与系统版本、签名用的账号类型（付费 / 免费 / 自签工具）、仓库的提交号。
- `./setup.sh check` 的完整输出。
- 发生了什么：报错原文、屏上看到的字、能复现的步骤。
- 日志：游戏里暂停菜单 →「分享诊断日志」导出的文件。贴之前看一眼，去掉不想公开的东西（设备名、文件路径里的用户名）。

不要贴游戏文件、密钥，也不要贴它们的下载地址；不要上传你做出来的安装包，里面有游戏内容。带这些内容的问题会被直接删除。「怎么拿到游戏文件」这一步我们帮不了：那要用你自己的主机和你自己买的游戏。
