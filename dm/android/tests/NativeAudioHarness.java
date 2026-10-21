// #200 NativeActivity 音频钩子集成测试：真实 native_audio → audio_session → dm_afj/audio_output/policy。
// 覆盖构造/恢复零自动播放、键盘 P/S 门控（源/Repeat/app 状态）、序列化 drain+自有 PCM、暂停/焦点丢失、
// DELAYED/FAILED 无输出、关闭失败保留重试。窄 AAudio fixture 仅替换 API 表。
// #200 integration test over the actual native_audio hooks and accepted session stack.
import android.content.Context;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;

public final class NativeAudioHarness {
    static {
        System.loadLibrary("dm_native_audio_host");
    }

    private static final int OUT_PLAYING = 1;
    private static final int E_JNI = -2;
    private static final int KEY_P = 44, KEY_S = 47, DOWN = 0;

    private static native void nativeFixtureReset();
    private static native void nativeFixtureCloseFailTwice();
    private static native int nativeFixtureOpens();
    private static native int nativeFixtureStarts();
    private static native int nativeFixtureCloses();
    private static native int nativeCreate(Context context, int queueFrames);
    private static native int nativeKey(int key, int action, int repeat, int keyboard);
    private static native int nativeResume();
    private static native int nativePause();
    private static native void nativeSetFocus(int focused);
    private static native int nativeTick();
    private static native int nativeState();
    private static native int nativeHasSession();
    private static native int nativeLastError();
    private static native int nativeDestroy();
    private static native int nativeDestroyNoEnv();
    private static native void nativeSetClock(long ns);
    private static native void nativeAdvanceClock(long ns);
    private static native int nativeConsume(int frames);
    private static native long nativeWritten();
    private static native int nativePending();
    private static native long nativeConsumed();
    private static native int nativeOrderErrors();
    private static native int nativeReference(long phase, short[] out, int frames);

    private static int checks, failures;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
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
        Context context = new Context(audio, NativeAudioHarness.class.getClassLoader());
        nativeFixtureReset();

        check(nativeCreate(context, 8192) == 1, "native audio helper created");
        nativeSetClock(0);
        check(nativeHasSession() == 0, "constructor creates no audio session");
        check(nativeState() != OUT_PLAYING, "constructor is idle");
        check(audio.requestCalls == 0, "constructor requests no focus");
        check(nativeFixtureOpens() == 0 && nativeFixtureStarts() == 0, "constructor opens no stream");

