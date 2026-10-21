# Android InputManager 手柄移除/变化接线 (#183)

`ControllerHotplugBoundary` 是打包在诊断 APK 内的窄 Java 边界：它只监听真实 `InputManager` 的 added/removed/changed 事件，并把当前 session 的 removed/changed device ID 传给 native。没有通用设备 registry。

## Java 边界

- 构造时通过 `Activity.getSystemService(Context.INPUT_SERVICE)` 取得真实 `InputManager`，要求主 looper；用 main `Handler` 注册 listener。
- `register()` 在主线程调用真实 `registerInputDeviceListener`；异常返回 false，不声称注册成功。
- `release()` 先失效 released/session，再 `unregisterInputDeviceListener`；幂等，取消注册异常如实返回 false 并保留 pending 注册以便窄重试，迟到回调在 Java 与 native 两层都被丢弃。重复成功不二次注销。
- added 只转发；native 对 added 不 admission、不偷 touch。removed/changed 由 native 只对当前 admitted device ID 生效。

## Native 绑定与生命周期

`native.c` 在 `onCreate` 通过 Activity 的 `ClassLoader.loadClass` 加载打包类，`RegisterNatives` 绑定静态 `nativeControllerEvent(JII)V`，并调用 `register()`。loader 从 Activity 实例的 `Context.getClassLoader()` 取得（不是 `Class.getClassLoader()` 的定义/boot loader）。JNI 每步检查异常并清理；连续 `NewGlobalRef` 逐次检查失败，前一失败清异常后才允许下一步；加载/注册/global ref 失败只记录失败，不声称热插拔可用。

先执行显式 owner 获取：进程内已有另一个 live owner 时直接 busy 失败，第二 NativeActivity 不会覆盖/清除第一个 owner，也不会进入 JNI 注册或修改自身状态。owner 停止/失败只释放自己的 owner；第一个 owner 停止后第二实例才可获取。

回调没有把 native 指针存进 Java。进程内只有一个 `g_controller_app`，并与 `controller_session` 一起校验；`destroy` 先清除全局 owner/session、调用 Java `release()`（内部先失效身份再 unregister），随后才销毁 arbiter/释放 app。回调只使用当前 session 的 admitted ID；matching remove/change 调用既有 `gamepad_rearm` + neutral output，wrong ID/add/stale session 不变。物理热插拔、provider/guest 和真机验收仍属于 parent 缺口。

## 验证

- 真实 `android.jar` 编译全部 app/java；host 窄 `InputManager`/Activity/Handler/Looper stub 运行 24 checks（注册、added/removed/changed、release 迟到、失败注册、失败 unregister、主线程拒绝）。
- actual native callback harness：matching remove/change、add/wrong-id、stale session、new device，及 hotplug ID mutation。
- Android arm64-v8a API29 `dm_android_app` 编译链接。
- 自有诊断 APK 构建通过，`classes.dex` 包含 `ControllerHotplugBoundary` 与 `nativeControllerEvent`。

不执行 phone/adb/物理手柄/game/SDK runtime/rtld；host/APK 构建不证明真机热插拔或完整设备验收。
