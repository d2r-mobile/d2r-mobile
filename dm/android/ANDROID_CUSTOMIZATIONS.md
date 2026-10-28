# Android settings and controls / Android 设置与操作

Android 支持 Turnip／系统驱动选择、显示比例、提交帧率显示以及 30 帧省电模式。

Android supports Turnip or system-driver selection, aspect selection, a submitted-FPS display and 30 fps power-saving pacing.

## Settings / 设置

- `AndroidPortSettings.java` owns startup controls and the private `android_port_options_v1` preferences. Game resource imports do not include this file. Same-package, same-signature APK updates preserve preferences; uninstalling or clearing app data does not.
- `16:9` uses a centered surface and 1280×720 game output. Phone widescreen uses aspect-aware output at height 720 (1600×720 on the test phone), without stretching the 16:9 image. Internal 3D scale remains unchanged.
- FPS counts successful/suboptimal queue-present submissions over elapsed time. It is labelled submitted FPS, not a measurement of evenly spaced panel scanout. Turning it off hides the view and stops its periodic callback.
- Driver selection uses the private `use_turnip` preference, defaulting to the system driver. The native loader validates the selection and Turnip identity; a Turnip failure never silently selects the system driver. Both choices use Vulkan. The FPS label follows the selected driver.

设置独立于游戏导入目录保存；同包名、同签名覆盖安装会保留。卸载或清除应用数据会删除设置。宽屏调用游戏窗口的比例适配；增加像素后需要另测性能。FPS 是提交帧率，不能据此判断每帧上屏是否均匀。

## Touch menu commands / 触屏菜单命令

Virtual navigation buttons release after one game input sample, preventing the same press from activating the next dialog. The button keeps ownership of its contact until the finger lifts. Gameplay skill holds and item hold/drag timing keep their existing behavior. These buttons remain hidden by default; this fix covers their alternate/debug path.

虚拟导航圆钮在游戏采样一次后释放，防止同一次按压重复触发下一级对话。手指抬起前，触点仍归该按钮所有。技能长按与物品长按、拖动保持原有时序。圆钮默认隐藏，此修复覆盖其备用／调试路径。

## Camera views / 镜头视角

Choose Options → Custom → Camera view for the original, over-the-shoulder or first-person view. The touch camera buttons hide while a physical controller is active. Camera modes require an overlay generated from the same sources as the APK; prepare a matching import pack before updating. Device validation is pending.

在游戏「选项 → 自定义 → 镜头视角」选择原版、越肩或第一人称视角。使用实体手柄时，触屏转向按钮会隐藏。镜头模式需要与 APK 同份源码生成的覆盖层，更新前请准备匹配的导入包，待实机验证。

## Startup save backups / 启动页存档备份

Export and import are available only before the native game starts. Exit normally and reopen the application before managing saves. The system document picker can save the ZIP to Downloads or an installed cloud document provider; this does not implement automatic cloud synchronization. Only ZIPs exported by this feature are supported. Restore replaces the complete committed `files/save` directory, including characters, shared stash and saved settings. It does not change game resources, `save.work`, or Android startup preferences.

导出／导入只在游戏启动前可用。请先正常存盘退出，再重新打开应用。系统文件选择器支持下载目录及已接入的云盘；不提供自动云同步。导入会替换全部角色、共享储物箱和存档设置，不修改游戏资源或启动设置。请先导出一份外部备份。

Archives are validated in a staging directory before replacement: version marker, relative `save/` paths, no duplicate entries, at most 4096 entries plus the marker, 64 MiB per file and 512 MiB in total. Symlink exports are rejected. Before replacing existing saves, a ZIP is retained in private `files/save-backups/before-import-<UUID>.zip`; the previous directory also remains as an interrupted-swap fallback. Startup recovers that directory if the committed directory is missing. These private backups are removed by uninstall/clear-data and are not a substitute for an exported ZIP. They can accumulate with repeated imports. Filesystem rename recovery does not guarantee durability after power loss.

导入前先在暂存目录校验路径、数量、大小和版本，再保留本机旧存档备份并换目录。私有备份随卸载／清除数据一起删除，反复导入也会增加占用，不能代替保存到下载或云盘的外部 ZIP。断电持久性不作保证。手机文件选择器与实际存档恢复仍需实机验收。

Local archive regression (synthetic saves only):

```sh
javac -encoding UTF-8 -d /tmp/dm-save-tests dm/android/app/src/main/java/com/xiaoboluo/d2r/SaveArchive.java dm/android/tests/startup-save/SaveArchiveTest.java
java -cp /tmp/dm-save-tests com.xiaoboluo.d2r.SaveArchiveTest
```

