// #170 仅用于主机测试的窄 Android 边界 stub；不对应真实设备行为。
// #170 narrow Android-boundary stubs for host tests only; not device behaviour.
package android.media;

public class AudioManager {
    public static final int AUDIOFOCUS_GAIN = 1;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT = 2;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK = 3;
    public static final int AUDIOFOCUS_LOSS = -1;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT = -2;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK = -3;
    public static final int AUDIOFOCUS_REQUEST_FAILED = 0;
    public static final int AUDIOFOCUS_REQUEST_GRANTED = 1;
    public static final int AUDIOFOCUS_REQUEST_DELAYED = 2;

    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }

    public int nextResult = AUDIOFOCUS_REQUEST_GRANTED;
    public AudioFocusRequest lastRequest;
    public AudioFocusRequest abandonedRequest;
    public int requestCalls;
    public int abandonCalls;

    public int requestAudioFocus(AudioFocusRequest request) {
        requestCalls++;
        lastRequest = request;
        // 真实平台不会在没有 opt-in 的情况下延迟授予。| A real platform does not delay without the opt-in.
        if (nextResult == AUDIOFOCUS_REQUEST_DELAYED && !request.getAcceptsDelayedFocusGain()) {
            return AUDIOFOCUS_REQUEST_FAILED;
        }
        return nextResult;
    }

    public int abandonAudioFocusRequest(AudioFocusRequest request) {
        abandonCalls++;
        abandonedRequest = request;
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    /** 测试助手：平台按注册的 main Handler 排队投递。| Test helper: the platform queues onto the registered main Handler. */
    public void emit(AudioFocusRequest request, int focusChange) {
        android.os.Handler handler = request.getOnAudioFocusChangeHandler();
        if (handler == null) throw new IllegalStateException("request has no main handler");
        handler.post(() -> request.getOnAudioFocusChangeListener().onAudioFocusChange(focusChange));
    }
}
