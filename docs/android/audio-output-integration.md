# 音频输出组合（#163）| Audio output composition (#163)

本切片把已接受的焦点/意图策略（`dm/android/app/audio_focus.c`，PR #155）与真实 AAudio 输出模块
（`dm/android/audio/dm_aaudio.c`）组合成一个最小 owner：`dm_android_app_audio_output`。策略只回答"该不该播、
多大声"，真实 PCM 由 AAudio 的 data callback 输出，音量在回调内以 Q15 定点应用。
This slice composes the accepted focus/intent policy with the real AAudio output module into one minimal owner. The
policy only decides whether to play and how loud; the AAudio data callback produces the actual PCM and applies gain
in Q15 fixed point inside that callback.

## 音量语义 | Gain semantics

- 有界有限值 `0..1`。非法、NaN、越界返回 `-EINVAL`，当前音量保持不变。
- `1.0` 位精确保留样本（回调直接返回原样本）；`0` 是有意静音，PCM 全零，但**不**计入 underrun。
- 应用点是真实 data callback：已入队但尚未回调的帧也服从最新音量（"already written, not yet played"）。
- 量化：Q15（1.0 = 32768），`(sample * q15 + 16384) >> 15`，即带 +0.5 LSB 偏置的最近 LSB 取整，负样本同一规则。
  乘积上界 `2^30`，加偏置仍在 int32 内，无溢出；结果再钳位到 int16 范围。
- 回调内只有一次 relaxed 原子读取和整数乘法：不分配、不加锁、不做 IO，控制线程可在任何时刻更新音量。

Bounded finite `0..1`; invalid/NaN/out-of-range is refused with `-EINVAL` and the current gain is unchanged. `1.0`
preserves samples bit-exactly, `0` is intentional mute (all-zero PCM, not underrun). Gain is applied in the real data
callback, so queued frames obey the latest value. Quantisation is Q15 with `(sample*q15 + 16384) >> 15`, documented
above with its overflow bound. The callback does one relaxed atomic load and integer multiplies only.

## 句柄所有权 | Handle ownership

`dm_audio_output` 保留**所有** AAudio 句柄，与策略自己的 started/owned 视图无关：
The owner retains **every** AAudio handle regardless of the policy's own started/owned view:

- `dm_audio_create` 返回 `-EAGAIN`（协商失败且回滚 close 失败）时，清理句柄归 owner，标记 `cleanup_only`，
  不启动；后续串行 `dm_audio_output_destroy` 重试，直到 close 成功。
- `dm_audio_start` 失败时已打开的流仍由 owner 保留，策略进入 ERROR；destroy 会退休该句柄。
- `dm_audio_destroy` 失败时句柄保留，`destroy_pending`/`destroy_retries` 记录，调用方可串行重试。
- 策略 `USER_STOP` 只停止流，不释放句柄；句柄只在 destroy 成功后消失。
- **断流恢复**：真实 AAudio error callback 断流后，下一次串行控制调用（stop/start）会退休坏流并在控制线程上
  真正 `open` 新流；坏流从不被"清除 disconnected"后复用。退休走 destroy，因此 stop 请求失败仍会尝试 close，
  close 失败则保留句柄等串行重试；`opens`/`replacements` 记录真实新建与替换次数。
- data/error callback 从不调用策略、从不 close、从不 destroy；策略与 AAudio 控制只发生在调用方的控制线程。

**Disconnect recovery**: after the real AAudio error callback disconnects a stream, the next serialized control call
(stop/start) retires the bad stream and really opens a new one on the control thread; a disconnected stream is never
"cleared" and reused. Retirement goes through destroy, so a failing stop request still attempts close, and a failed
close keeps the handle for a serialized retry; `opens`/`replacements` count the real opens and replacements.

A create `-EAGAIN` cleanup handle, a stream left open by a failed start and a stream whose close failed are all kept
here and retired by later serialized destroy calls. Callbacks never call the policy or close anything.

## 调用方约束 | Caller contract

- 控制调用（`handle`/`handle_generation`/`destroy`）在单一控制线程串行执行。
- 调用 `destroy` 前必须停止外部生产者；`dm_audio_output_write` 是单生产者。
- host 测试与 Android 交叉链接只证明 PCM 组合、所有权与链接，**不**证明设备播放、延迟、AV 同步或真实音频硬件行为。

Control calls are serialized on one thread; external producers must be stopped before destroy; `write` is
single-producer. Host tests and the Android cross-link prove PCM composition, ownership and linking only — not device
playback, latency, AV sync or real audio-hardware behaviour.
