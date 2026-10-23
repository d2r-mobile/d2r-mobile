# Android settings and controls / Android 设置与操作

Android 支持 Turnip／系统驱动选择、显示比例、提交帧率显示以及 30 帧省电模式。

Android supports Turnip or system-driver selection, aspect selection, a submitted-FPS display and 30 fps power-saving pacing.

## Settings / 设置

- `AndroidPortSettings.java` owns startup controls and the private `android_port_options_v1` preferences. Game resource imports do not include this file. Same-package, same-signature APK updates preserve preferences; uninstalling or clearing app data does not.
- `16:9` uses a centered surface and 1280×720 game output. Phone widescreen uses aspect-aware output at height 720 (1600×720 on the test phone), without stretching the 16:9 image. Internal 3D scale remains unchanged.
- FPS counts successful/suboptimal queue-present submissions over elapsed time. It is labelled submitted FPS, not a measurement of evenly spaced panel scanout. Turning it off hides the view and stops its periodic callback.

设置独立于游戏导入目录保存；同包名、同签名覆盖安装会保留。卸载或清除应用数据会删除设置。宽屏调用游戏窗口的比例适配；增加像素后需要另测性能。FPS 是提交帧率，不能据此判断每帧上屏是否均匀。

## App updates / 应用更新

Keep the preference filename and keys stable. Verify both aspect modes, touch coordinates, FPS off/on and an APK update preserving preferences. Game imports must pass the product/version checks.

保持设置文件名与键名稳定，更新后复验两种比例、触控、FPS 开关和覆盖安装保留设置。游戏包仍须通过产品与版本检查。

## Test build dependencies / 测试版依赖

`-PdmTestDriverLibs` points to the existing arm64 adrenotools JNI libraries. A trusted Turnip library is provisioned separately as `files/dm/turnip/vulkan.purple.so`. This test branch does not contain or download driver binaries or game files. Startup requires Turnip and checks the Vulkan driver identity; it does not silently select the Qualcomm system driver.

`test-volk` is MIT-licensed volk pinned to `0dc3ce00bf98b9f0b6fe708ca0f7eb74e2830173`, used for dispatch through the explicitly loaded driver. Driver packaging for a general release is separate from this provisioned-phone build.
