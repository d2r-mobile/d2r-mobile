package org.d2r.android.port;

import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;
import java.util.function.Consumer;
import org.d2rmobile.android.diagnostic.AudioFocusBoundary;

final class GameAudioFocus implements AudioFocusBoundary.Listener {
    private final AudioFocusBoundary boundary;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Consumer<Boolean> output;
    private int generation, revision;
    private boolean resumed, destroyed;

    GameAudioFocus(AudioManager manager, Consumer<Boolean> output) {
        this.output = output;
        boundary = new AudioFocusBoundary(manager, this);
    }

    void resume() {
        if (resumed || destroyed) return;
        resumed = true;
        int result = boundary.request(++generation, true);
        publish(result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
    }

    void pause() {
        resumed = false;
        generation++;
        publish(false);
        boundary.abandon();
    }

    void destroy() {
        if (destroyed) return;
        pause();
        destroyed = true;
        boundary.release();
    }

    @Override public void onAudioFocusEvent(int token, int event) {
        if (destroyed || !resumed || token != generation) return;
        publish(event == AudioFocusBoundary.EVENT_GAIN);
    }

    private void publish(boolean allowed) {
        int token = ++revision;
        // AAudio stop/reopen runs after the platform focus callback has returned.
        handler.post(() -> {
            if (token == revision) output.accept(allowed && resumed && !destroyed);
        });
    }
}
