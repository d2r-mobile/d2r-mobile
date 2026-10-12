# 准备游戏文件

**你必须拥有正版游戏。** 本项目只是让你已经购买的《暗黑破坏神 II：狱火重生》Nintendo Switch 版在自己的 iPhone / iPad 上运行。仓库里没有、以后也不会有任何游戏文件；请不要向他人索取或分发游戏文件，也不要在议题和合并请求里贴出游戏文件或下载地址。

English: [GAME_FILES.md](GAME_FILES.md)

## 需要哪些文件

Switch 版《暗黑破坏神 II：狱火重生》，**带「术士君临」资料片**，更新到 **3.3.94012**。本体、更新和资料片都要是你自己购买的；本项目是按带资料片的版本做的，没有资料片的情况不支持、也没有验过。需要其中两部分：

| 部分 | 内容 | 大小 |
|---|---|---|
| 程序文件（ExeFS） | `main`、`sdk`、`subsdk0` | 约 26 MB |
| 更新包资源（RomFS） | `data/` 目录，里面有 `build_manifest.psv` 和一批 `.data` 文件 | 约 8.4 GB |

版本必须对上：改写规则和生成脚本按这个版本逐项核对，别的版本会直接报错并说明哪里不一致，不会悄悄出一个坏包。

## 怎么从自己的主机导出

用你自己的 Switch 和你自己购买的游戏导出。常用的开源工具是 **nxdumptool**，它的说明页讲了安装和使用方法，并且长期维护：

<https://github.com/DarkMatterCore/nxdumptool>

导出时选游戏的**更新（Update）**，分别导出它的 ExeFS 与 RomFS 两部分。

## 已经有游戏包文件时：一条命令取出来

如果你导出的是一整个游戏包（`.xci` 或 `.nsp`，里面同时有本体和 3.3.94012 的更新），不用手动拆：

```
dm/tools/extract_game.sh <你的游戏包> <你的 prod.keys>
```

它会拆包、认出本体与更新、取出上面说的两部分、核对版本，然后放到下一节说的位置。`prod.keys` 是从你自己的主机导出的密钥文件；游戏包和密钥都只在你的电脑上读，不会被拷贝或上传。

需要先装好 **hactool**（开源的 Switch 文件解包工具）：<https://github.com/SciresM/hactool>，按它的说明编译出来，放进 `PATH` 或用 `HACTOOL=<路径>` 指给脚本。磁盘留出约 20 GB。

## 放到哪里

放进工作区（缺省 `~/dm-work`，可用环境变量 `DM_ROOT` 换位置）：

```
~/dm-work/data/switch/3.3.94012/Update/ExeFS/main
~/dm-work/data/switch/3.3.94012/Update/ExeFS/sdk
~/dm-work/data/switch/3.3.94012/Update/ExeFS/subsdk0
~/dm-work/data/switch/3.3.94012/Update/RomFS/data/build_manifest.psv
~/dm-work/data/switch/3.3.94012/Update/RomFS/data/*.data
```

放好后运行 `./setup.sh check`：它逐项报告找到了什么、还缺什么、下一步该执行哪条命令，并核对三个程序文件是不是 3.3.94012 这一版，不做任何改动。也可以把上面两步并成一条：在仓库根目录执行 `./setup.sh <你的游戏包> <你的 prod.keys>`，取出游戏文件后直接接着准备工作区。

## 手机上要放的那个「游戏资源文件夹」

App 装到手机上以后，第一次打开会说「还没有游戏资源」。它要的就是你游戏文件 `RomFS/data` 里的 **18 个资源档案**（合计约 5.6 GB），安装包里不带：

```
00-00000001.data … 00-00000006.data      （6 个）
01-00000001.data  01-00000002.data
02-00000001.data  02-00000002.data
03-00000001.data  03-00000002.data
04-00000001.data  04-00000002.data
05-00000001.data  05-00000002.data
17-00000001.data  17-00000002.data
```

把这 18 个文件放进一个文件夹（叫什么都行，外面多套几层也认），拷进这个 App 自己的文件夹：手机上「文件」App →「我的 iPhone」→ 这个 App 的名字；或者用线连上 Mac，在 Finder 里选中手机 →「文件」→ 拖进这个 App。拷完回到 App 点「重试」。没拷全也不要紧：屏上会列出已有几个、还缺哪几个，补齐再点重试。

用 Xcode 构建并连着线的话，`dm/ios/README.zh-CN.md` 的「找游戏资源」一节有一条命令替你推过去。

## 为什么是 Switch 版而不是 PC 版

iPhone 和 Switch 用的是同一类处理器。Switch 版的游戏程序绝大部分可以一个字节不改地在 iPhone 上直接执行，这是能流畅运行的前提；PC 版的程序是为另一类处理器编写的，做不到这一点。游戏资源两个版本也不通用：贴图、界面图、音频各用各的格式。
