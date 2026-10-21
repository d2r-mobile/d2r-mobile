package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.graphics.Insets;
import android.os.Build;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;

/** Focused actual-host test for the additive IME inset observer. */
public final class DmImeInsetsTest {
    private static int checks;

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError("FAIL: " + label);
    }

    private static Activity activityWithNativeContent() {
        Activity activity = new Activity();
        ViewGroup content = new ViewGroup(activity);
        content.width = 1000;
        content.height = 800;
        View nativeSurface = new View(activity);
        content.addView(nativeSurface);
        activity.setContentView(content);
        return activity;
    }

    private static ViewGroup contentOf(Activity activity) {
        return activity.findViewById(android.R.id.content);
    }

    private static DmTextEditorView editorOf(Activity activity) {
        ViewGroup content = contentOf(activity);
        if (content.getChildCount() < 2) return null;
        View child = content.getChildAt(content.getChildCount() - 1);
        return child instanceof DmTextEditorView ? (DmTextEditorView) child : null;
    }

    private static WindowInsets visible(int left, int top, int right, int bottom) {
        return new WindowInsets(true, Insets.of(left, top, right, bottom));
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        Build.VERSION.SDK_INT = 30;
        WindowInsets.apiCalls = 0;

        Activity activity = activityWithNativeContent();
        ViewGroup content = contentOf(activity);
        View nativeSurface = content.getChildAt(0);
        InputMethodManager imm = new InputMethodManager();
        DmTextEditorHost host = new DmTextEditorHost(activity, imm);
        check(host.show(new DmInputConnection.Sink() {
            public void onCommit(CharSequence text) { }
            public void onDelete(CharSequence text) { }
            public void onSubmit(CharSequence text) { }
            public void onCancel() { }
        }), "show accepted");
        DmTextEditorView editor = editorOf(activity);
        check(editor != null, "owned editor attached");
        check(content.getChildCount() == 2 && content.getChildAt(0) == nativeSurface,
                "peer content preserved");
        check(editor.requestApplyInsetsCalls > 0, "actual requestApplyInsets requested");
        check(!host.imeInsetsObservation().known, "initial observation is unknown");

        WindowInsets visibleInsets = visible(10, 20, 30, 40);
        editor.dispatchApplyWindowInsets(visibleInsets);
        DmImeInsetsObservation shown = host.imeInsetsObservation();
        check(shown.known && shown.imeVisible, "API30 visible observation");
        check(shown.left == 10 && shown.top == 20 && shown.right == 30 && shown.bottom == 40,
                "asymmetric geometry preserved");
        check(shown.sessionGeneration == host.sessionGeneration(), "generation captured");
        check(WindowInsets.apiCalls >= 2, "API30 Type/isVisible/getInsets used");

        editor.dispatchApplyWindowInsets(new WindowInsets(false, Insets.of(1, 2, 3, 4)));
        DmImeInsetsObservation hidden = host.imeInsetsObservation();
        check(hidden.known && !hidden.imeVisible, "API30 hidden observation");
        check(hidden.left == 0 && hidden.top == 0 && hidden.right == 0 && hidden.bottom == 0,
                "hidden geometry is neutral");

        editor.dispatchApplyWindowInsets(visible(-4, -5, 6, 7));
        DmImeInsetsObservation bounded = host.imeInsetsObservation();
        check(bounded.left == 0 && bounded.top == 0 && bounded.right == 6 && bounded.bottom == 7,
                "negative geometry safely bounded");

        editor.dispatchApplyWindowInsets(visible(0, 0, Integer.MAX_VALUE, Integer.MAX_VALUE));
        DmImeInsetsObservation overflow = host.imeInsetsObservation();
        check(overflow.known && overflow.imeVisible
                        && overflow.right == content.width && overflow.bottom == content.height,
                "oversized geometry bounded by actual root extent");

        editor.dispatchApplyWindowInsets(visible(900, 0, 900, 0));
        DmImeInsetsObservation asymmetric = host.imeInsetsObservation();
        check(asymmetric.left == 900 && asymmetric.right == 100,
                "asymmetric compatible pair preserved and bounded");

        View.OnApplyWindowInsetsListener stale = editor.getOnApplyWindowInsetsListener();
        check(stale != null, "observer listener is present");
        check(host.hide(), "hide accepted");
        check(!host.isAttached() && !host.isSessionActive(), "hide detaches session");
        check(content.getChildCount() == 1 && content.getChildAt(0) == nativeSurface,
                "hide preserves peer content");
        DmImeInsetsObservation afterHide = host.imeInsetsObservation();
        check(!afterHide.known, "hide invalidates observation");
        stale.onApplyWindowInsets(editor, visibleInsets);
        check(!host.imeInsetsObservation().known
                        && host.imeInsetsObservation().sequence == afterHide.sequence,
                "stale callback cannot update observation");

        check(host.show(new DmInputConnection.Sink() {
            public void onCommit(CharSequence text) { }
            public void onDelete(CharSequence text) { }
            public void onSubmit(CharSequence text) { }
            public void onCancel() { }
        }), "reattach show accepted");
        View.OnApplyWindowInsetsListener fresh = editor.getOnApplyWindowInsetsListener();
        check(fresh != null && fresh != stale, "reattach installs a fresh listener");
        DmImeInsetsObservation afterReattach = host.imeInsetsObservation();
        stale.onApplyWindowInsets(editor, new WindowInsets(true, Insets.of(0, 0, 0, 800)));
        check(!host.imeInsetsObservation().known
                        && host.imeInsetsObservation().sequence == afterReattach.sequence,
                "old listener cannot overwrite new same-view session");
        fresh.onApplyWindowInsets(editor, visible(11, 12, 13, 14));
        DmImeInsetsObservation freshObservation = host.imeInsetsObservation();
        check(freshObservation.known && freshObservation.left == 11
                        && freshObservation.right == 13, "new listener still updates");
        check(host.hide(), "reattach hide accepted");

        check(host.release(), "release accepted");
        check(!host.release(), "second release idempotent");
        check(!host.show(new DmInputConnection.Sink() {
            public void onCommit(CharSequence text) { }
            public void onDelete(CharSequence text) { }
            public void onSubmit(CharSequence text) { }
            public void onCancel() { }
        }), "released host rejects show");
        check(!host.imeInsetsObservation().known, "release invalidates observation");

        // API 29 must be explicitly unknown and must not enter API 30 methods.
        Build.VERSION.SDK_INT = 29;
        WindowInsets.apiCalls = 0;
        Activity legacyActivity = activityWithNativeContent();
        DmTextEditorHost legacy = new DmTextEditorHost(legacyActivity, new InputMethodManager());
        check(legacy.show(new DmInputConnection.Sink() {
            public void onCommit(CharSequence text) { }
            public void onDelete(CharSequence text) { }
            public void onSubmit(CharSequence text) { }
            public void onCancel() { }
        }), "API29 show accepted");
        DmTextEditorView legacyEditor = editorOf(legacyActivity);
        legacyEditor.dispatchApplyWindowInsets(visible(1, 2, 3, 4));
        DmImeInsetsObservation unknown = legacy.imeInsetsObservation();
        check(!unknown.known && !unknown.imeVisible, "API29 observation explicitly unknown");
        check(WindowInsets.apiCalls == 0, "API29 never calls API30 Type/isVisible/getInsets");
        legacy.hide();

        // An unmeasured root extent must stay unknown instead of inventing bounds.
        Build.VERSION.SDK_INT = 30;
        Activity unmeasuredActivity = activityWithNativeContent();
        ViewGroup unmeasuredContent = contentOf(unmeasuredActivity);
        unmeasuredContent.width = 0;
        unmeasuredContent.height = 0;
        DmTextEditorHost unmeasured = new DmTextEditorHost(unmeasuredActivity, new InputMethodManager());
        check(unmeasured.show(new DmInputConnection.Sink() {
            public void onCommit(CharSequence text) { }
            public void onDelete(CharSequence text) { }
            public void onSubmit(CharSequence text) { }
            public void onCancel() { }
        }), "unmeasured show accepted");
        editorOf(unmeasuredActivity).dispatchApplyWindowInsets(visible(1, 2, 3, 4));
        check(!unmeasured.imeInsetsObservation().known,
                "unmeasured root extent reports unknown");
        unmeasured.hide();

        // Off-main host control is rejected before touching the observer.
        Build.VERSION.SDK_INT = 30;
        Activity offMainActivity = activityWithNativeContent();
        DmTextEditorHost offMain = new DmTextEditorHost(offMainActivity, new InputMethodManager());
        final boolean[] rejected = {false};
        Thread thread = new Thread(() -> {
            try {
                offMain.show(new DmInputConnection.Sink() {
                    public void onCommit(CharSequence text) { }
                    public void onDelete(CharSequence text) { }
                    public void onSubmit(CharSequence text) { }
                    public void onCancel() { }
                });
            } catch (IllegalStateException expected) {
                rejected[0] = true;
            }
        });
        thread.start();
        try {
            thread.join();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new AssertionError("interrupted");
        }
        check(rejected[0], "off-main show rejected");
        check(contentOf(offMainActivity).getChildCount() == 1, "off-main leaves peer content");

        System.out.println("PASS ime insets: " + checks + " checks");
    }
}
