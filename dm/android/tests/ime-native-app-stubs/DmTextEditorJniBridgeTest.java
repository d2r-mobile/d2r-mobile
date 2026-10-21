package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;

/** Focused bridge/host test; native delivery is replaced only by the test seam. */
public final class DmTextEditorJniBridgeTest {
    private static int checks;

    private static final class Recorder implements DmTextEditorJniBridge.EventSink {
        int calls;
        int lastEvent = -1;
        int lastUnits = -1;
        String lastPayload;
        long lastSession;

        @Override
        public void onTextEvent(long session, int event, String payload, int codeUnits) {
            calls++;
            lastSession = session;
            lastEvent = event;
            lastPayload = payload;
            lastUnits = codeUnits;
        }
    }

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError("FAIL: " + label);
    }

    private static ViewGroup content(Activity activity) {
        return activity.findViewById(android.R.id.content);
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        Activity activity = new Activity();
        ViewGroup content = new ViewGroup(activity);
        View nativeSurface = new View(activity);
        content.addView(nativeSurface);
        activity.setContentView(content);
        InputMethodManager imm = new InputMethodManager();
        Recorder events = new Recorder();
        DmTextEditorJniBridge bridge =
                new DmTextEditorJniBridge(activity, 42L, events, imm);

        check(content.getChildCount() == 1, "bridge construction is inactive");
        check(bridge.show(), "bridge explicit show accepted");
        check(content.getChildCount() == 2 && bridge.isActive(), "bridge attaches actual host editor");
        DmTextEditorView editor =
                (DmTextEditorView) content.getChildAt(content.getChildCount() - 1);
        DmInputConnection connection =
                (DmInputConnection) editor.onCreateInputConnection(new EditorInfo());
        check(connection.commitText("A你😀", 1), "bridge connection commit");
        check(events.calls == 1 && events.lastSession == 42L
                && events.lastEvent == DmTextEditorJniBridge.EVENT_COMMIT
                && events.lastUnits == 4, "native event commit bounded");
        check("A你😀".equals(events.lastPayload), "native event commit payload");
        check(connection.deleteSurroundingText(1, 0), "bridge connection delete");
        check(events.calls == 2 && events.lastEvent == DmTextEditorJniBridge.EVENT_DELETE
                && events.lastUnits == 2, "native event delete bounded");
        check("😀".equals(events.lastPayload), "native event delete payload");
        check(connection.performEditorAction(EditorInfo.IME_ACTION_DONE), "bridge submit");
        check(events.calls == 3 && events.lastEvent == DmTextEditorJniBridge.EVENT_SUBMIT
                && events.lastUnits == 2, "native event submit bounded");
        check("A你".equals(events.lastPayload), "native event submit payload");
        check(!bridge.isActive() && content.getChildCount() == 1, "submit retires host editor");
        check(bridge.release(), "bridge release after submit");
        check(!bridge.release(), "second release idempotent");
        int calls = events.calls;
        bridge.onCommit("late");
        check(events.calls == calls, "released bridge suppresses late native event");
        check(!bridge.show(), "released bridge rejects show");
        check(content.getChildAt(0) == nativeSurface, "native content preserved");
        System.out.println("PASS ime jni bridge: " + checks + " checks");
    }
}
