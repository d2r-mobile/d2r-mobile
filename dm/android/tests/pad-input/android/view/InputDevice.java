package android.view;
import java.util.HashMap;
import java.util.Map;
public class InputDevice {
    public static final int SOURCE_GAMEPAD = 0x401, SOURCE_JOYSTICK = 0x1000010, SOURCE_DPAD = 0x201;
    public static final Map<Integer, InputDevice> devices = new HashMap<>();
    public static InputDevice getDevice(int id) { return devices.get(id); }
    public int getVendorId() { return 0x045e; }
}
