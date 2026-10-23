# Android settings and controls / Android 设置与操作

Android 支持 Turnip／系统驱动选择、显示比例、提交帧率显示以及 30 帧省电模式。

Android supports Turnip or system-driver selection, aspect selection, a submitted-FPS display and 30 fps power-saving pacing.

## Settings / 设置

- `AndroidPortSettings.java` owns startup controls and the private `android_port_options_v1` preferences. Game resource imports do not include this file. Same-package, same-signature APK updates preserve preferences; uninstalling or clearing app data does not.
- `16:9` uses a centered surface and 1280×720 game output. Phone widescreen uses aspect-aware output at height 720 (1600×720 on the test phone), without stretching the 16:9 image. Internal 3D scale remains unchanged.
- FPS counts successful/suboptimal queue-present submissions over elapsed time. It is labelled submitted FPS, not a measurement of evenly spaced panel scanout. Turning it off hides the view and stops its periodic callback.
- Driver selection uses the private `use_turnip` preference, defaulting to Turnip. The native loader validates the selection and Turnip identity; a Turnip failure never silently selects the system driver. Both choices use Vulkan. The FPS label follows the selected driver.

设置独立于游戏导入目录保存；同包名、同签名覆盖安装会保留。卸载或清除应用数据会删除设置。宽屏调用游戏窗口的比例适配；增加像素后需要另测性能。FPS 是提交帧率，不能据此判断每帧上屏是否均匀。

## Physical controllers / 实体手柄

Buttons and sticks feed the existing host controller input; clear controller input selects the original controller layout, and touching the screen returns to the touch layout. No game resource reimport is required for the current matching overlay.

支持实体手柄与触屏／手柄界面切换。额外修复启动页尚未装载 JNI 时的输入保护、暂停时纯模拟扳机与十字键清零、设备断开通知，以及十字键按键与轴重复上报时的合并。保留我们已有的驱动、比例、FPS 和 30 帧接线。

Local integration guards JNI until the game starts, registers Android device-removal callbacks, releases all controller channels on pause/focus loss, and merges d-pad key/axis reports independently. State-machine regression checks and instructions are in `tests/pad-input/README.md`. Physical-controller and Android Auto device acceptance are separate from these checks; Android Auto support is not added by this PR.

## App updates / 应用更新

Keep the preference filename and keys stable. Verify both aspect modes, touch coordinates, FPS off/on and an APK update preserving preferences. Game imports must pass the product/version checks.

保持设置文件名与键名稳定，更新后复验两种比例、触控、FPS 开关和覆盖安装保留设置。游戏包仍须通过产品与版本检查。

## Test build dependencies / 测试版依赖

`-PdmTestDriverLibs` points to the existing arm64 adrenotools JNI libraries. `-PdmTurnipAssets` points to an assets root containing the trusted `turnip/vulkan.purple.so` (SHA256 `1d80dfa019659b008e4669311db5b1e4a02af59ff1d5458a98e2f5fe18ed013b`). The APK bundles this library; first use atomically extracts it into versioned private `files/gpu/turnip-1d80dfa0/`, outside imported game resources. Update that directory version when replacing the bundled driver. No driver binaries or game files are committed or downloaded by the application. System selection loads the platform Vulkan loader without adrenotools.

`test-volk` is MIT-licensed volk pinned to `0dc3ce00bf98b9f0b6fe708ca0f7eb74e2830173`, used for dispatch through the explicitly selected driver. A source build still needs the trusted external driver build inputs above.
