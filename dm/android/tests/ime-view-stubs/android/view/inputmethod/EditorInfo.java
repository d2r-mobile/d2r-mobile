package android.view.inputmethod;

/** Narrow host-test EditorInfo boundary; field-only contract. */
public class EditorInfo {
    public static final int IME_ACTION_DONE = 6;
    public static final int IME_ACTION_GO = 2;
    public static final int IME_ACTION_SEND = 4;
    public static final int IME_ACTION_SEARCH = 3;
    public static final int IME_MASK_ACTION = 255;
    public static final int IME_FLAG_NO_FULLSCREEN = 33554432;
    public static final int IME_FLAG_NO_PERSONALIZED_LEARNING = 16777216;

    public int inputType;
    public int imeOptions;
    public int initialSelStart;
    public int initialSelEnd;
    public int initialCapsMode;
    public int actionId;
    public CharSequence actionLabel;
    public CharSequence hintText;
    public CharSequence label;
}
