// #170 主机测试用窄 stub；签名与真实 android.media.AudioFocusRequest 的 Builder 子集一致。
// #170 narrow host-test stub matching the real AudioFocusRequest.Builder subset.
package android.media;

public class AudioFocusRequest {
    private final int focusGain;
    private final AudioAttributes attributes;
    private final boolean pauseWhenDucked;
    private final boolean acceptsDelayed;
    private final AudioManager.OnAudioFocusChangeListener listener;
    private final android.os.Handler handler;

    private AudioFocusRequest(Builder b) {
        this.focusGain = b.focusGain;
        this.attributes = b.attributes;
        this.pauseWhenDucked = b.pauseWhenDucked;
        this.acceptsDelayed = b.acceptsDelayed;
        this.listener = b.listener;
        this.handler = b.handler;
    }

    public int getFocusGain() { return focusGain; }
    public AudioAttributes getAudioAttributes() { return attributes; }
    public boolean willPauseWhenDucked() { return pauseWhenDucked; }
    public boolean getAcceptsDelayedFocusGain() { return acceptsDelayed; }
    public AudioManager.OnAudioFocusChangeListener getOnAudioFocusChangeListener() { return listener; }
    public android.os.Handler getOnAudioFocusChangeHandler() { return handler; }

    public static final class Builder {
        private final int focusGain;
        private AudioAttributes attributes;
        private boolean pauseWhenDucked;
        private boolean acceptsDelayed;
        private AudioManager.OnAudioFocusChangeListener listener;
        private android.os.Handler handler;

        public Builder(int focusGain) { this.focusGain = focusGain; }

        public Builder setAudioAttributes(AudioAttributes attributes) {
            this.attributes = attributes;
            return this;
        }

        public Builder setWillPauseWhenDucked(boolean pauseWhenDucked) {
            this.pauseWhenDucked = pauseWhenDucked;
            return this;
        }

        public Builder setAcceptsDelayedFocusGain(boolean acceptsDelayed) {
            this.acceptsDelayed = acceptsDelayed;
            return this;
        }

        public Builder setOnAudioFocusChangeListener(AudioManager.OnAudioFocusChangeListener listener) {
            this.listener = listener;
            return this;
        }

        public Builder setOnAudioFocusChangeListener(AudioManager.OnAudioFocusChangeListener listener,
                                                     android.os.Handler handler) {
            this.listener = listener;
            this.handler = handler;
            return this;
        }

        public AudioFocusRequest build() {
            if (listener == null) throw new IllegalStateException("listener == null");
            return new AudioFocusRequest(this);
        }
    }
}
