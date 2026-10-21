package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;

/** Focused host test: actual DmTextEditorHost + DmTextEditorView + DmInputConnection. */
public final class DmTextEditorHostTest {
    private static int checks;

    private static final class Recorder implements DmInputConnection.Sink {
        int commits;
        int deletes;
        int submits;
        int cancels;
        String lastCommit;
        String lastSubmit;

        @Override public void onCommit(CharSequence text) {
            commits++;
            lastCommit = text.toString();
        }
        @Override public void onDelete(CharSequence text) { deletes++; }
        @Override public void onSubmit(CharSequence text) {
            submits++;
            lastSubmit = text.toString();
        }
        @Override public void onCancel() { cancels++; }
    }

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError("FAIL: " + label);
    }

    private static Activity activityWithNativeContent() {
        Activity activity = new Activity();
        ViewGroup content = new ViewGroup(activity);
        View nativeSurface = new View(activity);
        content.addView(nativeSurface);
        activity.setContentView(content);
        return activity;
    }

    private static ViewGroup contentOf(Activity activity) {
        return activity.findViewById(android.R.id.content);
    }

    private static DmTextEditorView attachedEditor(Activity activity) {
        ViewGroup content = contentOf(activity);
        if (content.getChildCount() < 2) return null;
        View child = content.getChildAt(content.getChildCount() - 1);
        return child instanceof DmTextEditorView ? (DmTextEditorView) child : null;
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        Activity activity = activityWithNativeContent();
        ViewGroup content = contentOf(activity);
        View nativeSurface = content.getChildAt(0);
        InputMethodManager imm = new InputMethodManager();
        DmTextEditorHost host = new DmTextEditorHost(activity, imm);

        check(content.getChildCount() == 1, "construction preserves existing content");
        check(imm.showCalls == 0, "construction does not show keyboard");
        check(!host.isAttached() && !host.isSessionActive(), "construction is inactive");

        Recorder first = new Recorder();
        check(host.show(first), "explicit show accepted");
        check(imm.showCalls == 1 && host.isAttached() && host.isSessionActive(), "show attaches active editor");
        check(host.isShowRequestAccepted(), "accepted show reported");
        check(content.getChildCount() == 2 && content.getChildAt(0) == nativeSurface,
                "existing NativeActivity content preserved");
        DmTextEditorView editor = attachedEditor(activity);
        check(editor != null && editor.getParent() == content, "owned editor is attached");
        DmInputConnection connection =
                (DmInputConnection) editor.onCreateInputConnection(new EditorInfo());
        check(connection != null && connection.commitText("abc", 1), "actual connection works through host");
        check(first.commits == 1 && "abc".equals(first.lastCommit), "host forwards commit");

        check(host.hide(), "explicit hide accepted");
        check(!host.isAttached() && !host.isSessionActive(), "hide detaches and retires");
        check(first.cancels == 1, "hide cancels once");
        check(content.getChildCount() == 1 && content.getChildAt(0) == nativeSurface,
                "hide leaves only existing content");
        check(!host.hide(), "second hide is idle");
        check(first.cancels == 1, "second hide does not duplicate cancel");

        imm.showResult = false;
        Recorder rejected = new Recorder();
        check(!host.show(rejected), "IMM false returns false");
        check(content.getChildCount() == 1 && !host.isAttached(), "IMM false rolls back attachment");
        check(rejected.cancels == 0, "IMM false invents no cancel");
        imm.showResult = true;

        imm.showException = new RuntimeException("platform");
        Recorder thrown = new Recorder();
        boolean threw = false;
        try {
            host.show(thrown);
        } catch (RuntimeException expected) {
            threw = true;
        }
        check(threw, "platform exception propagated");
        check(content.getChildCount() == 1 && !host.isAttached(), "platform exception rolls back attachment");
        check(thrown.cancels == 0, "platform exception invents no cancel");
        imm.showException = null;

        // Focus refusal must roll back before the IMM show request.
        View.rejectFocus = true;
        Activity focusRefusalActivity = activityWithNativeContent();
        ViewGroup focusRefusalContent = contentOf(focusRefusalActivity);
        InputMethodManager focusRefusalImm = new InputMethodManager();
        DmTextEditorHost focusRefusalHost = new DmTextEditorHost(focusRefusalActivity, focusRefusalImm);
        Recorder refused = new Recorder();
        int refusedShowCalls = focusRefusalImm.showCalls;
        check(!focusRefusalHost.show(refused), "focus refusal returns false");
        check(focusRefusalImm.showCalls == refusedShowCalls, "focus refusal does not request IMM");
        check(focusRefusalContent.getChildCount() == 1
                && !focusRefusalHost.isAttached() && !focusRefusalHost.isSessionActive(),
                "focus refusal detaches owned editor");
        check(refused.cancels == 0, "focus refusal invents no cancel");
        View.rejectFocus = false;

        // A platform hide exception must still detach the owned editor.
        Activity hideFailureActivity = activityWithNativeContent();
        ViewGroup hideFailureContent = contentOf(hideFailureActivity);
        InputMethodManager hideFailureImm = new InputMethodManager();
        DmTextEditorHost hideFailureHost = new DmTextEditorHost(hideFailureActivity, hideFailureImm);
        Recorder hideFailure = new Recorder();
        check(hideFailureHost.show(hideFailure), "hide-failure session accepted");
        hideFailureImm.hideException = new RuntimeException("hide failed");
        boolean hideThrew = false;
        try {
            hideFailureHost.hide();
        } catch (RuntimeException expected) {
            hideThrew = true;
        }
        check(hideThrew, "hide platform exception propagated");
        check(hideFailureContent.getChildCount() == 1
                && !hideFailureHost.isAttached() && !hideFailureHost.isSessionActive(),
                "hide exception still detaches owned editor");
        check(!hideFailureHost.hide(), "hide retry is idle after cleanup");
        hideFailureImm.hideException = null;
        check(hideFailureHost.release(), "release after hide exception succeeds");

        Activity releaseFailureActivity = activityWithNativeContent();
        ViewGroup releaseFailureContent = contentOf(releaseFailureActivity);
        InputMethodManager releaseFailureImm = new InputMethodManager();
        DmTextEditorHost releaseFailureHost = new DmTextEditorHost(releaseFailureActivity, releaseFailureImm);
        Recorder releaseFailure = new Recorder();
        check(releaseFailureHost.show(releaseFailure), "release-failure session accepted");
        releaseFailureImm.hideException = new RuntimeException("hide failed");
        boolean releaseThrew = false;
        try {
            releaseFailureHost.release();
        } catch (RuntimeException expected) {
            releaseThrew = true;
        }
        check(releaseThrew, "release platform exception propagated");
        check(releaseFailureContent.getChildCount() == 1
                && !releaseFailureHost.isAttached() && !releaseFailureHost.isSessionActive(),
                "release exception still detaches owned editor");
        check(!releaseFailureHost.release(), "release retry is terminal after cleanup");
        releaseFailureImm.hideException = null;

        Recorder focus = new Recorder();
        check(host.show(focus), "focus session accepted");
        DmTextEditorView focusEditor = attachedEditor(activity);
        check(focusEditor != null, "focus editor attached");
        focusEditor.onFocusChanged(false, 0, null);
        check(!host.isAttached() && !host.isSessionActive(), "focus loss detaches inactive editor");
        check(focus.cancels == 1, "focus loss cancels once");
        check(content.getChildCount() == 1, "focus loss leaves only existing content");
        host.hide();
        check(focus.cancels == 1, "hide after focus loss does not duplicate");

        Recorder submitted = new Recorder();
        check(host.show(submitted), "submit session accepted");
        DmTextEditorView submitEditor = attachedEditor(activity);
        DmInputConnection submitConnection =
                (DmInputConnection) submitEditor.onCreateInputConnection(new EditorInfo());
        check(submitConnection.commitText("done", 1), "submit text accepted");
        check(submitConnection.performEditorAction(EditorInfo.IME_ACTION_DONE), "submit action accepted");
        check(!host.isAttached() && !host.isSessionActive(), "submit detaches inactive editor");
        check(submitted.submits == 1 && "done".equals(submitted.lastSubmit), "submit once");
        check(submitted.cancels == 0, "submit does not cancel");
        check(content.getChildCount() == 1, "submit leaves only existing content");
        check(!host.hide(), "hide after submit is idle");

        Recorder repeated = new Recorder();
        check(host.show(repeated), "first repeated show accepted");
        check(host.hide(), "first repeated hide accepted");
        check(host.show(repeated), "second repeated show accepted");
        check(host.hide(), "second repeated hide accepted");
        check(repeated.cancels == 2, "each hide cancels its session");
        check(content.getChildCount() == 1, "repeated lifecycle preserves content");
        check(host.release(), "release accepted");
        check(content.getChildCount() == 1, "release preserves existing content");
        check(!host.release(), "second release is idempotent");
        check(!host.show(new Recorder()), "released host rejects show");

        // An old host only removes its own editor, not a later host's editor.
        Activity shared = activityWithNativeContent();
        ViewGroup sharedContent = contentOf(shared);
        InputMethodManager sharedImm = new InputMethodManager();
        DmTextEditorHost oldHost = new DmTextEditorHost(shared, sharedImm);
        DmTextEditorHost newHost = new DmTextEditorHost(shared, sharedImm);
        Recorder oldSink = new Recorder();
        Recorder newSink = new Recorder();
        check(oldHost.show(oldSink), "old host show accepted");
        check(newHost.show(newSink), "new host show accepted");
        check(sharedContent.getChildCount() == 3, "two owned editors coexist with native content");
        check(oldHost.release(), "old host release accepted");
        check(sharedContent.getChildCount() == 2, "old release removes only its editor");
        check(newHost.isAttached() && newHost.isSessionActive(), "new host remains active");
        check(newHost.hide(), "new host hide accepted");
        check(sharedContent.getChildCount() == 1, "new host cleanup preserves native content");

        // Off-main control is rejected before attaching or showing.
        Activity offMainActivity = activityWithNativeContent();
        InputMethodManager offMainImm = new InputMethodManager();
        DmTextEditorHost offMain = new DmTextEditorHost(offMainActivity, offMainImm);
        int offMainShow = offMainImm.showCalls;
        final boolean[] rejectedOffMain = {false};
        Thread thread = new Thread(() -> {
            try {
                offMain.show(new Recorder());
            } catch (IllegalStateException expected) {
                rejectedOffMain[0] = true;
            }
        });
        thread.start();
        try {
            thread.join();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new AssertionError("interrupted");
        }
        check(rejectedOffMain[0], "off-main host show rejected");
        check(offMainImm.showCalls == offMainShow, "off-main show does not touch platform");
        check(contentOf(offMainActivity).getChildCount() == 1, "off-main show attaches nothing");

        System.out.println("PASS ime activity host: " + checks + " checks");
    }
}
