# Android 诊断 APK

English: [README.md](README.md)

本 NativeActivity 开发 App **只打包自有合成样例**，不是可玩的 D2R。APK 中没有游戏数据或 Switch 密钥。包名 `org.d2rmobile.android.diagnostic`，仅 ARM64，最低 API 29、目标 API 35，不申请网络或共享存储权限。

启动时读取合成产物、重绑定宿主门、映射 RX/R/RW 页面，尝试在实际 App 进程中完成原生客体 TLS 往返；日志标签为 `D2R-Diagnostic`。窗口建立后执行已测试的 Vulkan 片段坐标图案，读回并复制到 Android 原生窗口。这是单帧工程诊断，不是优化后的游戏呈现器。暂停／恢复记录日志，低内存时释放 GPU 上下文，重绘时重建。不包含后台渲染循环、游戏资源导入、存档、游戏操作或音频。

## 生命周期所有权

`app/lifecycle.c` 是 NativeActivity 使用的生产生命周期控制器。状态为
`created -> starting -> loading -> owned-diagnostic`，另有 `paused`、
`stopping`、`stopped` 和 `failed`。诊断后端明确标记为
`owned-diagnostic`；`running` 保留给已准入的真实游戏后端，本 App 不会进入。
加载失败会显示为 `failed`，只执行一次宿主清理；必须停止并重新启动后才能再次
尝试。控制器持有一个已 acquire 的 `ANativeWindow` 引用，而不是回调期间可失效
的借用指针；窗口替换先 acquire 新引用，旧引用在有工作时保留在唯一的 retired
槽中，直到最后一次 `work_end`。匹配的 destroy 回调会延后释放，不会提前释放旧
引用；retired 槽占用期间的第二次替换会明确拒绝（背压），陈旧回调忽略。停止可
重复调用，异步工作未完成时停止保持 `stopping`，直到工作结束后再清理。控制器
只在 Android Activity/控制线程上使用。

失败路径同样会等待排空：暂停、恢复或 Surface acquire 失败时，如果仍有工作，
控制器保持 `failed`，保留 pending 计数并继续持有 Surface，直到最后一次
`work_end` 后只清理一次。`stop` 只确认已经排空的失败并允许重启，不会提前清除
pending。`dm_app_lifecycle_destroy()` 在有未完成工作或活动控制器尚未停止时返回
0 并保持指针有效；调用者必须保留它，在 `work_end`/`stop` 后重试。

当前诊断绘制和门加载是同步的，所以 NativeActivity 调用者通常可以立即 stop 并
destroy。未来异步游戏／引擎所有者必须保留控制器指针，在所属线程调用
`work_end`，并仅在 `dm_app_lifecycle_pending_work()` 归零后重试 stop/destroy；
不得在工作完成前释放控制器或代替 Activity 回调释放 Surface。

## 输入交付缺口（#32）

当前输入 looper 只处理 Android Back 键，没有把 `MotionEvent` 触点交给 r2d
核心。下一片必须在同一输入／核心线程创建唯一的 `dm_android_pad` 和
`dm_android_touch`，把 `MotionEvent` pointer ID 当作不透明 ID，使用非负
`CLOCK_MONOTONIC` 纳秒时间戳，并按 view density 把物理像素转换为逻辑点后调用
`dm_android_touch_down/move/up/cancel`。消费端必须调用
`dm_android_touch_read`（不得直接读 `dm_android_pad_read`）以取得有序队列视图。

适配器的 16 槽脉冲环有界。环满导致变更事件被拒绝时，App 必须在输入线程排空
读取后重试，不能静默丢弃；即使环满，`ACTION_UP`/`ACTION_CANCEL` 也必须在同一
线程完成（无法排队时回退 `cancel_all`，避免指针卡住）。该接线不属于生命周期
切片，#32 仍保持开放。

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
