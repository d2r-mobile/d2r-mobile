// Narrow InputManager boundary; production uses the real Android API.
package android.hardware.input;

import android.os.Handler;

public class InputManager {
    public interface InputDeviceListener {
        void onInputDeviceAdded(int deviceId);
        void onInputDeviceRemoved(int deviceId);
        void onInputDeviceChanged(int deviceId);
    }

    public int registerCalls;
    public int unregisterCalls;
    public boolean failRegister;
    public boolean failUnregister;
    public InputDeviceListener listener;
    public Handler handler;

    public void registerInputDeviceListener(InputDeviceListener listener, Handler handler) {
        registerCalls++;
        if (failRegister) throw new IllegalStateException("register failed");
        this.listener = listener;
        this.handler = handler;
    }

    public void unregisterInputDeviceListener(InputDeviceListener listener) {
        unregisterCalls++;
        if (failUnregister) throw new IllegalStateException("unregister failed");
        if (this.listener == listener) this.listener = null;
    }

    public void emitAdded(int deviceId) { listener.onInputDeviceAdded(deviceId); }
    public void emitRemoved(int deviceId) { listener.onInputDeviceRemoved(deviceId); }
    public void emitChanged(int deviceId) { listener.onInputDeviceChanged(deviceId); }
}
