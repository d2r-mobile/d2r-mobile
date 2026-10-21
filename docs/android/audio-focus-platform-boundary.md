# Android AudioManager 焦点边界（#170）| Android AudioManager focus boundary (#170)

`app/java/org/d2rmobile/android/diagnostic/AudioFocusBoundary.java` 是当前诊断 APK 内最小的真实平台焦点边界：
只调用 `AudioManager` / `AudioFocusRequest` / `AudioAttributes`，不创建或操作 AAudio，不修改 `dm_af_policy`，
构造时不会自动请求焦点。
This is the smallest real platform focus boundary packaged in the diagnostic APK. It only uses AudioManager,
AudioFocusRequest and AudioAttributes; it never creates or touches AAudio, never mutates the native policy, and never
requests focus on construction.

## API

| 方法 | 行为 |
| --- | --- |
| `int request(int generation)` | 构建 `AUDIOFOCUS_GAIN`、`USAGE_GAME`、`CONTENT_TYPE_MUSIC`、`setWillPauseWhenDucked(false)` 的请求，返回平台真实结果 `GRANTED(1)` / `DELAYED(2)` / `FAILED(0)`；只有 GRANTED/DELAYED 建立所有权并保存 generation。已释放后返回 FAILED 且不调用平台。 |
| `int request(int generation, boolean acceptsDelayedFocusGain)` | 与 `request(generation)` 相同，但只有 `acceptsDelayedFocusGain=true` 才向真实 `AudioFocusRequest.Builder` 设置 `setAcceptsDelayedFocusGain(true)`；否则平台不会返回 DELAYED。 |
| `boolean abandon()` | 幂等；只放弃自己实际保存的那个 `AudioFocusRequest` 实例；无活跃请求时不调用平台并返回 false。 |
| `void release()` | 幂等；放弃活跃请求并永久拒绝后续 request。 |
| `isActive()` / `generation()` / `droppedCallbackCount()` | 诊断状态；`droppedCallbackCount` 统计因释放/替换/失败请求被丢弃的回调。 |

`request()` 在已有请求时会先放弃旧请求，再申请新请求；旧请求的 token 立即失效。

## 事件与 generation

平台回调映射为 `EVENT_GAIN(0)`、`EVENT_LOSS(1)`、`EVENT_LOSS_TRANSIENT(2)`、`EVENT_CAN_DUCK(3)`，与
`dm_af_event` 的焦点子集数值一致，JNI 可直接映射。未知平台码直接忽略，不发明事件。监听器携带请求时的
`generation`；`release()` 之后或 token 不匹配（旧请求/已放弃请求）的迟到回调被丢弃并计入 `lateCallbackCount()`。

## 线程/调用方契约 | Thread and caller contract

- **所有控制调用（request/abandon/release）必须在 Android 主 looper 上执行。** 其它线程调用会抛
  `IllegalStateException`，在触碰平台状态之前被拒绝；不存在跨线程竞争，也没有暂存队列。
- 平台监听器用主 looper 的 `Handler` 注册（`setOnAudioFocusChangeListener(listener, mainHandler)`），
  Android 在 `requestAudioFocus` 返回后按队列 FIFO 投递；回调也在主 looper 上执行，与 request 同一线程、不并发。
- 每个排队回调在投递时重新校验请求 token 与 released；release/替换后旧请求的队列事件被丢弃并计数，不会在新
  generation 之后送达。listener 内调用 `release()` 会立即使同一队列中更旧的事件失效。
- 若平台违约在非主线程或同步回调（不经 main Handler），边界 fail closed：不转发、不伪造成功，只计入
  `droppedCallbackCount()`。
- 监听器内**不得**同步调用 AAudio 或原生策略；必须把事件入队回串行控制线程，再作为 `DM_AF_EV_FOCUS_*`
  交给 `dm_af_policy`。
- 本类不阻塞等待焦点回调；`DELAYED` 只表示平台延迟授予，不假装已获得焦点。延迟授予必须由调用方显式 opt-in。
- 后续 JNI 集成必须先把控制调用 marshal 到主 looper（例如向主 looper 的 `Handler` post），构造
  `AudioManager`（来自 `Context`），并在释放前先 `abandon()`。

## 打包与验证 | Packaging and evidence

`app/build.sh` 用本地 SDK 的 `android.jar` 编译 Java，再用 build-tools 的 `d8` 生成 `classes.dex` 并打包进 APK；
`AndroidManifest.xml` 的 `android:hasCode` 为 `true`，`NativeActivity` 与原有合成诊断资源保持不变。
`provenance.py` 在 APK 含 `classes.dex` 时把它纳入 sidecar 条目（大小 + sha256），签名与 provenance 验证均通过。
主机 Java 测试只替换 `android.media` 边界（`tests/java-stubs`），验证请求属性、GRANTED/DELAYED/FAILED 区分、
回调代次、放弃指向实际请求、幂等性、迟到回调拒绝；同一源码也针对真实 `android.jar` 编译。

`app/build.sh` compiles the Java against the local android.jar, dexes it with the SDK's d8, packages `classes.dex`,
sets `hasCode=true` and keeps the NativeActivity and owned synthetic assets. Provenance covers the dex when present.

## 必需的 Java CI | Required Java CI

`.github/workflows/ci.yml` 的 `java-boundary` job（ubuntu-24.04）是必跑路径：先要求 runner 的 JDK 与
`platforms/android-35/android.jar`（缺失直接失败，不 skip；runner 已带 SDK 时按需用 sdkmanager 安装 API 35），
然后执行 `ci/scripts/java-boundary.sh`：
The `java-boundary` job is mandatory: it requires the runner JDK and the real API 35 android.jar (missing inputs fail
instead of skipping), then runs `ci/scripts/java-boundary.sh`, which

- 用真实 `android.jar` 编译 `dm/android/app/java` 下**当前全部**源码（`find` 遍历，后续新增源自动纳入）；
- 运行已验收的 `AudioFocusBoundaryTest` 主机控制（只有 `android.media` / `android.os` 窄 stub）；
- 每次运行私有负控制：缺失 android.jar、缺失 javac、故意 malformed 源都必须失败并正确归因；
- 发布清洗过路径的 `java-boundary.log` 到 `ci-java-boundary` artifact。

本地可显式运行 `ANDROID_SDK_ROOT=<sdk> bash ci/scripts/java-boundary.sh`。
`dm/android/tests/platform_audio_focus_java.sh` 在本地仍是可选流程（缺 jar 时明确打印 skip），
设置 `DM_JAVA_REQUIRE_ANDROID=1` 后缺 jar 会失败；CMake 中的 `platform_audio_focus_java` 测试仍只在
`Java_FOUND` 时注册，因此不能替代这条必需 CI。
Locally run the script with an explicit SDK; the older helper script remains optional locally but fails on a missing
jar when `DM_JAVA_REQUIRE_ANDROID=1`. The conditional CMake test is not a substitute for this required path.

该路径只证明真实 Android API 编译与主机控制通过，不安装/运行 APK，也不证明设备焦点或播放行为。
This path proves real-Android-API compilation and host controls only; it does not install or run the APK and does not
prove device focus or playback behaviour.

## 限制 | Limits

本切片只是打包好的平台边界：不证明设备焦点请求、音频播放、延迟、AV 同步或 App 音频 UI。父任务在原生 JNI /
控制线程接线与真实硬件证据之前保持打开。APK 未安装、未运行，未触碰手机/adb/音频硬件。
This slice is only the packaged platform boundary. It does not prove device focus, playback, latency or AV sync. The
APK is not installed or run; no phone, adb or audio hardware was touched.
