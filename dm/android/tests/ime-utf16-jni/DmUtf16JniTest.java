package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;

/** Actual Java bridge -> actual JNI text event function with a real JVM string. */
public final class DmUtf16JniTest {
    static { System.loadLibrary("dmutf16jni"); }

    private static int checks;

    private static void check(boolean ok, String label) {
        checks++;
        if (!ok) throw new AssertionError("FAIL: " + label);
    }

    private static native void testStart(long session);
    private static native int testUnits();
    private static native int testEvent();
    private static native long testSequence();
    private static native String testPayload();

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        Activity activity = new Activity();
        ViewGroup content = new ViewGroup(activity);
        content.addView(new View(activity));
        activity.setContentView(content);
        InputMethodManager imm = new InputMethodManager();

        testStart(42L);
        DmTextEditorJniBridge bridge = new DmTextEditorJniBridge(activity, 42L, null, imm);
        check(bridge.show(), "actual Java bridge shows");
        DmTextEditorView editor = (DmTextEditorView) content.getChildAt(content.getChildCount() - 1);
        DmInputConnection connection = (DmInputConnection) editor.onCreateInputConnection(new EditorInfo());
        check(connection.commitText("A你😀", 1), "actual Java commit");
        check(testEvent() == 0 && testUnits() == 4, "JNI commit metadata");
        check("A你😀".equals(testPayload()), "JNI commit payload");
        check(connection.deleteSurroundingText(1, 0), "actual Java delete");
        check(testEvent() == 1 && testUnits() == 2, "JNI delete metadata");
        check("😀".equals(testPayload()), "JNI delete payload");
        check(connection.performEditorAction(EditorInfo.IME_ACTION_DONE), "actual Java submit");
        check(testEvent() == 2 && testUnits() == 2, "JNI submit metadata");
        check("A你".equals(testPayload()), "JNI submit payload");
        check(testSequence() == 3, "JNI event sequence");

        testStart(43L);
        DmTextEditorJniBridge cancel = new DmTextEditorJniBridge(activity, 43L, null, imm);
        cancel.onCancel();
        check(testEvent() == 3 && testUnits() == 0, "JNI cancel clears payload");
        check("".equals(testPayload()), "JNI cancel empty payload");
        System.out.println("PASS real JVM/JNI UTF-16 payload: " + checks + " checks");
    }
}
