package android.view;

/** Narrow host-test KeyEvent boundary. */
public class KeyEvent {
    public static final int KEYCODE_BACK = 4;
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;

    private final int action;

    public KeyEvent(int action, int code) {
        this.action = action;
    }

    public int getAction() {
        return action;
    }
}
