// #170 焦点边界主机测试：只替换 android.media / android.os 边界，验证真实请求属性（含 delayed opt-in）、
// 主 looper 单线程约束（非主线程拒绝）、main Handler 队列投递顺序、release/替换后旧事件不转发。
// #170 host tests replacing only the android.media / android.os boundary: request attributes including the delayed
// opt-in, main-looper confinement with off-thread rejection, FIFO main-Handler delivery, and no stale forwarding
// after release or replacement.
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;

import org.d2rmobile.android.diagnostic.AudioFocusBoundary;

public final class AudioFocusBoundaryTest {
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

    private static final class Recorder implements AudioFocusBoundary.Listener {
        int calls;
        int generation = -1;
        int event = -1;
        int[] events = new int[8];
        AudioFocusBoundary boundary;
        boolean releaseOnFirst;

        @Override
        public void onAudioFocusEvent(int generation, int event) {
            this.calls++;
            this.generation = generation;
            this.event = event;
            if (calls <= events.length) events[calls - 1] = event;
            if (releaseOnFirst && calls == 1 && boundary != null) boundary.release();
        }
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();

        AudioManager audio = new AudioManager();
        Recorder recorder = new Recorder();
        AudioFocusBoundary boundary = new AudioFocusBoundary(audio, recorder);

        // 构造不自动请求；主 looper 是唯一控制线程 | no request on construction; the main looper is the control thread
        check(audio.requestCalls == 0, "construction must not request focus");
        check(!boundary.isActive(), "new boundary is inactive");

        // 1. 真实属性与 GRANTED；默认不 opt-in delayed | real attributes, no delayed opt-in by default
        check(boundary.request(7) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "granted result returned");
        check(audio.requestCalls == 1, "exactly one platform request");
        check(boundary.isActive(), "granted request is owned");
        check(boundary.generation() == 7, "generation retained");
        AudioFocusRequest granted = audio.lastRequest;
        check(granted != null, "request object retained");
        check(granted.getFocusGain() == AudioManager.AUDIOFOCUS_GAIN, "AUDIOFOCUS_GAIN requested");
        check(granted.getAudioAttributes().getUsage() == AudioAttributes.USAGE_GAME, "USAGE_GAME attribute");
        check(granted.getAudioAttributes().getContentType() == AudioAttributes.CONTENT_TYPE_MUSIC,
                "CONTENT_TYPE_MUSIC attribute");
        check(!granted.willPauseWhenDucked(), "willPauseWhenDucked=false");
        check(!granted.getAcceptsDelayedFocusGain(), "default request does not accept delayed focus");
        check(granted.getOnAudioFocusChangeListener() != null, "platform listener registered");
        check(granted.getOnAudioFocusChangeHandler() != null, "main Handler registered");

        // 2. 回调按 main 队列 FIFO 顺序投递；未知码不发明事件 | FIFO delivery; unknown codes produce nothing
        audio.emit(granted, AudioManager.AUDIOFOCUS_GAIN);
        audio.emit(granted, AudioManager.AUDIOFOCUS_LOSS);
        check(recorder.calls == 0, "callbacks are queued, not delivered inside the platform call");
        deliver();
        check(recorder.calls == 2, "both queued callbacks delivered");
        check(recorder.events[0] == AudioFocusBoundary.EVENT_GAIN
                && recorder.events[1] == AudioFocusBoundary.EVENT_LOSS, "queued order preserved");
        check(recorder.generation == 7, "generation carried on delivery");
        audio.emit(granted, 12345);
        deliver();
        check(recorder.calls == 2, "unknown platform code yields no invented event");

