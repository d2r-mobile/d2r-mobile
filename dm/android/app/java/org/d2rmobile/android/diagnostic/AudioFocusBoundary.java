// #170 真实 Android AudioManager 焦点边界：只请求/放弃平台焦点，并按调用方 generation 转发平台回调。
// 不创建或操作 AAudio、不修改原生策略；构造时不请求焦点。
// #170 real AudioManager focus boundary: requests/abandons platform focus only and forwards callbacks tagged with
// the caller's generation. It never creates or touches AAudio, never mutates native policy, and never requests focus
// on construction.
//
// Single-thread contract (enforced): every control call (request/abandon/release) must run on the Android main
// looper, and the platform listener is registered with a main-looper Handler, so Android queues callbacks on that
// same looper after the request returns. Calls from any other thread throw IllegalStateException before touching
// platform state. Because control and callbacks share one thread, no callback can run concurrently with
// requestAudioFocus and no cross-thread staging is needed; every queued callback re-checks its request token and the
// released flag at delivery time, so a listener that releases or replaces the boundary inside one callback cannot
// let older queued events through.
//
// A later JNI layer must marshal to the main looper (for example by posting to a Handler created there) before
// calling request/abandon/release, and must enqueue listener events back onto its serialized control thread before
// touching AAudio or dm_af_policy.
package org.d2rmobile.android.diagnostic;

import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;

public final class AudioFocusBoundary {
    /** 与 dm_af_event 的焦点子集一一对应；JNI 侧直接映射。| Mirrors the focus subset of dm_af_event for JNI. */
    public static final int EVENT_GAIN = 0;
    public static final int EVENT_LOSS = 1;
    public static final int EVENT_LOSS_TRANSIENT = 2;
    public static final int EVENT_CAN_DUCK = 3;

    /** 平台调用结果原样返回 AudioManager.AUDIOFOCUS_REQUEST_GRANTED/DELAYED/FAILED。 */
    public interface Listener {
        /** 主 looper 回调；不得在此同步调用 AAudio 或原生策略。| Main-looper callback; never call AAudio here. */
        void onAudioFocusEvent(int generation, int event);
    }

    private final AudioManager audioManager;
    private final Listener listener;
    private final Looper mainLooper;
    private final Handler mainHandler;

    // 单线程（主 looper）状态，无并发访问。| Single-threaded (main looper) state, no concurrent access.
    private AudioFocusRequest activeRequest;
    private int activeGeneration;
    private long nextToken = 1;
    private long activeToken;
    private boolean released;
    private int droppedCallbacks;

    public AudioFocusBoundary(AudioManager audioManager, Listener listener) {
        if (audioManager == null) throw new IllegalArgumentException("audioManager == null");
        if (listener == null) throw new IllegalArgumentException("listener == null");
        this.audioManager = audioManager;
        this.listener = listener;
        this.mainLooper = Looper.getMainLooper();
        if (mainLooper == null) throw new IllegalStateException("no main looper");
        this.mainHandler = new Handler(mainLooper);
    }

    /** 不启用 delayed focus 的请求。| Request without delayed-focus opt-in. */
    public int request(int generation) {
        return request(generation, false);
    }

    /**
     * 请求 AUDIOFOCUS_GAIN（游戏用途）。只有 acceptsDelayedFocusGain 为 true 才向真实 builder 设置
     * setAcceptsDelayedFocusGain(true)。返回平台真实结果，只有 GRANTED/DELAYED 建立所有权。非主 looper 调用
     * 抛 IllegalStateException；已释放返回 FAILED 且不调用平台。
     * Requests AUDIOFOCUS_GAIN for game usage; delayed focus is opted into on the real request only when asked.
     * Returns the real platform result and only GRANTED/DELAYED establish ownership. Off-main calls throw
     * IllegalStateException; a released boundary returns FAILED without touching the platform.
     */
    public int request(int generation, boolean acceptsDelayedFocusGain) {
        requireMainLooper();
        if (released) return AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        abandon();

        final long token = nextToken++;
        final AudioManager.OnAudioFocusChangeListener platformListener =
                new AudioManager.OnAudioFocusChangeListener() {
                    @Override
                    public void onAudioFocusChange(int focusChange) {
                        dispatch(token, focusChange);
                    }
                };
        final AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
        final AudioFocusRequest request = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attributes)
                .setWillPauseWhenDucked(false)
                .setAcceptsDelayedFocusGain(acceptsDelayedFocusGain)
                .setOnAudioFocusChangeListener(platformListener, mainHandler)
                .build();

        final int result = audioManager.requestAudioFocus(request);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                || result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED) {
            activeRequest = request;
            activeToken = token;
            activeGeneration = generation;
        } else {
            activeRequest = null;
            activeToken = 0;
            activeGeneration = 0;
        }
        return result;
    }

    /**
     * 放弃当前请求（幂等）。只放弃自己实际持有的那个 AudioFocusRequest 实例；无活跃请求时不调用平台。
     * 平台调用抛出异常时句柄仍保留以便重试，但 callback token 立即失效；返回是否真的放弃了平台请求。
     * Abandons exactly the retained request. If the platform call throws, the request stays retained for a retry
     * while callback tokens are invalidated immediately; returns whether a platform abandon really happened.
     */
    public boolean abandon() {
        requireMainLooper();
        final AudioFocusRequest request = activeRequest;
        if (request == null) return false;
        activeToken = 0;        // 先失效 token：旧 callback 立即惰性，无论平台调用是否成功
        audioManager.abandonAudioFocusRequest(request);
        activeRequest = null;   // 只有平台调用成功后才丢失可重试的身份
        activeGeneration = 0;
        return true;
    }

    /** 释放边界（幂等）：放弃活跃请求并永久拒绝后续 request；失败时保留请求供同一主线程重试。 */
    public void release() {
        requireMainLooper();
        if (released && activeRequest == null) return;   // 已完全释放：重复 release 无操作
        released = true;                                 // 逻辑上终止；request() 从此拒绝
        abandon();                                       // 平台失败时请求保留，下一次 release 重试
    }

    public boolean isActive() { return activeRequest != null; }

    public int generation() { return activeGeneration; }

    /** 因释放/替换/失败请求或非主线程投递而被丢弃的回调数。| Callbacks dropped for released/replaced/failed/off-main delivery. */
    public int droppedCallbackCount() { return droppedCallbacks; }

    /** 兼容旧调用方的别名。| Alias kept for older callers. */
    public int lateCallbackCount() { return droppedCallbacks; }

    private void requireMainLooper() {
        if (Looper.myLooper() != mainLooper) {
            throw new IllegalStateException("AudioFocusBoundary must be called on the main looper");
        }
    }

    private void dispatch(long token, int focusChange) {
        if (Looper.myLooper() != mainLooper) {
            // 平台违约在非主线程投递：fail closed，不转发也不假装成功。
            // Platform violated the main-looper contract: fail closed, forward nothing.
            droppedCallbacks++;
            return;
        }
        final int event = mapFocusChange(focusChange);
        if (event < 0) return;          // 未知平台码不发明事件 | unknown platform codes yield no invented event
        if (released || activeRequest == null || token != activeToken) {
            droppedCallbacks++;
            return;
        }
        listener.onAudioFocusEvent(activeGeneration, event);
    }

    private static int mapFocusChange(int focusChange) {
        if (focusChange == AudioManager.AUDIOFOCUS_GAIN) return EVENT_GAIN;
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS) return EVENT_LOSS;
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) return EVENT_LOSS_TRANSIENT;
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) return EVENT_CAN_DUCK;
        return -1;
    }
}