        // resume 不自动播放；未 active 的 P 也不请求。
        nativeSetFocus(1);
        check(nativeKey(KEY_P, DOWN, 0, 0) == 0, "non-keyboard P is not claimed");
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1 && nativeHasSession() == 0,
                "P without active App does not create a session");
        check(audio.requestCalls == 0, "inactive P requests no focus");
        check(nativeResume() == 0, "resume accepted");
        check(nativeHasSession() == 0 && nativeTick() == 0, "resume alone never plays");
        check(audio.requestCalls == 0, "resume requests no focus");

        // 显式 active + P：真实 GRANTED，timer 写自有 PCM。
        check(nativePause() == 0 && nativeResume() == 0, "active cycle");
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1, "keyboard P claimed");
        check(nativeHasSession() == 1, "first explicit P lazily creates the session");
        check(nativeState() == OUT_PLAYING, "GRANTED + active P plays");
        check(nativeFixtureStarts() == 1 && nativeFixtureOpens() == 1, "one real stream started");
        short[] pcm = new short[16];
        check(nativeReference(1, pcm, 8) == 8, "owned reference PCM available");
        check(pcm[0] == (short)((1 * 37) & 0x1fff) - 4096 || pcm[0] != 0, "owned PCM is non-trivial");
        check(nativeTick() == 0, "first tick establishes the pacing baseline");
        nativeAdvanceClock(8_000_000);
        check(nativeTick() == 384, "timer writes one 8ms/48kHz block while PLAYING");
        nativeAdvanceClock(8_000_000);
        check(nativeTick() == 384, "timer keeps producing 8ms blocks");
        check(nativeKey(KEY_S, 1, 0, 1) == 0, "S key up is ignored");
        check(nativeKey(KEY_S, DOWN, 1, 1) == 0, "repeat S is ignored");
        check(nativeKey(KEY_S, DOWN, 0, 1) == 1, "keyboard S claimed");
        check(nativeState() != OUT_PLAYING && nativeTick() == 0, "S stops producing");
        int abandons = audio.abandonCalls;
        check(abandons == 1, "S abandoned the platform request");

        // pause → 迟到 GAIN 不自动恢复；resume 后仍需显式 P。
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1 && nativeState() == OUT_PLAYING, "P restarts after stop");
        check(nativePause() == 0, "pause accepted");
        check(nativeTick() == 0, "producer stops before pause completes");
        AudioFocusRequest req = audio.lastRequest;
        audio.emit(req, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeTick() == 0 && nativeState() != OUT_PLAYING, "late GAIN after pause stays parked");
        check(nativeResume() == 0 && nativeTick() == 0, "resume without new P does not auto-play");

        // 窗口/焦点丢失：暂停且不自动恢复。
        nativeSetFocus(0);
        check(nativeTick() == 0, "focus loss stops producing");
        nativeSetFocus(1);
        check(nativeTick() == 0 && nativeState() != OUT_PLAYING, "focus regain alone does not auto-play");

        // DELAYED/FAILED 不产生输出，真实 GAIN 后才能写。
        nativeSetFocus(1);
        check(nativeResume() == 0, "resume after focus regain");
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        int startsBefore = nativeFixtureStarts();
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1, "P with DELAYED claimed");
        check(nativeState() != OUT_PLAYING && nativeTick() == 0, "DELAYED never plays");
        check(nativeFixtureStarts() == startsBefore, "DELAYED does not transiently start");
        audio.emit(audio.lastRequest, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        nativeTick();                       // 新的时间基线
        nativeAdvanceClock(8_000_000);
        int gainTick = nativeTick();
        check(gainTick > 0 && nativeState() == OUT_PLAYING,
                "real GAIN after DELAYED allows production (tick=" + gainTick + " state=" + nativeState() + ")");

        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1, "P with FAILED claimed");
        check(nativeState() != OUT_PLAYING && nativeTick() == 0, "FAILED never plays");

        // 关闭失败：保留会话/handle，重试成功后释放。
        nativeFixtureCloseFailTwice();
        int firstDestroy = nativeDestroy();
        check(firstDestroy != 0, "close failure retains the helper (rc=" + firstDestroy + ")");
        check(nativeHasSession() == 1, "session retained after failed close");
        int retryDestroy = nativeDestroy();
        check(retryDestroy == 0, "cleanup retry succeeds (rc=" + retryDestroy + ")");
        check(nativeHasSession() == 0, "session gone after proven cleanup");

        // 重新创建可再次工作（无跨实例污染）。
        nativeFixtureReset();
        check(nativeCreate(context, 2048) == 1, "recreate helper");
        nativeSetFocus(1);
        nativeResume();
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1 && nativeState() == OUT_PLAYING, "recreated helper plays");
        check(nativeDestroy() == 0, "final cleanup");
        check(nativeFixtureCloses() == nativeFixtureOpens(), "every opened fixture stream was closed");

        // 没有 JNIEnv 时不得丢弃 global Activity ref 还报成功；保留 helper 供正确清理。
        check(nativeCreate(context, 64) == 1, "helper for no-env cleanup boundary");
        check(nativeDestroyNoEnv() != 0, "destroy without JNIEnv refuses to report success");
        check(nativeHasSession() == 0, "no session was created");
        check(nativeDestroy() == 0, "normal cleanup after refused no-env destroy");

        // 48kHz 持续供给：受控时钟 + 真实注册 data callback 消费数秒；旧 8frames/tick 供给必须不达标。
        nativeFixtureReset();
        check(nativeCreate(context, 2048) == 1, "pacing helper created");
        nativeSetClock(0);
        nativeSetFocus(1);
        nativeResume();
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1 && nativeState() == OUT_PLAYING, "pacing session plays");
        nativeTick();   // 建立时间基线
        // 精确分数时钟：1000×8,333,333ns = 8.333333s 应为 floor(399,999.984)=399999 帧；
        // 独立 48kHz consumer 每 tick 取 399 帧（不依赖生产者返回值），silence 会以顺序错误暴露。
        int fractionalUnderruns = 0;
        for (int i = 0; i < 1000; i++) {
            nativeAdvanceClock(8_333_333);
            nativeTick();
            if (nativeConsume(399) != 399) fractionalUnderruns++;
        }
        long fractionalWritten = nativeWritten();
        check(fractionalWritten == 399_999,
                "fractional slices accumulate exactly to 399999 frames, got " + fractionalWritten);
        check(fractionalUnderruns == 0, "independent 48kHz consumer never underruns during fractional run");
        check(nativeConsumed() == 399_000, "consumer demand is independent of producer return values");
        check(nativeOrderErrors() == 0, "fractional run has no PCM gaps or duplicates");
        check(nativePending() < 512, "bounded pending backlog");

        // 有界 timer jitter run：独立 consumer 按 48kHz 需求 384 帧/tick 消费。
        check(nativePause() == 0 && nativeDestroy() == 0, "fractional cleanup");
        nativeFixtureReset();
        check(nativeCreate(context, 8192) == 1, "jitter helper created");
        nativeSetClock(0);
        nativeSetFocus(1);
        nativeResume();
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1 && nativeState() == OUT_PLAYING, "jitter session plays");
        nativeTick();
        nativeAdvanceClock(8_333_333);
        int prefill = nativeTick();
        check(prefill == 399, "explicit prefill before the consumer starts, got " + prefill);
        int[] jitter = {8_000_000, 8_000_000, 7_000_000, 9_000_000, 12_000_000, 4_000_000, 8_000_000, 8_000_000};
        int jitterUnderruns = 0;
        for (int i = 0; i < 1000; i++) {
            nativeAdvanceClock(jitter[i % jitter.length]);
            nativeTick();
            if (nativeConsume(384) != 384) jitterUnderruns++;
        }
        long paced = nativeWritten();
        check(paced >= 383_600 && paced <= 384_800,
                "8s jittered clock supplies ~48kHz, got " + paced);
        check(jitterUnderruns == 0, "independent 384/tick consumer never underruns under jitter");
        check(nativeConsumed() == 384_000, "jitter consumer demand matches 48kHz cadence");
        check(nativeOrderErrors() == 0, "jitter run has no PCM gaps or duplicates");

        // partial write + 队列满：相位按实际写入推进，未写样本保留并随后续 tick 发出。
        check(nativePause() == 0 && nativeDestroy() == 0, "jitter cleanup");
        nativeFixtureReset();
        check(nativeCreate(context, 32) == 1, "partial-write helper created");
        nativeSetClock(0);
        nativeSetFocus(1);
        nativeResume();
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1, "partial session plays");
        nativeTick();
        nativeAdvanceClock(100_000_000);            // 4800 帧需求，超过有界 4096 catchup
        int first = nativeTick();
        check(first == 32, "queue-full write partially accepts 32 frames, got " + first);
        check(nativePending() > 0, "unwritten samples retained");
        for (int guard = 0; guard < 200 && nativePending() > 0; guard++) {
            nativeConsume(32);
            nativeTick();
        }
        check(nativePending() == 0, "bounded backlog drains without skipping samples");
        check(nativeWritten() == 4096, "bounded catchup writes exactly the capped budget, got " + nativeWritten());
        check(nativeOrderErrors() == 0, "partial writes preserve PCM order");

        // 长暂停/late GAIN/stop：无陈旧供给；再显式 P 只从新时间基线供给，不突发旧音频。
        nativeConsume(32);                  // 清空 partial 会话残留队列
        check(nativeKey(KEY_S, DOWN, 0, 1) == 1, "stop claimed");
        nativeAdvanceClock(5_000_000_000L);
        check(nativeTick() == 0, "long stop supplies no stale frames");
        AudioFocusRequest stale = audio.lastRequest;
        audio.emit(stale, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(nativeTick() == 0 && nativeState() != OUT_PLAYING, "late GAIN after stop stays inert");
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        check(nativeKey(KEY_P, DOWN, 0, 1) == 1 && nativeState() == OUT_PLAYING, "explicit P after stop");
        nativeTick();
        nativeAdvanceClock(8_000_000);
        int resumedWrite = nativeTick();
        check(resumedWrite > 0 && resumedWrite <= 512,
                "resume supplies one tick, no stale burst, got " + resumedWrite);
        check(nativeDestroy() == 0, "pacing final cleanup");

        System.out.println("NativeAudioHarness: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }
}
