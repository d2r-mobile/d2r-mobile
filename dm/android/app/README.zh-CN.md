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

## MotionEvent 交付与诊断消费（#32）

NativeActivity 输入 looper 现在通过 `app/touch_bridge.c` 把 Android
`MotionEvent` 转交给复用的 r2d 触控核心。`DOWN`、`POINTER_DOWN`、`MOVE`、
`UP`、`POINTER_UP`、`CANCEL` 都在同一 looper／核心线程处理。pointer ID 是不透明
Android ID；事件时间使用 `CLOCK_MONOTONIC` 纳秒；调用适配器前按 Activity density
把物理像素换算为逻辑点；动作索引支持不连续 ID。`app/view_policy.c` 把物理视图
尺寸、density 与逻辑点边界同诊断渲染缓冲区分开；绘制路径不再强制 256x256 窗口
几何，因此后续重绘不会把 1440x900 的物理输入域变成 256x256。

同一 looper 上的 `timerfd` 让 `dm_android_touch_tick` 在之后没有新触点时仍推进
定时器（ZL 领先、延迟脉冲、判定和重复）。只有 `app/input_policy.c` 同时报告
resumed、focused、输入队列、窗口和 bridge 就绪时，才接受 MotionEvent 和 timer
tick。timer 启动失败、致命 read 错误或 hangup 也属于接受条件：输入被门控、触控
状态归零并记录不可用，直到后续回调明确重试成功。暂停、失焦、队列替换、窗口丢失
和销毁都会停止 timer，取消活动指针与排队脉冲，并只消费一次中性快照。输入队列
替换会先取消并销毁旧 bridge，再连接新队列。进程创建时不假定已 resume/focus，
只有对应 NativeActivity 回调实际观察到后才接受输入。重复／陈旧输入队列和 timer
回调会被忽略或清理，不保留失效指针。timer error/hangup 和 read 失败会停止 timer、
取消触控并记录日志；不会用合成输入事件冒充 tick。

bridge 是明确的诊断消费者：每次 read 最多取一个有序脉冲并保存有界快照，供限流
日志观察；`diagnostic_reads` 只表示 bridge 消费了快照，不表示客体／游戏 HID
收到。UP 被拒绝时先排空一个脉冲重试；仍不能排队则回退 `cancel_all`，16 槽环满
不会留下卡住指针。即使平台 CANCEL 帧没有可用坐标／触点，也会释放全部所有权。

当前 App 仍是自有诊断：消费快照只写日志，没有连接游戏状态／HID／HUD。真实客体
HID 与游戏手势／验收路径仍是 #32/#33–40 的后续工作。

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
