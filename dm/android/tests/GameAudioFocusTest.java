package org.d2r.android.port;

import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Looper;
import java.util.ArrayList;
import java.util.List;

public final class GameAudioFocusTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) {
        Looper.prepareMainLooper();
        AudioManager manager = new AudioManager();
        List<Boolean> output = new ArrayList<>();
        GameAudioFocus focus = new GameAudioFocus(manager, output::add);
        focus.resume();
        check(output.isEmpty());
        Looper.getMainLooper().drain();
        check(output.get(output.size()-1));
        AudioFocusRequest first = manager.lastRequest;
        manager.emit(first, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
        Looper.getMainLooper().drain();
        check(!output.get(output.size()-1));
        manager.emit(first, AudioManager.AUDIOFOCUS_GAIN);
        focus.pause();
        Looper.getMainLooper().drain();
        check(!output.get(output.size()-1) && manager.abandonCalls==1);
        focus.resume();
        Looper.getMainLooper().drain();
        check(output.get(output.size()-1));
        manager.emit(first, AudioManager.AUDIOFOCUS_LOSS);
        Looper.getMainLooper().drain();
        check(output.get(output.size()-1));
        manager.emit(manager.lastRequest, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK);
        Looper.getMainLooper().drain();
        check(!output.get(output.size()-1));
        manager.emit(manager.lastRequest, AudioManager.AUDIOFOCUS_GAIN);
        Looper.getMainLooper().drain();
        check(output.get(output.size()-1));
        focus.pause();manager.nextResult=AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        focus.resume();Looper.getMainLooper().drain();
        check(!output.get(output.size()-1));
        manager.emit(manager.lastRequest, AudioManager.AUDIOFOCUS_GAIN);
        Looper.getMainLooper().drain();check(output.get(output.size()-1));
        focus.pause();manager.nextResult=AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        focus.resume();Looper.getMainLooper().drain();check(!output.get(output.size()-1));
        focus.pause();manager.nextResult=AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        focus.resume();focus.destroy();Looper.getMainLooper().drain();
        check(!output.get(output.size()-1));
        int requests=manager.requestCalls;
        focus.resume();manager.emit(first,AudioManager.AUDIOFOCUS_GAIN);
        Looper.getMainLooper().drain();check(manager.requestCalls==requests&&!output.get(output.size()-1));
        System.out.println("PASS playable audio focus: deferred output, transient/duck gain, stale/pause/destroy, delayed/failed grant");
    }
}
