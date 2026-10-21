# #200 NativeActivity 音频调用方

`app/native_audio.c/h` 是 Accepted `audio_session`（PR197）在真实 NativeActivity 中的唯一调用方；只由主 looper
输入回调和既有输入 timer 驱动，AAudio 数据回调不进入本模块。

## 控制契约

| 事件 | 行为 |
| --- | --- |
| `ANativeActivity_onCreate` | 仅保存 JavaVM/Activity global ref 与选项；不创建 session、不请求焦点、不开流。 |
| 键盘 `P`（DOWN，`AINPUT_SOURCE_KEYBOARD` 且非 gamepad，repeat=0，App active+resumed+focused） | 首次显式请求时 lazy 创建 `dm_audio_session`，调用真实 `dm_audio_session_play`；真实 GRANTED/DELAYED/FAILED 原样保留。 |
| 键盘 `S`（同上条件） | `dm_audio_session_stop`：停止输出并 abandon 同一平台请求；不自动重放。 |
| 输入 timer | 串行调用 `dm_audio_session_drain`，按单调时钟以 48kHz 供给自有 PCM：跨 tick 保留 ns×rate 的亚帧余量，精确累计 elapsed（长 elapsed 先钳制到 4096 帧预算避免乘法溢出）；写入 session 的接受帧数推进 PCM 相位；有界 backlog 4096 帧（约 85ms），队列满/partial write 保留未写样本随后续 tick 续写。停止/暂停/基线重置同时清空余量，恢复不突发旧音频。 |
| 输入队列退休 / 窗口丢失 | 先验证该回调指向当前 queue/window（陈旧回调零副作用），再暂停同一 owner 的音频；resume 不重放旧请求。 |
| 窗口焦点丢失 | 同暂停：停输出并放弃意图；焦点恢复不自动播放，需要新的显式 `P`。 |
| `onDestroy` | 先停生产者，再做有界串行 cleanup（最多两次 session destroy，覆盖 dm_audio close 成功但 stop/wait 未证明的两阶段释放）；未证明零 handle 时保留 App 状态与 JNI/AAudio handle，不释放。 |

## 明确限制

- 未运行手机/adb/音频硬件/游戏/SDK runtime；不声明实际可听播放、焦点仲裁、延迟或 AV 同步。
- host 测试只把 AAudio API 表替换为窄 fixture，其余为真实 `native_audio → audio_session → dm_afj → audio_output → dm_af_policy → dm_audio`。
- PCM 是自有诊断合成信号，不是游戏音频，也不读取私有资产。
- 无 autoplay：创建/恢复/焦点恢复都不产生播放意图；只有显式键盘请求可以。