## Physical controllers / 实体手柄

Buttons and sticks feed the existing host controller input; clear controller input selects the original controller layout, and touching the screen returns to the touch layout. No game resource reimport is required for the current matching overlay.

支持实体手柄与触屏／手柄界面切换。额外修复启动页尚未装载 JNI 时的输入保护、暂停时纯模拟扳机与十字键清零、设备断开通知，以及十字键按键与轴重复上报时的合并。保留我们已有的驱动、比例、FPS 和 30 帧接线。

Local integration guards JNI until the game starts, registers Android device-removal callbacks, releases all controller channels on pause/focus loss, and merges d-pad key/axis reports independently. State-machine regression checks and instructions are in `tests/pad-input/README.md`. Physical-controller device acceptance is separate from these checks.

## Android Auto parked games / Android Auto 停车游戏

The same `DmActivity` declares `MAIN` + `CAR_LAUNCHER` and is resizable for the projected window. Android Auto parked games require Android 15 or newer. Both drivers still use the phone GPU; game files, saves, touch controls and physical-controller routing remain on the phone. Existing surface resize callbacks update the Vulkan window; touch controls share the same game viewport. Existing pause/focus-loss callbacks clear input and notify the host. Android Auto controls parked-only availability and automatically exits the app when vehicle motion is detected.

复用现有游戏 Activity、触屏和手柄输入，增加停车游戏入口及窗口尺寸适配。需要 Android 15 或更新版本以及支持停车游戏的 Android Auto。画面由手机渲染，存档保留在手机，不需要重新导入游戏数据。先在手机完成资源导入和文件访问授权，再从车机启动。宽屏设置使用当前游戏窗口的比例。

For this locally installed test APK, enable Android Auto developer settings and its **Unknown sources** option, then reconnect Android Auto while parked. This is the documented parked-app development path; it does not bypass driving restrictions. Look for **D2R Mobile** in the car's app launcher (the test package is `com.xiaoboluo.d2r`). Pair a controller with the phone, or use the car touchscreen. Verify launch, aspect/touch alignment, controller takeover, leaving/reopening the app, audio pause/resume and disconnect while parked. A phone launch or synthetic secondary display alone does not establish Android Auto compatibility; DHU or vehicle validation is required.

本地安装版需要在 Android Auto 自身的开发者设置中开启“未知来源”，停车后重新连接，在车机应用列表选择 D2R Mobile。手柄连接手机。验收需要检查启动、比例与触控坐标、手柄切换、离开和恢复、声音及断开连接。手机启动和模拟副屏检查不能代替 DHU／真实车机验收。

References: [parked app manifest](https://developer.android.com/training/cars/parked/auto), [parked lifecycle](https://developer.android.com/training/cars/platforms/android-auto#parked-apps), [local vehicle testing](https://developer.android.com/training/cars/testing#allow-unknown-sources).

## App updates / 应用更新

Android 2.0.1 includes widened-stash position restoration, controller-legend cleanup, BC4/BC5 filtering compatibility, Adreno 740/750 shader settings and compute-pipeline failure handling. Adreno 830 keeps its existing defaults. These fixes require an APK update; no game resource reimport is needed.

Android 2.0.1 修复加宽储物箱的位置恢复、手柄提示清理、BC4／BC5 过滤兼容及计算管线失败处理，并调整 Adreno 740／750 的着色器设置。Adreno 830 保持现有默认值。这些修复只需更新 APK，无需重新导入游戏资源。

Keep the preference filename and keys stable. Verify both aspect modes, touch coordinates, FPS off/on and an APK update preserving preferences. Game imports must pass the product/version checks.

保持设置文件名与键名稳定，更新后复验两种比例、触控、FPS 开关和覆盖安装保留设置。游戏包仍须通过产品与版本检查。

## Test build dependencies / 测试版依赖

`-PdmTestDriverLibs` points to the existing arm64 adrenotools JNI libraries. `-PdmTurnipAssets` points to an assets root containing the trusted `turnip/vulkan.purple.so` (SHA256 `1d80dfa019659b008e4669311db5b1e4a02af59ff1d5458a98e2f5fe18ed013b`). The APK bundles this library; first use atomically extracts it into versioned private `files/gpu/turnip-1d80dfa0/`, outside imported game resources. Update that directory version when replacing the bundled driver. No driver binaries or game files are committed or downloaded by the application. System selection loads the platform Vulkan loader without adrenotools.

`test-volk` is MIT-licensed volk pinned to `0dc3ce00bf98b9f0b6fe708ca0f7eb74e2830173`, used for dispatch through the explicitly selected driver. A source build still needs the trusted external driver build inputs above.
