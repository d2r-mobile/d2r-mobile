# Android roadmap / Android 待办入口

The playable Android port is developed on `main`. Follow upstream through `upstream/main` and selectively port applicable changes. Turnip is the tested graphics baseline. Basic launch, camp/outdoor gameplay, attacks, skills, pickup, inventory/stash dragging and normal save/exit have user reports; this does not establish every compatibility case or sustained 60 FPS.

可玩 Android 版本以 `main` 为唯一开发基线，按需参考 `upstream/main`。Turnip 为已测试的显示基线。用户已经验证基本启动、营地／野外游玩、攻击、技能、拾取、背包／仓库拖动及正常存盘退出；完整兼容性与持续 60 帧仍需分别验收。

## Work tracked in Issues / 当前任务

| Area / 类别 | Entry / 入口 | Remaining scope / 剩余范围 |
|---|---|---|
| Performance / 性能 |  | Sustained frame times, CPU/GPU work, 30 FPS efficiency and unplugged power/thermal comparisons / 持续帧时间、工作量、锁30能效与拔线功耗温升 |
| Touch UI / 触屏界面 |  | Reachable HUD resources, class skill states and safe-area coverage / 可达HUD资源、职业技能状态、安全区 |
| Input / 操作 | ,  | Combined gestures, trading/controller switching and actual IMEs / 组合手势、交易／手柄切换、真实输入法 |
| Gameplay lifecycle / 游戏流程 |  | Background, lock screen, Surface recovery, failures and extended runs / 后台、锁屏、Surface恢复、异常及长时运行 |
| Saves / 存档 |  | Storage failures, interrupted commits/restores and reload consistency / 存储失败、提交／恢复中断、重载一致性 |
| Audio / 音频 |  | Device listening tests for focus, calls, routing and resume / 焦点、通话、输出设备切换与恢复听测 |
| Graphics correctness / 图形正确性 | , ,  | Shader/reflection boundaries, resource lifetime/failure paths and actual Adreno validation layers / 着色器反射边界、资源寿命／失败处理、真实Adreno验证层 |
| Delivery / 交付 |  | Independent build reproduction, import/upgrade/backup instructions and verified release artifacts / 独立复现构建、导入／升级／备份说明、已验证发布产物 |
| Compatibility research / 兼容储备 | – | Patch size, indirect-count draws and downsampling: semantics/runtime reachability remain unproven / 细分面片、计数间接绘制、降采样的语义和运行可达性仍未证明 |

## Task states and completion / 状态与完成条件

- `type:bug`: a reproduced failure with expected/actual behavior, exact source/APK and sanitized evidence. Close after the fix passes its regression. / 已复现故障，注明预期／实际结果、准确版本和脱敏证据；修复回归通过后关闭。
- `type:feature`: missing behavior with observable acceptance criteria. / 缺失能力，必须有可观察的验收条件。
- `type:performance`: comparable measurements with scene, settings, temperature and charging state. Less work is useful; it alone does not prove higher FPS or lower power. / 同场景、设置、温度和充电状态对照；工作量减少不自动等于提帧或省电。
- `type:verification`: missing coverage for existing behavior. A host fixture, a short device run and a full matrix are different evidence. / 已有能力的未覆盖场景；宿主夹具、短时实机和完整矩阵分别记录。
- `type:maintenance`: bounded build, documentation, dependency or delivery work. / 有明确边界的构建、文档、依赖或交付工作。

`status:in-progress` means someone is actually working on that task. `status:backlog` means no active executor; `status:blocked` identifies a specific unmet prerequisite. A merged PR does not close unrelated unfinished checks. Consolidated or superseded tasks must identify where their remaining requirements are tracked.

`status:in-progress` 只用于确实有人执行的任务；`status:backlog` 表示未在执行，`status:blocked` 应写明具体前置条件。PR合并不等于其他未完成验收自动通过；归并或取代任务必须注明剩余条款去向。

Game files, keys, credentials, saves and raw device logs stay local. Preserve scanning, W^X, memory ownership and failure rollback. Device tests follow the user's current authorization, preserve data, and end with normal save/exit and diagnostic cleanup.

游戏文件、密钥、凭据、存档与原始设备日志保持本地。保留扫描、W^X、内存所有权和失败回滚。实机测试遵循用户当时授权，保留数据，完成后正常存盘退出并清理诊断开关。
