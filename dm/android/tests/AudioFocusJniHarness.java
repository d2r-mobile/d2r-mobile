// #184 主机 JVM 测试：通过真实 C 适配器 JNI 入口驱动真实 AudioFocusBoundary 与桥接类，只替换 Android 边界。
// 覆盖 lookup 失败回滚、真实 request 结果、callback 代次/LOSS 顺序/overflow、release 与迟到回调安全、多实例隔离。
// #184 host JVM test: drives the actual C adapter JNI entry points and the real AudioFocusBoundary/bridge class,
// replacing only the Android boundary. Covers lookup-failure rollback, real request results, callback
// generation/LOSS ordering/overflow, release and late-callback safety, and instance isolation.
import android.content.Context;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;

public final class AudioFocusJniHarness {
    static {
        System.loadLibrary("dm_audio_focus_jni_host");
    }

    private static native long nativeCreate(Context context);
    private static native int nativeRequest(long handle, int generation, boolean acceptsDelayed);
    private static native int nativeAbandon(long handle);
    private static native int nativeDrain(long handle, int[] events);
    private static native void nativeDeliver(long handle, int generation, int event);
    private static native int nativeRelease(long handle);

    private static int checks;
    private static int failures;

    private static void check(boolean condition, String what) {
        checks++;
        if (!condition) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    private static void deliver() {
        Looper.getMainLooper().drain();
    }

    private static int drain(long handle, int[] events) {
        return nativeDrain(handle, events);
    }

    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();

        AudioManager audio = new AudioManager();
        Context context = new Context(audio, AudioFocusJniHarness.class.getClassLoader());
        long owner = nativeCreate(context);
        check(owner > 0, "create returns a live handle");
        check(nativeCreate(null) == 0, "null context is rejected without crashing");

        // 真实平台结果区分 | distinct real platform results
        check(nativeRequest(owner, 7, false) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "granted");
        AudioFocusRequest granted = audio.lastRequest;
        check(granted != null, "platform request object retained");
        check(!granted.getAcceptsDelayedFocusGain(), "default request does not opt into delayed focus");

        // callbacks：main looper 队列 -> Java bridge -> C ring，generation 与顺序保持
        audio.emit(granted, AudioManager.AUDIOFOCUS_GAIN);
        audio.emit(granted, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        int[] events = new int[16];
        int count = drain(owner, events);
        check(count == 2, "two queued events delivered");
        check(events[0] == 7 && events[1] == 0, "GAIN with generation 7");
        check(events[2] == 7 && events[3] == 1, "LOSS follows GAIN with generation 7");

        // delayed / failed 区分；未 opt-in 时 stub 拒绝 delayed
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        check(nativeRequest(owner, 8, false) == AudioManager.AUDIOFOCUS_REQUEST_FAILED,
                "delayed refused without opt-in");
        check(nativeRequest(owner, 9, true) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED, "delayed with opt-in");
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        check(nativeRequest(owner, 10, false) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "failed result");

        // off-main 控制调用被 Java 边界拒绝，C 返回 E_THREAD 且不触碰平台
        int requestsBefore = audio.requestCalls;
        final int[] offMain = new int[1];
        Thread thread = new Thread(() -> offMain[0] = nativeRequest(owner, 11, false));
        thread.start();
        thread.join();
        check(offMain[0] == -3, "off-main request reports E_THREAD");
        check(audio.requestCalls == requestsBefore, "off-main request does not call the platform");

        // abandon 命中实际拥有的请求，幂等
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeRequest(owner, 12, false) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "regrant");
        AudioFocusRequest current = audio.lastRequest;
        int abandonsBefore = audio.abandonCalls;
        check(nativeAbandon(owner) == 1, "abandon reports a real abandonment");
        check(audio.abandonCalls == abandonsBefore + 1 && audio.abandonedRequest == current,
                "abandon uses the exact owned request");
        check(nativeAbandon(owner) == 0, "second abandon is a no-op");

        // 多实例互不污染 | instances do not contaminate each other
        long second = nativeCreate(context);
        check(second > 0 && second != owner, "second instance has a distinct handle");
        check(nativeRequest(second, 21, false) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "second instance request");
        AudioFocusRequest secondRequest = audio.lastRequest;
        audio.emit(secondRequest, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        int[] secondEvents = new int[4];
        check(drain(second, secondEvents) == 1 && secondEvents[0] == 21 && secondEvents[1] == 1,
                "second instance drains its own event");
        check(drain(owner, events) == 0, "first instance has no foreign event");

        // overflow fail closed：既有 LOSS 也必须显式报错，不能静默丢
        long overflowOwner = nativeCreate(context);
        check(nativeRequest(overflowOwner, 31, false) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "overflow request");
        AudioFocusRequest overflowRequest = audio.lastRequest;
        for (int i = 0; i < 64; i++) audio.emit(overflowRequest, AudioManager.AUDIOFOCUS_GAIN);
        audio.emit(overflowRequest, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain(overflowOwner, new int[260]) == -4, "overflow fails closed instead of dropping LOSS");
        check(nativeRelease(overflowOwner) == 0, "overflow owner releases cleanly");

        // lookup 失败回滚：坏 classloader 不泄漏 handle，随后正常创建仍可用
        Context badContext = new Context(audio, null);
        for (int i = 0; i < 3; i++) check(nativeCreate(badContext) == 0, "lookup failure rejected");
        long afterFailure = nativeCreate(context);
        check(afterFailure > 0, "create still works after lookup failures");
        check(nativeRelease(afterFailure) == 0, "cleanup after lookup failures");

        // release：释放后迟到回调不得进入原生层，旧 handle 不得解引用
        AudioFocusRequest releasedRequest = secondRequest;
        check(nativeRelease(second) == 0, "release succeeds");
        audio.emit(releasedRequest, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain(second, events) == -1, "drain on released handle is invalid, not UAF");
        nativeDeliver(second, 99, 1);   // 模拟 release 后仍到达的平台回调：只查表，不解引用
        check(nativeDrain(second, events) == -1, "late platform delivery cannot reach a freed owner");
        check(nativeRelease(second) == -1, "double release is rejected");
        check(nativeRequest(second, 22, false) == -1, "request on released handle is rejected");

        check(nativeRelease(owner) == 0, "first owner still releases");

        // off-main release 被拒绝后主线程重试必须真正 abandon 保留的请求，旧回调保持惰性。
        long retryOwner = nativeCreate(context);
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeRequest(retryOwner, 55, false) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED,
                "retry owner granted");
        AudioFocusRequest retryRequest = audio.lastRequest;
        int beforeRetry = audio.abandonCalls;
        final int[] offMainRelease = new int[1];
        Thread releaseThread = new Thread(() -> offMainRelease[0] = nativeRelease(retryOwner));
        releaseThread.start();
        releaseThread.join();
        check(offMainRelease[0] == -3, "off-main release rejected");
        check(audio.abandonCalls == beforeRetry, "off-main release leaves platform unchanged");
        check(nativeRelease(retryOwner) == 0, "main release retry succeeds");
        check(audio.abandonCalls == beforeRetry + 1, "main retry abandons retained request");
        check(audio.abandonedRequest == retryRequest, "retry abandons the exact owned request");
        audio.emit(retryRequest, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain(retryOwner, events) == -1, "late callback after retried release is inert");

        // 抛异常的 classloader：create 必须返回 0 且不留下待处理异常，之后 JNI 仍可正常使用。
        Context throwing = new Context(audio, AudioFocusJniHarness.class.getClassLoader()) {
            @Override
            public ClassLoader getClassLoader() {
                throw new IllegalStateException("lookup failure");
            }
        };
        boolean leaked = false;
        long thrownHandle = -1;
        try {
            thrownHandle = nativeCreate(throwing);
        } catch (RuntimeException failure) {
            leaked = true;
        }
        check(!leaked && thrownHandle == 0, "throwing classloader rejected without a leaked exception");

        // 构造器失败（无 AudioManager）：同样回滚且不留下待处理异常。
        Context noAudio = new Context(audio, AudioFocusJniHarness.class.getClassLoader()) {
            @Override
            public Object getSystemService(String name) {
                return null;
            }
        };
        leaked = false;
        long noAudioHandle = -1;
        try {
            noAudioHandle = nativeCreate(noAudio);
        } catch (RuntimeException failure) {
            leaked = true;
        }
        check(!leaked && noAudioHandle == 0, "constructor failure rejected without a leaked exception");
        long afterFailures = nativeCreate(context);
        check(afterFailures > 0, "JNI remains usable after failed creations");
        check(nativeRelease(afterFailures) == 0, "cleanup after failed creations");

        // 平台 abandon 首次抛异常：owner 保留，重试必须真正放弃同一请求，迟到回调惰性。
        AudioManager transientAudio = new AudioManager() {
            private boolean failed;

            @Override
            public int abandonAudioFocusRequest(AudioFocusRequest request) {
                if (!failed) {
                    failed = true;
                    throw new RuntimeException("transient platform abandon failure");
                }
                return super.abandonAudioFocusRequest(request);
            }
        };
        Context transientContext = new Context(transientAudio,
                AudioFocusJniHarness.class.getClassLoader());
        long transientOwner = nativeCreate(transientContext);
        check(transientOwner > 0, "transient owner created");
        transientAudio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeRequest(transientOwner, 81, false) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED,
                "transient owner granted");
        AudioFocusRequest transientRequest = transientAudio.lastRequest;
        check(nativeRelease(transientOwner) == -2, "platform abandon failure reported, owner retained");
        check(transientAudio.abandonCalls == 0, "failed platform abandon did not fabricate success");
        check(nativeRelease(transientOwner) == 0, "release retry succeeds");
        check(transientAudio.abandonCalls == 1 && transientAudio.abandonedRequest == transientRequest,
                "retry really abandons the same platform request");
        transientAudio.emit(transientRequest, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain(transientOwner, events) == -1, "late callback after platform-retry release is inert");

        System.out.println("AudioFocusJniHarness: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }
}
