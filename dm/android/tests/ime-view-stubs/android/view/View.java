package android.view;

import android.content.Context;
import android.graphics.Rect;
import android.os.IBinder;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

/** Narrow host-test View boundary; production extends the real android.jar View. */
public class View {
    private final Context context;
    private boolean focusable;
    private boolean focusableInTouchMode;
    private boolean focused;

    public View(Context context) {
        this.context = context;
    }

    public final Context getContext() {
        return context;
    }

    public void setFocusable(boolean value) {
        focusable = value;
    }

    public void setFocusableInTouchMode(boolean value) {
        focusableInTouchMode = value;
    }

    public final boolean requestFocus() {
        focused = true;
        return true;
    }

    public IBinder getWindowToken() {
        return null;
    }

    public boolean isFocusable() {
        return focusable;
    }

    public boolean isFocusableInTouchMode() {
        return focusableInTouchMode;
    }

    public boolean isFocused() {
        return focused;
    }

    public boolean onCheckIsTextEditor() {
        return false;
    }

    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        return null;
    }

    protected void onFocusChanged(boolean focused, int direction, Rect previouslyFocusedRect) {
    }

    public void onWindowFocusChanged(boolean hasWindowFocus) {
    }

    protected void onAttachedToWindow() {
    }

    protected void onDetachedFromWindow() {
    }

    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        return false;
    }
}
