package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.content.Context;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.view.inputmethod.InputMethodManager;

/**
 * Small real Activity host for the accepted {@link DmTextEditorView}.
 *
 * <p>The host mounts only its owned 1x1 transparent editor into the Activity's
 * existing {@code android.R.id.content} view on an explicit {@link #show} call.
 * It never replaces or removes the NativeActivity rendering surface. When the
 * session is hidden, cancelled or released, only the owned editor is detached,
 * focus is cleared and the editor no longer intercepts gameplay touch. Creating
 * the host does not show the keyboard or attach the editor.</p>
 *
 * <p>All control calls require the main looper. Cleanup is idempotent; a
 * released host rejects further shows and cannot remove a later host's view.
 * The host delegates composition/connection/session behavior to the accepted
 * {@link DmTextEditorView}; it does not add a second editor or JNI queue.</p>
 *
 * <p>This is still not native App wiring: no JNI caller, keyboard insets,
 * occlusion handling or guest text consumer is introduced here.</p>
 */
public final class DmTextEditorHost {
    private static final int EDITOR_WIDTH = 1;
    private static final int EDITOR_HEIGHT = 1;

    private final Activity activity;
    private final InputMethodManager inputMethodManager;
    private final Looper mainLooper;
    private final DmTextEditorView editor;
    private boolean attached;
    private boolean released;
    private boolean detaching;

    public DmTextEditorHost(Activity activity) {
        this(activity, (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE));
    }

    public DmTextEditorHost(Activity activity, InputMethodManager inputMethodManager) {
        if (activity == null) throw new IllegalArgumentException("activity == null");
        if (inputMethodManager == null) {
            throw new IllegalArgumentException("inputMethodManager == null");
        }
        this.activity = activity;
        this.inputMethodManager = inputMethodManager;
        this.mainLooper = Looper.getMainLooper();
        if (mainLooper == null) throw new IllegalStateException("no main looper");
        this.editor = new DmTextEditorView(activity, inputMethodManager);
        this.editor.setOnFocusChangeListener((view, hasFocus) -> {
            if (!hasFocus) hide();
        });
    }

    /**
     * Starts an explicit text session: mounts the owned editor only now and
     * asks the accepted View/session to show. Returns false for a released,
     * unattachable or platform-rejected show; all failure paths detach the
     * owned editor. A true result is an accepted show request, not proof of
     * keyboard visibility.
     */
    public boolean show(DmInputConnection.Sink callerSink) {
        requireMainLooper();
        if (released || callerSink == null) return false;
        if (editor.isSessionActive() || attached) hide();
        if (!attachEditor()) return false;
        if (!editor.requestFocus()) {
            detachEditor();
            return false;
        }
        boolean accepted;
        try {
            accepted = editor.show(new HostSink(callerSink));
        } catch (RuntimeException failure) {
            cleanupAfterShowFailure(failure);
            throw failure;
        }
        if (!accepted) {
            hide();
            return false;
        }
        return true;
    }

    /** Hides the active session and detaches only the owned editor. */
    public boolean hide() {
        requireMainLooper();
        boolean wasActive = editor.isSessionActive();
        boolean wasAttached = attached || editor.getParent() != null;
        try {
            if (wasActive) editor.hide();
        } finally {
            detachEditor();
        }
        return wasActive || wasAttached;
    }

    /** Terminal idempotent cleanup; later show calls are rejected. */
    public boolean release() {
        requireMainLooper();
        if (released) {
            if (attached || editor.getParent() != null) {
                detachEditor();
                return true;
            }
            return false;
        }
        released = true;
        try {
            if (editor.isSessionActive()) editor.hide();
        } finally {
            detachEditor();
        }
        return true;
    }

    public boolean isSessionActive() {
        return editor.isSessionActive();
    }

    public boolean isShowRequestAccepted() {
        return editor.isShowRequestAccepted();
    }

    public boolean isAttached() {
        return attached && editor.getParent() != null;
    }

    public long sessionGeneration() {
        return editor.sessionGeneration();
    }

    private boolean attachEditor() {
        if (attached && editor.getParent() != null) return true;
        View root = activity.findViewById(android.R.id.content);
        if (!(root instanceof ViewGroup)) return false;
        ViewGroup content = (ViewGroup) root;
        try {
            content.addView(editor, new ViewGroup.LayoutParams(EDITOR_WIDTH, EDITOR_HEIGHT));
        } catch (RuntimeException failure) {
            detachEditor();
            throw failure;
        }
        attached = true;
        return true;
    }

    private void detachEditor() {
        if (detaching) return;
        detaching = true;
        try {
            if (editor.isFocused()) editor.clearFocus();
            ViewParent parent = editor.getParent();
            if (parent instanceof ViewGroup) {
                ((ViewGroup) parent).removeView(editor);
            }
            attached = false;
        } finally {
            detaching = false;
        }
    }

    private void cleanupAfterShowFailure(RuntimeException original) {
        try {
            if (editor.isSessionActive()) editor.hide();
        } catch (RuntimeException cleanupFailure) {
            original.addSuppressed(cleanupFailure);
        } finally {
            try {
                detachEditor();
            } catch (RuntimeException cleanupFailure) {
                original.addSuppressed(cleanupFailure);
            }
        }
    }

    private void requireMainLooper() {
        if (Looper.myLooper() != mainLooper) {
            throw new IllegalStateException("DmTextEditorHost must be called on the main looper");
        }
    }

    private final class HostSink implements DmInputConnection.Sink {
        private final DmInputConnection.Sink caller;

        HostSink(DmInputConnection.Sink caller) {
            this.caller = caller;
        }

        @Override
        public void onCommit(CharSequence text) {
            caller.onCommit(text);
        }

        @Override
        public void onDelete(CharSequence deletedText) {
            caller.onDelete(deletedText);
        }

        @Override
        public void onSubmit(CharSequence text) {
            detachEditor();
            caller.onSubmit(text);
        }

        @Override
        public void onCancel() {
            detachEditor();
            caller.onCancel();
        }
    }
}
