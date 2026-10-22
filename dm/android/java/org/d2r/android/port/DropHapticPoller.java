package org.d2r.android.port;

// Single-slot haptic queue lifecycle. The native producer may publish while
// the Activity is paused; resume discards exactly that stale slot before the
// handler starts polling. This class has no Android dependency so the actual
// production lifecycle can be exercised by the host regression.
final class DropHapticPoller {
    interface Source { int take(); }

    private final Source source;
    private boolean running;

    DropHapticPoller(Source source) {
        if (source == null) throw new IllegalArgumentException("source");
        this.source = source;
    }

    void resume() {
        if (running) return;
        source.take(); // discard anything published while paused
        running = true;
    }

    void pause() {
        running = false;
    }

    int poll() {
        return running ? source.take() : 0;
    }

    boolean running() {
        return running;
    }
}
