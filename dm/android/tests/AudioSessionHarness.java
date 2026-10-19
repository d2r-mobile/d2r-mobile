// #195 主机 JVM 集成测试：真实 audio_session + dm_afj + dm_audio_output/policy + 窄 AAudio fixture。
// 覆盖 constructor idle、GRANTED/DELAYED/FAILED、late GAIN、permanent LOSS/rearm、stale focus、overflow、
// JNI/AAudio 失败重试与最终零 handle。
// #195 host JVM integration test: real audio_session + dm_afj + dm_audio_output/policy with a narrow AAudio fixture.
import android.content.Context;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;

public final class AudioSessionHarness {
    static {
        System.loadLibrary("dm_audio_session_host");
    }

    private static final int OUT_STOPPED = 0;
    private static final int OUT_PLAYING = 1;
    private static final int OUT_PAUSED_LIFECYCLE = 3;
    private static final int E_JNI = -2;
    private static final int E_OVERFLOW = -4;

    private static native void nativeFixtureReset();
    private static native void nativeFixtureCloseFailOnce();
    private static native int nativeFixtureOpens();
    private static native int nativeFixtureStarts();
    private static native int nativeFixtureCloses();
    private static native long nativeCreate(Context context, int queueFrames);
    private static native int nativePlay();
    private static native int nativeResume();
    private static native int nativePause();
    private static native int nativeStop();
    private static native int nativeDrain();
    private static native int nativeWrite(short[] samples, int frames);
    private static native int nativePump(short[] out, int frames);
    private static native int nativeDestroy();
    private static native int nativeState();
    private static native int nativeFocusGeneration();
    private static native int nativeLastError();
    private static native int nativeHasFocus();
    private static native int nativeHasAudio();
    private static native int nativeClosed();

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

    public static void main(String[] args) {
        Looper.prepareMainLooper();

        AudioManager audio = new AudioManager();
        Context context = new Context(audio, AudioSessionHarness.class.getClassLoader());
        nativeFixtureReset();
        check(nativeCreate(context, 32) == 1, "create succeeds");
        check(nativeState() == OUT_STOPPED, "constructor is idle");
        check(nativeFocusGeneration() == 0, "constructor has no focus generation");
        check(audio.requestCalls == 0, "constructor does not request focus");
        check(nativeFixtureOpens() == 0 && nativeFixtureStarts() == 0, "constructor opens/starts no stream");
        check(nativeHasFocus() == 1 && nativeHasAudio() == 0, "focus owner exists but no audio handle");

        // GRANTED + 用户意图，但 App 未 active：不得启动。| GRANTED with intent but App inactive must not start.
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativePlay() == 1, "play returns platform GRANTED");
        check(nativeState() == OUT_PAUSED_LIFECYCLE, "granted play waits for App resume");
        check(nativeFixtureStarts() == 0, "no stream starts before App resume");
        check(nativeResume() == 0, "resume accepted");
        check(nativeState() == OUT_PLAYING, "active App + intent + GRANTED starts playback");
        check(nativeFixtureStarts() == 1 && nativeFixtureOpens() == 1, "one real stream started");

        // 真实 PCM 路径 | real PCM path
        short[] pcm = {1000, -1000, 2000, -2000};
        check(nativeWrite(pcm, 2) == 2, "write accepts two frames");
        short[] out = new short[4];
        check(nativePump(out, 2) == 1, "AAudio data callback runs");
        check(out[0] == 1000 && out[1] == -1000 && out[2] == 2000 && out[3] == -2000,
                "actual PCM matches the fixture stream");

        // pause 后迟到的 GAIN 不得自动播放 | late GAIN after pause must not auto-play
        check(nativePause() == 0, "pause accepted");
        check(nativeState() == OUT_PAUSED_LIFECYCLE, "pause parks output");
        AudioFocusRequest current = audio.lastRequest;
        audio.emit(current, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeDrain() == 0, "drain after late GAIN accepted");
        check(nativeState() == OUT_PAUSED_LIFECYCLE, "late GAIN cannot bypass pause");
        check(nativeResume() == 0, "resume after pause");
        check(nativeState() == OUT_PLAYING, "resume restores playback");

        // user stop：真实 abandon 同一请求，迟到 GAIN 不自动播放
        int abandonsBefore = audio.abandonCalls;
        check(nativeStop() == 0, "stop accepted");
        check(nativeState() == OUT_STOPPED, "stop parks output");
        check(audio.abandonCalls == abandonsBefore + 1 && audio.abandonedRequest == current,
                "stop abandons the exact platform request");
        int generationAfterStop = nativeFocusGeneration();
        audio.emit(current, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeDrain() == 0, "drain after stop accepted");
        check(nativeState() == OUT_STOPPED, "late GAIN after stop does not auto-play");
        check(nativeFocusGeneration() == generationAfterStop, "focus generation stays after stop");
        check(nativeWrite(pcm, 2) == 0, "write is rejected after stop");

        // DELAYED 只是 pending，真实 GAIN 回调后才允许播放；并用真实 AAudio start 计数证明没有瞬态启动。
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        int pendingStartsBefore = nativeFixtureStarts();
        check(nativePlay() == 2, "play returns platform DELAYED");
        check(nativeFixtureStarts() == pendingStartsBefore,
                "DELAYED must never transiently start AAudio before real GAIN");
        check(nativeState() != OUT_PLAYING, "delayed focus is not playback permission");
        AudioFocusRequest delayed = audio.lastRequest;
        audio.emit(delayed, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeDrain() == 0, "drain delayed grant");
        check(nativeState() == OUT_PLAYING, "real GAIN after DELAYED starts playback");
        check(nativeFixtureStarts() == pendingStartsBefore + 1, "exactly one real start after the granted GAIN");

