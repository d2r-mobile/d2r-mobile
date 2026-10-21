package android.view.inputmethod;

import android.os.IBinder;
import android.view.View;

/** Narrow host-test InputMethodManager boundary. */
public class InputMethodManager {
    public static final int SHOW_IMPLICIT = 0;
    public static final int HIDE_NOT_ALWAYS = 0;

    public boolean showResult = true;
    public RuntimeException showException;
    public RuntimeException hideException;
    public int showCalls;
    public int hideCalls;

    public boolean showSoftInput(View view, int flags) {
        showCalls++;
        if (showException != null) throw showException;
        return showResult;
    }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags) {
        hideCalls++;
        if (hideException != null) throw hideException;
        return true;
    }
}
