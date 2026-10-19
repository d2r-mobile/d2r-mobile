package android.view;

import android.content.Context;
import android.graphics.Rect;
import android.os.IBinder;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

/** Narrow host-test View boundary; production extends the real android.jar View. */
public class View {
    public interface OnFocusChangeListener {
        void onFocusChange(View view, boolean hasFocus);
    }

    private final Context context;
    private ViewParent parent;
    private OnFocusChangeListener focusListener;
    private boolean focusable;
    private boolean focusableInTouchMode;
    private boolean focused;
    public static boolean rejectFocus;

    public View(Context context) {
        this.context = context;
    }

    public final Context getContext() {
        return context;
    }

    public final ViewParent getParent() {
        return parent;
    }

    void setParent(ViewParent parent) {
        this.parent = parent;
    }

    public void setFocusable(boolean value) {
        focusable = value;
    }

    public void setFocusableInTouchMode(boolean value) {
        focusableInTouchMode = value;
    }

    public final boolean requestFocus() {
        if (rejectFocus) return false;
        boolean changed = !focused;
        focused = true;
        if (changed && focusListener != null) focusListener.onFocusChange(this, true);
        return true;
    }

    public void clearFocus() {
        boolean changed = focused;
        focused = false;
        if (changed && focusListener != null) focusListener.onFocusChange(this, false);
    }

    public void setOnFocusChangeListener(OnFocusChangeListener listener) {
        focusListener = listener;
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
        this.focused = focused;
        if (focusListener != null) focusListener.onFocusChange(this, focused);
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
