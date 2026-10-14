# 老系统：iOS 18 – 25 与 macOS 15

[English](OLDER_SYSTEMS.md) | **简体中文**

缺省的构建要求 iOS 26 / macOS 26：图形部分建在这两个系统才有的图形接口（Metal 4）上。没升到 26 的设备用「老接口版」——同一份源码、同一个游戏、同样的功能，只是图形部分改用老系统也有的接口，最低 **iOS 18.0 / macOS 15.0**。

## 我该用哪一种

先看系统，再看芯片。两件事互不相干，可以叠在一起（各设备的完整情况见首页「[支持哪些设备](../README.zh-CN.md#支持哪些设备)」）：

| 你的设备 | 系统是 iOS 26 及以上 | 系统是 iOS 18 – 25 |
|---|---|---|
| **A17 Pro 及更新**（iPhone 15 Pro 起、iPhone 16 起） | 缺省构建，什么都不用加 | 老接口版 |
| **A16 及更早**（iPhone 14 系列、iPhone 15 / 15 Plus 及更早） | 缺省构建 + 老机型贴图包 | 老接口版 + 老机型贴图包 |

- **系统**决定用哪种构建（缺省 / 老接口版）。
- **芯片**决定要不要多做一份「老机型贴图包」：A16 及更早的显卡不认游戏贴图的格式，做法见 [LEGACY_DEVICES.zh-CN.md](LEGACY_DEVICES.zh-CN.md)。
- Mac 只看系统：macOS 26 及以上用缺省构建，macOS 15 – 25 用老接口版；Apple 芯片的 Mac 都不需要贴图包。

## 怎么出老接口版

### iPhone / iPad，有 Mac 和 Xcode

准备步骤不变（`./setup.sh`），构建时多带一个开关：

```bash
cd dm/ios
DM_IOS_M3=1 ./run.sh build
```

它把部署目标改成 iOS 18.0、图形部分换成老接口。免费的 Apple ID 照旧可用：`DM_FREE_ACCOUNT=1` 与它可以同时设。A16 及更早的机型，再照 [LEGACY_DEVICES.zh-CN.md](LEGACY_DEVICES.zh-CN.md) 带上贴图包（`DM_ASTC_PACK`）。

### iPhone / iPad，不用 Xcode

发布页上有两份壳，挑对应系统的那一份，其余步骤与 [NO_XCODE.zh-CN.md](NO_XCODE.zh-CN.md) 完全相同：

| 壳 | 给谁 |
|---|---|
| `D2RMobile-shell.ipa` | iOS 26 及以上 |
| `D2RMobile-shell-ios18.ipa` | iOS 18 – 25 |

做包工具两份壳都认，命令不变。A16 及更早的机型同样加 `D2RM_ASTC_PACK`（见 NO_XCODE 的「老机型」一节）——贴图包只和芯片有关，两份壳用的是同一个贴图包。

### Mac

```bash
source ~/dm-work/build/cache/dm/venv/bin/activate
DM_MAC_M3=1 dm/tools/mac_app.sh        # 产出 ~/dm-work/out/macapp/D2R Mobile M3.app
```

它会把着色器库按 macOS 15 重编一份（约 3 分钟，只在第一次），放在输出目录里。老接口版的 App 叫「D2R Mobile M3」，存档和日志目录也是单独的，可以和缺省的 App 并存。和缺省的 App 一样，它只能在构建它的这台 Mac 上跑；**输出目录不要删也不要挪**，重编的着色器库在里面。

## 和缺省构建有什么不同

- 画面、玩法、存档格式都一样。
- 图形部分每次绘制要多提交约两到三成的指令，所以同样的画面多花一些处理器时间；我们量到的帧时间差别在测量噪声之内。
- 不带「资源下载扩展」（它只服务苹果托管的资源包，公开构建本来也用不上）。

## 实际情况

macOS 15：已有内测玩家在 Mac mini M4（macOS 15.7）上实际玩过老接口版。iOS 18：已有社区开发者在自己的分支上用同样的改法把游戏跑在了 iOS 18 上；本仓库这一版的 iOS 18 包刚发给内测玩家。遇到问题请开 Issue，带上机型、系统版本和诊断日志（暂停菜单 →「分享诊断日志」）。
