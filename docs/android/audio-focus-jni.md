# 焦点策略实例化 JNI 适配（#184）| Instance-owned JNI adapter (#184)

`app/audio_focus_jni.c/h` 把已验收的 `AudioFocusBoundary`（PR #171，main-looper 契约）包装成 C owner；Java 侧只新增
必要的 `AudioFocusJniBridge` 适配类。`native.c` 未修改（由 Hermes2 负责），本切片不是 App 调用点，也不声明已播放。
`app/audio_focus_jni.c/h` wraps the accepted AudioFocusBoundary (PR #171, main-looper contract) in a C owner; the only
Java addition is the minimal `AudioFocusJniBridge` adapter. `native.c` is untouched (owned by Hermes2); this slice is
not an App call site and does not claim playback.

## API 与线程契约 | API and thread contract

- `dm_afj_create(JNIEnv*, jobject activity_context, dm_afj_owner**)`：通过 **activity 的 ClassLoader**
  (`context.getClassLoader().loadClass(...)`) 加载打包的 bridge 类，RegisterNatives `nativeOnFocusEvent`，构造
  bridge 并持有全局引用；任何一步失败都回滚 owner 槽位/全局引用/本地引用。
- `dm_afj_request/abandon/release`：控制调用必须在 Android 主线程；Java 边界拒绝其它线程，C 捕获
  `IllegalStateException` 并返回 `DM_AFJ_E_THREAD`，不会触碰平台音频或策略。
- 返回平台真实结果 `GRANTED(1)/DELAYED(2)/FAILED(0)`；`DELAYED`、`GRANTED`、`FAILED` 明确区分，**不把 pending
  focus 当作播放许可**，也绝不从 JNI 回调启动 AAudio。
- `dm_afj_drain` 由调用方的串行控制步骤执行；回调只把 `{generation,event}` 放入本 owner 的有界环。

## 句柄安全 | Handle safety

Java 只看到单调递增的不透明 id，绝不持有 C 指针。回调按 id 在 16 槽 owner 表中查表并取 in-flight 引用；
`dm_afj_release` 先调用 Java `release()` 使监听器失效，再注销 id、等待 in-flight 回调归零、删除全局引用并释放。
id 永不复用，因此 release 后到达的回调/旧 id 只会查表失败并被安全丢弃，不会解引用已释放内存。
Java only sees a monotonically increasing opaque id, never a C pointer. Callbacks look the id up in a 16-slot owner
table and take an in-flight reference; release invalidates the Java listener first, then unregisters, waits for
in-flight callbacks, deletes the global reference and frees. Ids are never reused, so a late callback or stale id
simply fails lookup instead of dereferencing freed memory.

## Overflow | Overflow

环容量 64。溢出时置显式标志，`dm_afj_drain` 返回 `DM_AFJ_E_OVERFLOW` 且不返回事件；调用方必须 retire owner。
永久 LOSS 绝不会被静默丢弃。
The ring holds 64 events. On overflow an explicit flag makes `dm_afj_drain` return `DM_AFJ_E_OVERFLOW`; the caller must
retire the owner. A permanent LOSS is never silently dropped.

## 重试与异常清理 | Retry and exception cleanup

- `AudioFocusJniBridge.release()` 先让已验收的 bridge 完成线程检查与实际 `abandon`，成功后才提交本地 released
  状态；因此 off-main 调用返回 `DM_AFJ_E_THREAD` 时 owner 与平台焦点均保持原状，主线程重试仍会放弃同一个
  `AudioFocusRequest`，旧回调在重试释放后保持惰性。
- `AudioFocusJniBridge.release()` commits its released flag only after the accepted boundary has checked the thread
  and actually abandoned the platform request. An off-main call therefore returns `DM_AFJ_E_THREAD` with owner and
  focus intact, and a main-thread retry still abandons the exact request.
- 平台 `abandonAudioFocusRequest` 自身抛异常时，`AudioFocusBoundary` 立即失效 callback token，但保留
  `AudioFocusRequest` 身份；`release()` 仍保持终态（之后拒绝新 request），同一主线程的重试会再次放弃同一个
  保留请求。C 对非线程平台异常返回 `DM_AFJ_E_JNI` 并保留 owner，绝不把失败当成功释放。
- If the platform abandon itself throws, `AudioFocusBoundary` invalidates callback tokens immediately but retains the
  request identity; release stays terminal (later requests are refused) and a same-thread retry abandons that exact
  retained request. C reports a non-thread platform failure as `DM_AFJ_E_JNI` and keeps the owner instead of freeing
  it as if the abandon had succeeded.
- `dm_afj_create` 在每个失败的 JNI 操作后立即 clear 待处理异常并释放本地引用：抛异常的 `getClassLoader`、
  class lookup、构造器或 `NewGlobalRef` 失败都统一返回 `DM_AFJ_E_JNI`，不会把 Java 异常泄漏给调用方；
  `GetJavaVM` 失败时不会发布不可用 owner。方法查找逐个检查，避免在异常待处理时继续调用 JNI。
- `dm_afj_create` clears each failing JNI exception immediately and frees locals: throwing classloader, class lookup,
  constructor and NewGlobalRef failures all return `DM_AFJ_E_JNI` without leaking an exception; a failed GetJavaVM
  never publishes an unusable owner, and method lookups are checked one by one so no JNI call runs with a pending
  exception. The host harness runs with `-Xcheck:jni`.

## 证据 | Evidence

- Host JVM 测试通过真实 C JNI 入口驱动真实 bridge/boundary（只替换 Android 边界）：46 checks / 0 failures，
  同时 root 扩展 harness 41 checks 也通过；覆盖
  lookup 失败回滚、真实 request 结果、callback generation/LOSS 顺序、overflow fail closed、release 后迟到回调与旧
  id 安全、多实例隔离、off-main 拒绝、abandon 身份。
- `aarch64-linux-android29-clang -Wall -Wextra -Werror` 编译真实 C 适配器通过；桥接 Java 也针对真实
  `platforms/android-35/android.jar` 编译通过。
- 尚未有 native.c/App 调用点；真实设备播放、焦点评测、延迟或 AV 同步不在本切片证据内。
- No native.c/App call site yet; device playback, focus behaviour, latency and AV sync are not claimed here.
