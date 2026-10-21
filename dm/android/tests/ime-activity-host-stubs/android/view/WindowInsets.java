package android.view;

import android.graphics.Insets;
import android.os.Build;

/** Narrow host-test WindowInsets boundary with explicit API 30 Type usage. */
public class WindowInsets {
    public static final class Type {
        public static final int IME = 8;

        private Type() { }

        public static int ime() {
            requireApi30("WindowInsets.Type.ime");
            return IME;
        }
    }

    public static int apiCalls;
    private final boolean visible;
    private final Insets insets;

    public WindowInsets(boolean visible, Insets insets) {
        this.visible = visible;
        this.insets = insets;
    }

    public boolean isVisible(int type) {
        requireApi30("WindowInsets.isVisible");
        apiCalls++;
        return type == Type.IME && visible;
    }

    public Insets getInsets(int type) {
        requireApi30("WindowInsets.getInsets");
        apiCalls++;
        if (type != Type.IME || !visible) return Insets.NONE;
        return insets;
    }

    private static void requireApi30(String api) {
        if (Build.VERSION.SDK_INT < 30) {
            throw new UnsupportedOperationException(api + " is API 30+");
        }
    }
}
