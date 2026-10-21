// Host control for the actual ControllerHotplugBoundary; only Android
// InputManager/Activity/Handler boundaries are stubbed.
import android.app.Activity;
import android.hardware.input.InputManager;
import android.os.Looper;

import org.d2rmobile.android.diagnostic.ControllerHotplugBoundary;

public final class ControllerHotplugBoundaryTest {
    private static int checks;
    private static int failures;

    private static void check(boolean condition, String what) {
        checks++;
        if (!condition) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    private static final class TestActivity extends Activity {
        final InputManager manager;
        TestActivity(InputManager manager) { this.manager = manager; }
        @Override public Object getSystemService(String name) {
            return "input".equals(name) ? manager : null;
        }
    }

    private static final class Recorder implements ControllerHotplugBoundary.EventSink {
        int calls;
        long session;
        int event = -1;
        int device = -1;
        @Override public void onControllerEvent(long session, int event, int deviceId) {
            calls++;
            this.session = session;
            this.event = event;
            this.device = deviceId;
        }
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        InputManager manager = new InputManager();
        TestActivity activity = new TestActivity(manager);
        Recorder recorder = new Recorder();
        ControllerHotplugBoundary boundary =
                new ControllerHotplugBoundary(activity, 123456789L, recorder);

        check(manager.registerCalls == 0, "construction does not register");
        check(boundary.register(), "main-thread registration succeeds");
        check(manager.registerCalls == 1 && manager.handler != null, "registered with a main Handler");
        check(boundary.isRegistered(), "registered state is visible");
        check(boundary.register(), "repeat registration is idempotent");
        check(manager.registerCalls == 1, "repeat registration does not call the platform twice");

        manager.emitAdded(42);
        manager.emitChanged(42);
        manager.emitRemoved(42);
        check(recorder.calls == 3, "added/changed/removed are forwarded");
        check(recorder.session == 123456789L, "session is carried to native");
        check(recorder.device == 42, "device id is carried to native");

        InputManager.InputDeviceListener late = manager.listener;
        check(boundary.release(), "release succeeds");
        check(manager.unregisterCalls == 1, "release unregisters exactly once");
        check(!boundary.isRegistered() && boundary.isReleased(), "release invalidates state");
        late.onInputDeviceRemoved(42);
        check(recorder.calls == 3, "late callback after release is dropped");
        check(boundary.release(), "second release is idempotent");
        check(manager.unregisterCalls == 1, "second release does not unregister again");

        InputManager failing = new InputManager();
        failing.failRegister = true;
        ControllerHotplugBoundary failed = new ControllerHotplugBoundary(
                new TestActivity(failing), 7L, new Recorder());
        check(!failed.register(), "failed registration returns false");
        check(!failed.isRegistered(), "failed registration is not claimed");
        check(failed.release(), "release after failed registration is safe");
        check(failing.unregisterCalls == 0, "failed registration does not unregister");

        InputManager unregisterFails = new InputManager();
        unregisterFails.failUnregister = true;
        Recorder unregisterRecorder = new Recorder();
        ControllerHotplugBoundary unregisterFailed = new ControllerHotplugBoundary(
                new TestActivity(unregisterFails), 8L, unregisterRecorder);
        check(unregisterFailed.register(), "unregister-failure fixture registers");
        InputManager.InputDeviceListener lateUnregister = unregisterFails.listener;
        check(!unregisterFailed.release(), "platform unregister failure is surfaced");
        check(unregisterFailed.isRegistered(), "failed unregister retains pending registration");
        lateUnregister.onInputDeviceRemoved(42);
        check(unregisterRecorder.calls == 0, "late callback during failed unregister is inert");
        unregisterFails.failUnregister = false;
        check(unregisterFailed.release(), "release retry succeeds after platform recovery");
        check(!unregisterFailed.isRegistered(), "successful retry clears pending registration");
        check(unregisterFails.unregisterCalls == 2, "failed unregister is retried once");
        check(unregisterFailed.release(), "repeat success is idempotent");
        check(unregisterFails.unregisterCalls == 2, "successful unregister is not repeated");

        final boolean[] offMain = new boolean[2];
        Thread thread = new Thread(() -> {
            offMain[0] = throwsMain(() -> boundary.register());
            offMain[1] = throwsMain(boundary::release);
        });
        thread.start();
        try { thread.join(); } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(interrupted);
        }
        check(offMain[0] && offMain[1], "control calls reject a non-main thread");

        System.out.println("ControllerHotplugBoundaryTest: " + checks + " checks, "
                + failures + " failures");
        if (failures != 0) System.exit(1);
    }

    private static boolean throwsMain(Runnable action) {
        try { action.run(); return false; }
        catch (IllegalStateException expected) { return true; }
    }
}
