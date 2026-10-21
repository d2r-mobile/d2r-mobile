# 串行音频会话（#195）| Serialized audio session (#195)

`app/audio_session.c/h` 把两个已验收模块接起来：JNI 平台焦点 `dm_afj`（PR185）与真实音频输出 owner
`dm_audio_output`（PR164，内含 `dm_af_policy` + 真实 `dm_audio`）。不复制队列、注册表或策略；焦点事件走
`dm_afj` 的有界环，播放/增益/流由 `dm_audio_output` 决定。
`app/audio_session.c/h` connects the two accepted modules: the JNI platform-focus adapter `dm_afj` (PR185) and the
real audio output owner `dm_audio_output` (PR164, containing `dm_af_policy` + real `dm_audio`). No queue, registry or
policy is duplicated: focus events use `dm_afj`'s bounded ring and playback/gain/streams stay in `dm_audio_output`.

## 控制契约 | Control contract

所有函数在 Android 主线程串行调用（JNI 边界会拒绝其它线程）。构造不请求焦点、不开流、不启动。
All functions are serialized on the Android main thread (the JNI boundary rejects other threads). Construction
requests no focus, opens no stream and starts nothing.

| 调用 | 行为 |
| --- | --- |
| `create(JNIEnv*, activity, options)` | 初始化真实输出策略 + `dm_afj` owner；只保存句柄。 |
- `play(&platform_result)` | 新 focus generation 请求平台焦点；结果原样返回 1/2/0。先按真实结果更新焦点（GRANTED→GAIN；DELAYED→transient loss；FAILED→loss），再提交用户意图，因此旧 GRANTED 不可能让 pending/FAILED 瞬态启动。 |
- 请求失败区分：已验证的 off-main 拒绝无平台副作用，状态/代次不变；其它 JNI/平台失败可能发生在旧请求已 abandon 之后，会作废旧 generation 并派发永久 loss 停到安全状态，迟到 GAIN 不能恢复。 |
| `resume` / `pause` | 只派发 App 生命周期事件；resume 只有在用户意图 + GRANTED + 前台时才真正开始。 |
| `stop` | 策略停止 + 平台 abandon；generation 递增，迟到 GAIN 不会自动播放，需新的显式 play。 |
| `drain` | 从 `dm_afj` 取焦点事件，丢弃旧 generation，映射为 `DM_AF_EV_FOCUS_*` 交给输出策略。溢出停到安全状态并返回 `DM_AS_E_OVERFLOW`。 |
| `write` | 仅 `PLAYING` 时接受 PCM16；stop/pause/error/overflow/destroy 后返回 0。 |
| `destroy(&session)` | 先关闭生产者，再分别释放 JNI owner 与 AAudio handle；任一仍存在即返回错误并保留，绝不在有 handle 时报告成功。 |

焦点请求身份（session `focus_generation` + `dm_afj` token）与 AAudio 流 generation 完全分离：旧请求的迟到回调
被丢弃，当前请求的真实 LOSS 不会被误丢。

## 失败与重试 | Failures and retry

- JNI request/abandon/release 失败返回负错误码，owner 保留；同一主线程可重试。
- AAudio close 失败时 `dm_audio_output` 保留 handle，`destroy` 返回非零，再次调用会重试关闭；成功前不释放 session。
- 溢出 fail closed：停止输出并上报，不静默丢永久 LOSS。
- 平台 abandon 抛异常时 `AudioFocusBoundary` 保留请求身份（PR185 round28），session `stop` 返回错误但输出已停，
  `destroy` 重试会放弃同一请求后才释放。

## 证据与限制 | Evidence and limits

- Host JVM 集成测试（真实 C session + dm_afj + audio_output + policy + dm_audio，仅窄 AAudio fixture 与 Android
  stub）：**72 checks / 0 failures**，覆盖构造空闲、GRANTED+前台才播放、DELAYED→真实 GAIN、FAILED 不播放、
  pause/stop 后迟到 GAIN、permanent LOSS 重新武装、旧请求迟到回调、overflow、JNI abandon 失败重试、AAudio close
  失败保留重试、最终零 handle、真实 PCM 输出。
- `aarch64-linux-android29-clang -Wall -Wextra -Werror` 编译 session 通过；Android CMake target 增量添加。
- 无 `native.c`/App 调用点（Hermes3 拥有）；未安装/运行，不声称设备焦点、播放、延迟或 guest 产物。
- No native.c/App call site (owned by Hermes3); nothing installed or run. Device focus, playback, latency and guest
  producer acceptance are not claimed.
