// Narrow InputManager hotplug boundary for the single admitted Android controller.
// The native callback is a static JNI method; the helper never carries a native
// pointer and invalidates its session before unregistering.
package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;

public final class ControllerHotplugBoundary implements InputManager.InputDeviceListener {
    public static final int EVENT_ADDED = 0;
    public static final int EVENT_REMOVED = 1;
    public static final int EVENT_CHANGED = 2;

    /** Test seam only; production passes null and calls the registered native method. */
    public interface EventSink {
        void onControllerEvent(long session, int event, int deviceId);
    }

    private final InputManager inputManager;
    private final EventSink eventSink;
    private final Looper mainLooper;
    private final Handler mainHandler;
    private long session;
    private boolean registered;
    private boolean released;

    public ControllerHotplugBoundary(Activity activity, long session) {
        this(activity, session, null);
    }

    public ControllerHotplugBoundary(Activity activity, long session, EventSink eventSink) {
        if (activity == null) throw new IllegalArgumentException("activity == null");
        if (session == 0) throw new IllegalArgumentException("session == 0");
        this.mainLooper = Looper.getMainLooper();
        if (mainLooper == null) throw new IllegalStateException("no main looper");
        this.mainHandler = new Handler(mainLooper);
        this.inputManager = (InputManager) activity.getSystemService(Context.INPUT_SERVICE);
        if (inputManager == null) throw new IllegalStateException("no InputManager");
        this.session = session;
        this.eventSink = eventSink;
    }

    /** Registers on the main Handler; returns false on any platform failure. */
    public boolean register() {
        requireMainLooper();
        if (released) return false;
        if (registered) return true;
        try {
            inputManager.registerInputDeviceListener(this, mainHandler);
            registered = true;
            return true;
        } catch (RuntimeException failure) {
            registered = false;
            return false;
        }
    }

    /**
     * Invalidates the callback session before unregistering, so queued or late
     * callbacks are harmless. Idempotent; returns false only if unregistering
     * the actual listener throws.
     */
    public boolean release() {
        requireMainLooper();
        if (!released) {
            released = true;
            session = 0;
        }
        if (!registered) return true;
        try {
            inputManager.unregisterInputDeviceListener(this);
            registered = false;
            return true;
        } catch (RuntimeException failure) {
            // Keep registered=true: the platform may still retain us, so a
            // later release retries honestly instead of claiming success.
            return false;
        }
    }

    public boolean isRegistered() { return registered; }
    public boolean isReleased() { return released; }

    private void requireMainLooper() {
        if (Looper.myLooper() != mainLooper) {
            throw new IllegalStateException("ControllerHotplugBoundary must be called on the main looper");
        }
    }

    private void dispatch(int event, int deviceId) {
        if (released || session == 0) return;
        if (eventSink != null) eventSink.onControllerEvent(session, event, deviceId);
        else nativeControllerEvent(session, event, deviceId);
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        dispatch(EVENT_ADDED, deviceId);
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        dispatch(EVENT_REMOVED, deviceId);
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        dispatch(EVENT_CHANGED, deviceId);
    }

    private static native void nativeControllerEvent(long session, int event, int deviceId);
}
