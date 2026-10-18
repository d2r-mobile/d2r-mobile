# #159 输入来源请求退休

## 范围

Android 自有触控链新增一个显式的“当前输入来源退休”操作，仅处理输入来源切换、focus loss、pause、queue 更换和游戏手柄接管时仍留在 Android 桥中的请求。它不执行游戏动作、不改上游共享核心、不改变地址/协议、不宣称真机或 guest 已完成。

## API

- `dm_android_touch_retire_requests(touch, time_ns)`：调用已有的 `dm_android_touch_cancel_all`，然后由 Android 适配层清除上游 `dm_tc_cancel_all` **有意保留**的 `belt_request`，并清除未读的 Android portal request。上游核心不变。
- `dm_touch_bridge_retire_requests(bridge, time_ns)`：实际 bridge 入口，取消手指/脉冲/待发动作，清空 outer active 表并消费 neutral 快照。仅 standalone 支持；attach 了 provider 时在 mutation 前返回 0，active pointer/output/provider observable state 不变，不消费 provider action。
- `dm_input_arbiter` 的 `clear_touch` 现在走 bridge retirement；因此来源切换、gamepad 接管、focus loss、pause、queue replacement 都会退休未消费的 belt/portal 请求。

## 保留与失效

- 保留 belt rows/open、portal available/mask/identity/scene、game availability、skill set 等 host context。
- `dm_tc_cancel_all` 增加 gesture epoch；退休后的旧 portal ack 失败，迟到 MOVE/UP 找不到旧 pointer。
- 新的合法手势在同一 context 下仍能产生新的 belt/portal request；重复 retirement 幂等。
- 普通 `dm_android_touch_cancel_all` / `dm_touch_bridge_cancel_all` 的既有语义不变，旧测试仍确认 belt_request 在 plain cancel 后保留。

## 验证与限制

`dm/android/tests/touch_bridge.c` 覆盖真实 core→adapter→bridge：未消费 belt 请求被退休、新 gesture 仍工作、portal 旧 epoch ack 失败、新 portal gesture 可用、pending pulse 不复活。`dm/android/tests/input_source_requests.c` 用实际 arbiter invalidation 走真实 bridge，并以 test-only accessor 检查 pending request；`input_source_mutants.py` 的 omission mutant 必须失败。

Host ASAN/UBSAN 和 Android arm64 module cross 只证明该桥的 C 边界；不证明真实 Android 事件投递、game provider 未消费动作、游戏语义、真机时序或 full touch。attach 了 provider 的 bridge 明确拒绝 retirement（返回 0，先于任何 mutation）；provider pending action 的退休仍是父 #150/#39/#19 的接线缺口，不推断 provider/game-bound 行为。
