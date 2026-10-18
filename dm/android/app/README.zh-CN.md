# Android 诊断 APK

English: [README.md](README.md)

本 NativeActivity 开发 App **只打包自有合成样例**，不是可玩的 D2R。APK 中没有游戏数据或 Switch 密钥。包名 `org.d2rmobile.android.diagnostic`，仅 ARM64，最低 API 29、目标 API 35，不申请网络或共享存储权限。

启动时读取合成产物、重绑定宿主门、映射 RX/R/RW 页面，尝试在实际 App 进程中完成原生客体 TLS 往返；日志标签为 `D2R-Diagnostic`。窗口建立后执行已测试的 Vulkan 片段坐标图案，读回并复制到 Android 原生窗口。这是单帧工程诊断，不是优化后的游戏呈现器。暂停／恢复记录日志，低内存时释放 GPU 上下文，重绘时重建。不包含后台渲染循环、游戏资源导入、存档、游戏操作或音频。

## 构建

```sh
PATH=/opt/homebrew/bin:$PATH bash dm/android/app/build.sh
```

需要 NDK r29、CMake、宿主 glslc、JDK/keytool、zip 和 SDK platform/build-tools 35。通过 `paths.sh` 缺省仍放 T2，也可设置 `ANDROID_SDK_ROOT`。直接使用 aapt2、zipalign、apksigner，不使用 Gradle。SDK 包来自 Google 官方仓库，按 `repository2-1.xml` 的摘要核对：

| 包 | 仓库元数据中的 SHA-1 |
|---|---|
| `platform-35_r02.zip` | `0bb560a90a7a2cbd0dd8348224d518b638fe7949` |
| `build-tools_r35_macosx.zip` | `93ab8ce91230e067b5add4bfa79919c52b27f072` |

安装位置 `../d2r-work/tools/android/sdk`；压缩包／元数据位于 `tools/android/sdk-packages`，SDK 声明／许可随包保留。App 名称来自共享文案表的 `dmAndroidDiagnosticName`。

输出：`../d2r-work/build/android/apk-diagnostic/d2r-diagnostic-debug.apk`。本地调试签名密钥位于 `../d2r-work/keys/android-diagnostic-debug.p12`，不要提交密钥或生成的 APK。动态库不压缩，按 16 KiB 对齐；构建会验证 APK 签名、ZIP 对齐、包元数据及 ARM64 ABI，不自动安装或启动。

## 2026-10-17 手机检查点

通过 Android 无线调试配对后，可安装、检查日志（也支持 USB 调试）：

```sh
adb install -r ../d2r-work/build/android/apk-diagnostic/d2r-diagnostic-debug.apk
adb shell am start -W -n org.d2rmobile.android.diagnostic/android.app.NativeActivity
adb logcat -d -s D2R-Diagnostic
```

多设备时所有命令一致使用 `adb -s SERIAL`。小米 25010PN30G / SM8750、Android 16 API 36、4096 字节页已成功安装、启动诊断 APK。实际 NativeActivity 进程记录 `synthetic APK-process ARM64 gate smoke=PASS`、`256x256 pixel-check=PASS` 和 `CPU-readback window post=PASS`。通过启动桌面 Activity 再返回，记录了 paused／resumed。证据：`../d2r-work/build/dump-inspection/device-diagnostic.log`。手机拒绝了注入 Home 按键，因此改用 Activity Intent，没有修改安全设置。

这些结果仅覆盖 App 进程中的自有合成样例。真实游戏代码、完整生命周期／低内存压力、截图／可访问性 QA 与游戏兼容性尚未验证。后续源码变化（包括 SVC 监视器清除）已通过宿主测试／Android 编译，但未重新安装到手机测试，不宣称视觉 QA 通过。

画面沿用确定性的图形测试图案，没有产品字体、控件或品牌布局；web／React 设计研究不适用于此原生工程测试框架。真正的游戏 UI 及其视觉验收仍是后续工作。
