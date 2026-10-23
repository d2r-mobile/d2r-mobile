# 安卓团队协作入口

English: [ANDROID_COLLABORATION.md](ANDROID_COLLABORATION.md)

## 仓库职责

| 仓库 | 维护与用途 |
| --- | --- |
| Android implementation | 我们维护的私有安卓开发仓库；本仓是安卓日常协作入口 |
| Android implementation | 我们维护的私有鸿蒙开发仓库；权限及源码归属独立 |
| [d2r-mobile/d2r-mobile](https://github.com/d2r-mobile/d2r-mobile) | 上游项目主仓库；我们以安卓、鸿蒙端贡献者身份提交 PR，由上游维护者审查合入 |

本仓保留 d2r-mobile 的 Git 历史及 MIT 许可。集成的上游基线为 `source snapshot`。鸿蒙旧仓源自 xbl-ios，其生产源码与本仓分开维护，不自动互相覆盖。

GitHub 上现有公开 fork `the project/d2r-mobile` 只作为向上游提 PR 的通道，不是另一条产品开发线。私有本仓不是 GitHub fork；需要贡献时，将已审核的提交放到该 fork 的主题分支再提上游 PR，不整仓镜像或直接推上游 main。鸿蒙已有草稿 PR，未合并；安卓当前没有上游 PR。

## 新协作者从这里开始

1. 仓库管理员在 Settings → Collaborators 添加你的 GitHub 账号；接受邀请后克隆本仓。每位开发者使用自己的克隆或工作树。
2. 阅读本页、[安卓构建说明](../dm/android/README.zh-CN.md)、[上游贡献规范](../CONTRIBUTING.zh-CN.md)。游戏程序、资源、着色器及由游戏生成的文件在各自机器上准备，不在 Git 仓库中。
3. 以 `main` 为集成基线，使用 `android/<主题>` 分支。开始修改前，在本仓 Issue 中明确负责人和文件范围，避免多人同时改同一片文件；重构建及设备验证串行协调。
4. 完成后向本仓提 PR，写明源码提交、构建版本、设备、实际测试时间、结果和未验证项。日常开发提交可以包含明确标注的未完成功能；不能据此宣称可玩验收通过。
5. 经测试的独立修复再整理成上游 PR，遵循中英注释、文字表、资源边界及回归要求。上游同步先比较再合并，不 reset/clean 或强推覆盖本仓开发成果。

推荐远程命名：`origin` 指向本仓；`upstream` 指向 d2r-mobile/d2r-mobile，只拉取；`fork` 指向个人公开贡献 fork。本地可设置 `remote.pushDefault=origin`，并给 upstream 配置不可用的 push URL，防止误推。

## 代码结构

- `dm/android/app/`：Java Activity、全屏 Surface、悬浮触摸控件、系统输入框、桌面卡片及 Gradle。
- `dm/android/jni_android.c`、`text_input_jni.c`、`lifecycle_dispatch.h`：启动、专用宿主输入线程、生命周期派发。
- `dm/android/vulkan_present.*`：Android 窗口与交换链呈现。
- `dm/gfx/vulkan/`：NVN 接口适配、Vulkan 回放；`engine/xbg_game.cpp` 是当前图形性能及缓存问题的主要入口。
- `dm/host/`、`dm/i18n/`：共用宿主与中英文字表；跨平台改动须兼顾上游。
- `dm/android/tests/`、`dm/gfx/vulkan/engine/tests/`：Android 与 Vulkan 的独立回归检查。

## 当前实测状态（2026-10-16，北京时间）

开发包 `0.1.7-dev / code8` 在 vivo X200 Ultra（V2454A，Android 16/API36，Adreno 830）于 08:56 安装。APK SHA256：`381514cce18027320cb6b0a5a72a83beb2cfefcbedb9bc3a9d0fb7f089cf62d0`。对应源码提交 `c7db2646eb9ff74678cd47dc3c6f87834b3852ca`。这是开发基线，不是完成版。

| 项目 | 实际结果 |
| --- | --- |
| 构建 | 当前源码 assembleDebug、lintDebug 成功；i18n、字面量、原生控件保护、模式登记通过。Windows 未运行 Apple/Metal/iOS 回归 |
| 安装与存档 | code7、code8 均通过匹配应用及版本的 vivo 确认界面模拟点击完成覆盖安装；code8 安装前后 42 个 save/save.work 文件哈希一致 |
| 输入与后台 | code5 两次起名确认通过；code6 起名取消、起名时切后台取消及恢复同一进程通过；尚不是全输入/长期稳定性验收 |
| 闪烁与黑块 | code7 跨提交 descriptor memo 开→关→开对照复现。code8 排除含 texel buffer 的 memo，避免复用 slot 退休时已销毁的 VkBufferView，保留其他跨提交缓存。角色选择、创建预览、营地及两次短距离移动截图未再见原大块黑块；更长时间、野外/战斗仍待验 |
| 帧率 | code8 当前营地9.14–10.48 FPS（同工作量 960 帧），仍明显不足；固定场景 GPU 45.66–58.85 ms/帧，present 1.27–1.57 ms。没有已验证的稳定帧率提升，不能把系统屏幕叠层 60 当游戏 FPS |
| 卡片 | 2×2/4×2 源码已编译、系统 provider 已登记、27 个合成解析用例通过；桌面真实显示/刷新及 vivo 平台适配尚未验完 |

关键剩余：GPU 主绘制耗时、野外帧率与长时间画面回归；完整 NPC/战斗/技能/物品交互；多轮后台与冷启存档；iOS 触摸细节及通用导入界面。两项 shader SSA 候选经原版→候选→恢复版对照未确认稳定收益，当前设备保留原 shader。

10-22 更新：通用导入已做——发布用的 APK 不含游戏内容，玩家在电脑上用 `dm/tools/d2rm_android.py` 和自己的游戏文件做导入包，App 打开时带进度导入；着色器导出（`dm/gfx/tools/dmsh_vk.py`）做出的 6,927 个游戏着色器包与本仓测试用的那套逐字节相同；触屏外观与面板态和 iOS 对齐（技能图标、按钮底座、摇杆，面板上不出虚拟手柄圆钮）。iOS 触摸细节剩空技能格菜单、加点浮层、商店拖动跟手图。

本轮原始日志、截图、私有资源、存档备份和 APK 保存在维护者本机 `artifacts/android-20261016`，不随源码上传。协作证据摘要可入仓，原始材料需单独按权限查看。自动跟进保持暂停；不自动恢复旧代理。设备不清数据、不删存档，升级保持应用签名一致。
