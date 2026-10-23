package android.view;
public class KeyEvent {
    public static final int ACTION_DOWN=0, ACTION_UP=1;
    public static final int KEYCODE_BUTTON_A=96, KEYCODE_BUTTON_B=97, KEYCODE_BUTTON_X=99, KEYCODE_BUTTON_Y=100,
        KEYCODE_BUTTON_L1=102, KEYCODE_BUTTON_R1=103, KEYCODE_BUTTON_L2=104, KEYCODE_BUTTON_R2=105,
        KEYCODE_BUTTON_THUMBL=106, KEYCODE_BUTTON_THUMBR=107, KEYCODE_BUTTON_START=108, KEYCODE_BUTTON_SELECT=109,
        KEYCODE_BACK=4, KEYCODE_DPAD_UP=19, KEYCODE_DPAD_DOWN=20, KEYCODE_DPAD_LEFT=21, KEYCODE_DPAD_RIGHT=22;
    private final int id, action, key;
    public KeyEvent(int id,int action,int key) { this.id=id; this.action=action; this.key=key; }
    public int getSource(){return InputDevice.SOURCE_GAMEPAD;}
    public int getDeviceId(){return id;}
    public int getAction(){return action;}
    public int getKeyCode(){return key;}
    public InputDevice getDevice(){return InputDevice.getDevice(id);}
}