        // 3. 放弃指向实际所有者且幂等；队列中的旧回调被丢弃 | abandon identity/idempotency; queued stale dropped
        check(boundary.abandon(), "abandon reports a real abandonment");
        check(audio.abandonCalls == 1, "one platform abandon");
        check(audio.abandonedRequest == granted, "abandon uses the exact owned request");
        check(!boundary.isActive(), "abandon clears ownership");
        check(!boundary.abandon(), "second abandon is a no-op");
        check(audio.abandonCalls == 1, "second abandon does not call the platform");
        audio.emit(granted, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(recorder.calls == 2, "queued callback after abandon is dropped");
        check(boundary.droppedCallbackCount() == 1, "dropped callback counted");

        // 4. delayed 需要真实 opt-in | delayed requires the real opt-in
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        check(boundary.request(8) == AudioManager.AUDIOFOCUS_REQUEST_FAILED,
                "platform refuses delayed without opt-in");
        check(!boundary.isActive(), "failed delayed request owns nothing");
        check(boundary.request(9, true) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED,
                "delayed result returned with opt-in");
        check(audio.lastRequest.getAcceptsDelayedFocusGain(), "real request carries the delayed opt-in");
        check(boundary.isActive(), "delayed request is owned");
        AudioFocusRequest delayed = audio.lastRequest;
        audio.emit(delayed, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(recorder.generation == 9 && recorder.event == AudioFocusBoundary.EVENT_GAIN,
                "delayed grant forwarded with its generation");

        // 5. FAILED 不建立所有权 | failed establishes no ownership
        AudioManager failedAudio = new AudioManager();
        failedAudio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        Recorder failedRecorder = new Recorder();
        AudioFocusBoundary failed = new AudioFocusBoundary(failedAudio, failedRecorder);
        check(failed.request(3) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "failed result returned");
        check(!failed.isActive(), "failed request owns nothing");
        check(!failed.abandon(), "failed request has nothing to abandon");
        check(failedAudio.abandonCalls == 0, "no platform abandon after failure");

        // 6. 重新请求：旧请求被真实放弃，旧队列回调失效，新回调带新代次 | re-request replaces ownership
        AudioFocusRequest old = audio.lastRequest;
        audio.emit(old, AudioManager.AUDIOFOCUS_LOSS);        // 排队后立刻替换 | queued before replacement
        check(boundary.request(11, true) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED,
                "re-request returns platform result");
        check(audio.abandonedRequest == old, "re-request abandoned the previous exact request");
        AudioFocusRequest fresh = audio.lastRequest;
        check(fresh != old, "re-request built a new platform request");
        deliver();
        check(recorder.calls == 3, "queued old-request callback is dropped after replacement");
        audio.emit(fresh, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
        deliver();
        check(recorder.generation == 11 && recorder.event == AudioFocusBoundary.EVENT_LOSS_TRANSIENT,
                "new request callback forwarded with new generation");

        // 7. 非主 looper 控制调用被主动拒绝，且不触碰平台 | off-main control calls are rejected before the platform
        int requestsBefore = audio.requestCalls;
        int abandonsBefore = audio.abandonCalls;
        final boolean[] rejected = new boolean[3];
        Thread offMain = new Thread(() -> {
            rejected[0] = throwsMainLooper(() -> boundary.request(99));
            rejected[1] = throwsMainLooper(boundary::abandon);
            rejected[2] = throwsMainLooper(boundary::release);
        });
        offMain.start();
        try {
            offMain.join();
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(interrupted);
        }
        check(rejected[0] && rejected[1] && rejected[2], "request/abandon/release reject a non-main thread");
        check(audio.requestCalls == requestsBefore, "off-main request does not touch the platform");
        check(audio.abandonCalls == abandonsBefore, "off-main abandon does not touch the platform");
        check(boundary.isActive(), "off-main calls do not change ownership");

        // 8. listener 内 release：同一队列里更旧的事件绝不再转发 | release inside a listener blocks older queued events
        AudioManager releaseAudio = new AudioManager();
        Recorder releaseRecorder = new Recorder();
        AudioFocusBoundary releasing = new AudioFocusBoundary(releaseAudio, releaseRecorder);
        releaseRecorder.boundary = releasing;
        releaseRecorder.releaseOnFirst = true;
        check(releasing.request(31) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "release-race request granted");
        AudioFocusRequest releaseRequest = releaseAudio.lastRequest;
        releaseAudio.emit(releaseRequest, AudioManager.AUDIOFOCUS_LOSS);
        releaseAudio.emit(releaseRequest, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(releaseRecorder.calls == 1, "only the first queued event is forwarded");
        check(releaseRecorder.events[0] == AudioFocusBoundary.EVENT_LOSS, "the forwarded event is the first one");
        check(!releasing.isActive(), "release inside the listener abandoned ownership");
        check(releaseAudio.abandonCalls == 1, "release inside the listener abandons the exact request");
        check(releasing.droppedCallbackCount() == 1, "older queued event dropped, not silently forwarded");

        // 9. release 幂等并永久拒绝后续请求 | release is idempotent and refuses later requests
        boundary.release();
        boundary.release();
        check(!boundary.isActive(), "release abandons the active request");
        int afterRelease = audio.requestCalls;
        check(boundary.request(12) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "request after release fails");
        check(audio.requestCalls == afterRelease, "request after release does not touch the platform");
        audio.emit(fresh, AudioManager.AUDIOFOCUS_GAIN);
        deliver();
        check(recorder.calls == 4, "callback after release is dropped");

        System.out.println("AudioFocusBoundaryTest: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }

    private interface Action {
        void run();
    }

    private static boolean throwsMainLooper(Action action) {
        try {
            action.run();
            return false;
        } catch (IllegalStateException expected) {
            return true;
        }
    }
}
