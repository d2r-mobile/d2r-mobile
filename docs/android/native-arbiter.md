# NativeActivity 单一输入仲裁接线 (#168)

`native.c` 现在只持有一个 `dm_input_arbiter`。MotionEvent、8 ms timer、HUD 观察和诊断读回都使用同一仲裁器；App 不再创建、销毁或直接操作第二个 `dm_touch_bridge`。

## 生命周期

- `onCreate` 创建 arbiter；`onDestroy` 销毁一次。
- `input_created` 的 queue 替换/`input_destroyed` 的 queue 丢失只调用 `dm_input_arbiter_queue_changed`，保留 arbiter 并在新 queue 上复用；不会 double destroy 或复活旧 held/pending。
- `pause_app`/`focus_changed` 走 arbiter pause/focus，退休来源；恢复要求新的 DOWN，迟到 MOVE/UP 被拒绝。
- timer/window 失效走同一 queue-change 退休路径。

## 几何与观察

`touch_resize_window` 使用真实 `dm_view_policy` 的逻辑尺寸，并调用 `dm_input_arbiter_set_geometry(width_pt,height_pt,density,time)`。相同几何不消费状态；真实几何/密度变化退休来源并要求新 DOWN。`onConfig` density 无窗口时也走同一原子接口。

HUD 的 `dm_input_arbiter_observe` 和日志的 `dm_input_arbiter_diag` 只读；首次 source takeover 的中立边界由 `touch_consume` 的 `dm_input_arbiter_read` 消费一次，随后实际触摸输出不会被 observe 吞掉。BACK、来源位过滤、`preDispatch`/`finishEvent` exactly once 语义保持不变。

## 验证限制

`native_callbacks_host.py` 链接实际 `native.c` 函数、arbiter、touch bridge、adapter、core 和真实 `view_policy.c`，只 stub NDK/窗口边界；覆盖首次中立边界、observe 不消费、queue 切换、pause/resume、几何/密度和四个私有 mutant。Android arm64-v8a API29 `dm_android_app` 链接通过。

未增加物理 gamepad 路由或 guest HID；这些仍是后续集成缺口。host/链接结果不证明真机 MotionEvent 投递、provider/game 行为或设备时序。
