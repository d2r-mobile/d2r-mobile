# #149 自有 AAudio PCM16 输出边界

## 范围

`dm/android/audio/dm_aaudio.{h,c}` 是独立于 guest 音频、App focus、路由和 WSI 的自有 Android AAudio 输出切片。只接受 interleaved PCM16、双声道、48 kHz；其他格式在 open 后协商校验失败时关闭并返回 `-EPROTO`，不静默降级。

创建回滚所有权：open 后协商失败且 `close` 成功时释放对象、`out=NULL`、返回 `-EPROTO`。若回滚 `close` 失败，stream 仍持有 callback userdata，**不能**释放对象；此时返回 `-EAGAIN`，`*out` 是调用者拥有的 cleanup handle，必须串行调用 `dm_audio_destroy` 直到返回 0。失败的 destroy 返回 `-EIO` 并保留 handle；不会 NULL 二次 close 或丢弃资源。

## AAudio 边界

生产默认表在 Android 指向真实 `libaaudio`：`AAudio_createStreamBuilder`、format/channel/rate/direction/data+error callback 设置、`AAudioStreamBuilder_openStream`、`getFormat/getChannelCount/getSampleRate`、`requestStart/requestPause/requestStop`、`getState/waitForStateChange`、`close`。宿主测试只替换这张窄表；不做 Apple 声卡或 silent sink 验收。

## 队列与回调

- 单生产者固定环形队列，容量向上取 2 的幂；`dm_audio_write` 只复制可容纳的完整帧，返回实际写入帧，队列满的剩余帧计入 `frames_dropped`，不覆盖未消费样本。
- 数据回调只做原子索引、`memcpy`/`memset`，无文件、无分配、无锁、无阻塞；缺帧部分填零，并分别计数 `underrun_frames`/`underrun_events`，不把静音称为已播放数据。
- 回调返回 `AAUDIO_CALLBACK_RESULT_STOP` 当 closing。

## 生命周期

- `dm_audio_destroy` 先拒绝回调线程重入，置 closing，调用 `requestStop`，检查状态并在未 STOPPED 时 `waitForStateChange`，**等已进入的 data callback 退出后才 `close`**，然后再确认 active 为 0 才释放队列和控制块。AAudio 文档明确 requestStop 后不再开始新 data callback，且 callbacks 不会多线程并发。
- teardown 有显式状态：`ACTIVE → DESTROYING`；`close` 失败进入 `CLOSE_FAILED` 并保留仍可关闭的 stream，串行 retry 会再次 `close`；`close` 成功但 `stop`/`wait` 未证明进入 `CLOSED`，retry 只做最终释放，不会对 NULL 二次 close。`DESTROYING` 期间的并发 destroy 返回 `-EBUSY`；`-EIO` 表示本轮未完成、对象仍归调用者，成功后对象已释放。
- 错误回调只记录 disconnected；不回调内 stop/close。disconnect 后 producer write 返回 0，start 返回 `-ENOTCONN`。
- 公开 start/pause/stop/destroy 需宿主串行化；AAudio close 本身不是线程安全的。

## 证据边界

`dm/android/tests/audio_aaudio.c` 驱动实际生产模块，覆盖 PCM 顺序、多块、部分消费/欠载、溢出、格式协商拒绝、open/start/pause/stop/close/wait 失败、disconnect 和关闭清理。host boundary stub 不是 Android 实际播放、延迟、路由、音量或 AV sync 证据；手机/设备执行未做。父 #21 保持开放。
