# #165 仲裁器只读观察与原子几何

## 范围

为现有 `dm_input_arbiter` 增加最小只读视图和原子几何接口，供下一步 App 把输入状态收拢到一个仲裁器。仲裁器仍只拥有自己的实际 `dm_touch_bridge`，不暴露可销毁/绕过仲裁的裸 bridge 指针；不新增 router/framework，不改 App/native、bridge/adapter/core/provider/手柄算法。

## API

- `dm_input_arbiter_observe(arbiter, out)`：用仲裁器当前 density 对同一实际 bridge 做 `dm_touch_bridge_observe`；不读 pad、不消费 pulse/request、不推进 timer、不改变 source/sampling/ownership。
- `dm_input_arbiter_diag(arbiter, out)`：复制同一 bridge 的 `dm_touch_diag` 快照；不返回内部指针、不消费。
- `dm_input_arbiter_set_geometry(arbiter, width_pt, height_pt, density, time_ns)`：先校验有限正 width/height/density 和 `time_ns >= 0`；非法调用返回 0 且不改状态。几何完全相同是 no-op，保留 held 输入、待发边界和请求。真实变化更新实际 bridge 和 takeover probe 缓存的 width/height/density，退休两个来源并设置 fresh-required，至少一个合法新手势才能重新取得来源。
- 失败回滚：bridge resize/density 更新任一步失败时恢复旧几何，返回 0，不更新仲裁器字段；不向外泄漏部分几何状态。

## 保留边界

`focus/pause`、host belt/portal/context、skill set 所有权不变；`set_geometry` 只用既有 bridge resize/density 与来源失效路径。`take_touch` 的 staged probe 现在复用缓存，并在验证后退休其临时状态，避免旧密度/旧尺寸验证新触摸。

## 验证与限制

`dm/android/tests/input_source_view.c` 覆盖实际 arbiter→bridge→adapter：重复 observe/diag 不消费 belt 请求和 bottom-row pulse，diag 不推进 diagnostic read；非法/相同/变化几何；新尺寸/密度下 down 与旧 pointer 拒绝；新 gesture 可用；probe cache 实际更新。`input_source_view_mutants.py` 的 `omit-probe-cache` 与 `consume-observe` 必须失败。

Host ASAN/UBSAN 与 Android arm64 模块交叉编译只证明该 C 边界；不证明真实 App 接线、Android 事件投递、真机时序、provider/game-bound 行为或 full touch。App 接线在后续任务完成。