        // FAILED 不得启动 | FAILED is never permission
        int failedStartsBefore = nativeFixtureStarts();
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        check(nativePlay() == 0, "play returns platform FAILED");
        check(nativeState() != OUT_PLAYING, "FAILED never starts playback");
        check(nativeFixtureStarts() == failedStartsBefore, "FAILED does not transiently start AAudio");

        // permanent LOSS 撤销播放并要求新的用户意图 | permanent LOSS requires new user intent
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativePlay() == 1, "regrant after failure");
        check(nativeState() == OUT_PLAYING, "regrant plays");
        AudioFocusRequest lossRequest = audio.lastRequest;
        audio.emit(lossRequest, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain() == 0, "drain permanent loss");
        check(nativeState() == OUT_STOPPED, "permanent LOSS stops output");
        audio.emit(lossRequest, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeDrain() == 0, "drain late gain after permanent loss");
        check(nativeState() == OUT_STOPPED, "late GAIN cannot bypass rearm");
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativePlay() == 1, "explicit new play rearms");
        check(nativeState() == OUT_PLAYING, "rearmed play starts");

        // 替换请求后旧请求的迟到回调必须失效，当前请求的 LOSS 仍有效
        AudioFocusRequest stale = audio.lastRequest;
        check(nativePlay() == 1, "replacement request granted");
        AudioFocusRequest fresh = audio.lastRequest;
        check(fresh != stale, "new request identity");
        audio.emit(stale, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain() == 0, "drain stale focus event");
        check(nativeState() == OUT_PLAYING, "stale request LOSS cannot stop current playback");
        audio.emit(fresh, AudioManager.AUDIOFOCUS_LOSS);
        deliver();
        check(nativeDrain() == 0, "drain current LOSS");
        check(nativeState() == OUT_STOPPED, "current request LOSS stops output");

        // ring overflow fail closed：停到安全状态并显式报错
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativePlay() == 1, "overflow test play");
        AudioFocusRequest overflowRequest = audio.lastRequest;
        for (int i = 0; i < 65; i++) audio.emit(overflowRequest, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeDrain() == E_OVERFLOW, "overflow fails closed");
        check(nativeState() == OUT_STOPPED, "overflow stops output");
        check(nativeLastError() == E_OVERFLOW, "overflow error surfaced");
        check(nativeWrite(pcm, 2) == 0, "no producer after overflow stop");

        // 干净销毁 | clean destroy
        check(nativeDestroy() == 0, "destroy succeeds");
        check(nativeClosed() == 1 && nativeHasFocus() == 0 && nativeHasAudio() == 0,
                "destroy leaves zero owned handles");

        // JNI abandon 首次失败：handle 保留，重试释放同一请求
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
        Context transientContext = new Context(transientAudio, AudioSessionHarness.class.getClassLoader());
        nativeFixtureReset();
        check(nativeCreate(transientContext, 16) == 1, "transient session created");
        transientAudio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativePlay() == 1, "transient play granted");
        AudioFocusRequest transientRequest = transientAudio.lastRequest;
        check(nativeStop() == E_JNI, "stop reports platform abandon failure");
        check(nativeState() == OUT_STOPPED, "failed abandon still stopped output");
        check(nativeHasFocus() == 1, "focus owner retained for retry");
        check(nativeDestroy() == 0, "destroy retries release");
        check(transientAudio.abandonCalls == 1 && transientAudio.abandonedRequest == transientRequest,
                "retry abandons the same platform request before freeing");

        // AAudio close 失败：handle 保留，重试销毁
        nativeFixtureReset();
        check(nativeCreate(context, 16) == 1, "close-failure session created");
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativePlay() == 1 && nativeResume() == 0, "close-failure session plays");
        check(nativeState() == OUT_PLAYING, "close-failure session playing");
        nativeFixtureCloseFailOnce();
        check(nativeDestroy() != 0, "destroy reports retained AAudio handle");
        check(nativeHasAudio() == 1, "audio handle retained for retry");
        check(nativeDestroy() == 0, "destroy retry succeeds");
        check(nativeHasAudio() == 0 && nativeHasFocus() == 0, "final zero handles");
        check(nativeWrite(pcm, 2) == 0, "no producer after final destroy");
        check(nativeCreate(context, 16) == 1, "recovers after failed cleanup");
        check(nativeDestroy() == 0, "final cleanup");

        // 替换请求在旧请求 abandon 后抛异常：必须停到安全状态，旧 GAIN 不得恢复播放。
        AudioManager requestThrows = new AudioManager() {
            @Override
            public int requestAudioFocus(AudioFocusRequest request) {
                if (requestCalls >= 1) throw new RuntimeException("platform request failed after old abandon");
                return super.requestAudioFocus(request);
            }
        };
        nativeFixtureReset();
        check(nativeCreate(new Context(requestThrows, AudioSessionHarness.class.getClassLoader()), 16) == 1,
                "throwing-request session created");
        check(nativePlay() == 1 && nativeResume() == 0, "throwing-request session initially plays");
        check(nativeState() == OUT_PLAYING, "throwing-request session playing");
        AudioFocusRequest replaced = requestThrows.lastRequest;
        check(nativePlay() == E_JNI, "replacement request failure reported honestly");
        check(requestThrows.abandonCalls == 1, "old request was already abandoned before the failure");
        check(nativeState() != OUT_PLAYING, "request exception parks unfocused output");
        check(nativeWrite(pcm, 2) == 0, "no producer after request failure");
        requestThrows.emit(replaced, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeDrain() == 0, "drain after failed replacement");
        check(nativeState() != OUT_PLAYING, "stale GAIN cannot resume after failed replacement");
        check(nativeDestroy() == 0, "cleanup after failed replacement");

        System.out.println("AudioSessionHarness: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }
}
