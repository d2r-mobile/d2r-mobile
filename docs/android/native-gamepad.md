# NativeActivity 物理手柄路由 (#174)

`native.c` 将 Android gamepad/joystick `AInputEvent` 通过真实 `gamepad_events` converter 送入现有唯一 `dm_input_arbiter`。没有第二个 bridge、router 或 ID 映射 registry。

## 入口与身份

`input_ready` 在保留 BACK、touchsource、`preDispatch`/`finishEvent` exactly-once 的前提下，把 gamepad/joystick source 交给 `handle_gamepad`：

- 读取真实 `AInputEvent_getDeviceId`；负值在 uint32 API 前拒绝。
- 按真实 Android 事件类型读取纳秒时间：`AKeyEvent_getEventTime`（KeyEvent）或 `AMotionEvent_getEventTime`（MotionEvent）。NDK 没有 `AInputEvent_getEventTime`。
- 设备 ID 是非负 scalar（最大 `INT32_MAX`），不是 0..15 映射；generation 0 仍无效。
- 第一个完整验证通过的 event 才能 admission；已 admission 后其他 device 在转换前拒绝，直到 lifecycle reset。没有伪造热插拔检测。

## 转换与仲裁

调用方持有 `dm_gamepad_context` 和上一份 `dm_npad_state`。每个 event 先在副本上完整转换，失败则不改变 admission、context、sample 或 touch 来源；成功后转成 `dm_pad_sample` 交给 `dm_input_arbiter_connect/gamepad`。

- neutral sample 可 admission，但不会从 touch 抢 source。
- active sample 才让 arbiter 切换 source；同一 arbiter 产生一次 first-neutral boundary。
- 合法 touch 接管后立即清空旧 controller 的 converter 贡献与 prior sample（保留 admission/id/generation）；被拒绝的 touch 不清空。随后 neutral joystick 不会复活旧按键或偷回来源。
- `gamepad_source` 与 converter 共用完整 Android source 成员判断：接受包含完整 GAMEPAD 或 JOYSTICK 位的组合 source，拒绝纯 mouse/keyboard；touch 仍在入口优先。
- 数字 KeyEvent 与模拟 MotionEvent 的 HAT/trigger 贡献由 converter 组合，互不清除。
- 新 touch DOWN 仍按既有 first-neutral 路径回到 touch；没有 source OR。

## 生命周期与时间边界

pause、focus loss、queue 替换/丢失、窗口几何变化和 destroy 都调用 `gamepad_rearm`：清 converter、prior sample、admission，并记录当前 monotonic 边界；之后旧时间戳事件被拒绝，恢复后必须新的有效输入。真实热插拔/断开回调未实现，保留为 parent 缺口。

## 验证限制

`native_callbacks_host.py` 链接实际 `native.c` 函数、`gamepad_events`、`input_source`、touch bridge/adapter/core 和 `view_policy`，只 stub NDK/window/looper。Android arm64-v8a API29 `dm_android_app` 编译链接通过。

Host/编译结果不证明真机事件投递、设备热插拔、provider/guest HID、音频或完整 touch；这些 parent 仍 open。
