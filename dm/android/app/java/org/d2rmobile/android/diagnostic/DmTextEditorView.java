package org.d2rmobile.android.diagnostic;

import android.content.Context;
import android.graphics.Rect;
import android.os.IBinder;
import android.os.Looper;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/**
 * Small owned text-editor view and text-session owner.
 *
 * <p>The caller explicitly starts a session with {@link #show(DmInputConnection.Sink)}.
 * The view advertises itself as a text editor and returns a fresh accepted
 * {@link DmInputConnection} from {@link #onCreateInputConnection(EditorInfo)}.
 * The platform show request and the actual keyboard visibility are different
 * facts: {@link #isShowRequestAccepted()} only records that
 * {@link InputMethodManager#showSoftInput(View, int)} returned true. This class
 * does not claim that a keyboard became visible.</p>
 *
 * <p>One session owns one connection and the caller sink. Hide, focus/window
 * loss, detach, Back or explicit cancel retire the session exactly once; a
 * subsequent connection is a new session, so stale callbacks cannot write into
 * it. All public control methods and lifecycle callbacks require the Android
 * main looper. No user text is logged.</p>
 *
 * <p>This is an independent platform boundary. It is not yet called by the
 * native app, does not handle keyboard insets/occlusion and does not feed a
 * guest text consumer.</p>
 */
public final class DmTextEditorView extends View {

    /** Plain-text session with no suggestions or personalized learning. */
    public static final int INPUT_TYPE =
            InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;

    private final InputMethodManager inputMethodManager;
    private final Looper mainLooper;
    private Session active;
    private long nextGeneration = 1;
    private boolean backGestureInProgress;

    private static final class Session {
        final long generation;
        final DmInputConnection.Sink callerSink;
        DmInputConnection connection;
        boolean showRequestAccepted;
        boolean retired;

        Session(long generation, DmInputConnection.Sink callerSink) {
            this.generation = generation;
            this.callerSink = callerSink;
        }
    }

    public DmTextEditorView(Context context) {
        this(context, (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE));
    }

    public DmTextEditorView(Context context, InputMethodManager inputMethodManager) {
        super(context);
        if (inputMethodManager == null) {
            throw new IllegalArgumentException("inputMethodManager == null");
        }
        this.inputMethodManager = inputMethodManager;
        this.mainLooper = Looper.getMainLooper();
        if (mainLooper == null) throw new IllegalStateException("no main looper");
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    /**
     * Starts one explicit text session and requests the platform keyboard.
     * Returns the real showSoftInput result. On false the session and any
     * connection are retired and no cancel/submit callback is invented.
     * A true result is only an accepted request, not proof of visibility.
     */
    public boolean show(DmInputConnection.Sink callerSink) {
        requireMainLooper();
        if (callerSink == null) throw new IllegalArgumentException("callerSink == null");
        retire(active, true);

        Session session = new Session(nextGeneration++, callerSink);
        active = session;
        requestFocus();
        boolean accepted;
        try {
            accepted = inputMethodManager.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
        } catch (RuntimeException failure) {
            retire(session, false);
            throw failure;
        }
        if (!accepted) {
            retire(session, false);
            return false;
        }
        session.showRequestAccepted = true;
        return true;
    }

    /**
     * Fills the real EditorInfo contract and returns the session's one
     * connection. Repeated platform queries reuse the same connection and its
     * document/composing state; only a new show starts a new session.
     */
    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        requireMainLooper();
        if (active == null || active.retired || outAttrs == null) return null;
        outAttrs.inputType = INPUT_TYPE;
        outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE
                | EditorInfo.IME_FLAG_NO_FULLSCREEN
                | EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING;
        outAttrs.actionId = EditorInfo.IME_ACTION_DONE;
        outAttrs.actionLabel = null;
        outAttrs.hintText = null;
        outAttrs.label = null;
        if (active.connection == null) {
            active.connection = new DmInputConnection(new SessionSink(active));
        }
        int cursor = 0;
        CharSequence before = active.connection.getTextBeforeCursor(
                DmInputConnection.MAX_TEXT_UTF16_UNITS, 0);
        if (before != null) cursor = before.length();
        outAttrs.initialSelStart = cursor;
        outAttrs.initialSelEnd = cursor;
        outAttrs.initialCapsMode = 0;
        return active.connection;
    }

    /** Explicit hide/cancel; emits caller onCancel at most once. */
    public boolean hide() {
        requireMainLooper();
        if (active == null) return false;
        retire(active, true);
        return true;
    }

    /** Explicit cancel; terminal for the active session. */
    public boolean cancel() {
        requireMainLooper();
        if (active == null) return false;
        retire(active, true);
        return true;
    }

    /** Explicit release alias for a terminal hide/cancel; idempotent. */
    public boolean release() {
        return cancel();
    }

    /** True only while one explicit session is owned. */
    public boolean isSessionActive() {
        return active != null && !active.retired;
    }

    /** True only if the platform show request returned true; not visibility. */
    public boolean isShowRequestAccepted() {
        return active != null && active.showRequestAccepted;
    }

    /** Generation of the active session, or 0 when idle. */
    public long sessionGeneration() {
        return active == null ? 0 : active.generation;
    }

    @Override
    protected void onFocusChanged(boolean focused, int direction, Rect previouslyFocusedRect) {
        super.onFocusChanged(focused, direction, previouslyFocusedRect);
        if (!focused) retire(active, true);
    }

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        super.onWindowFocusChanged(hasWindowFocus);
        if (!hasWindowFocus) retire(active, true);
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        retire(active, true);
    }

    @Override
    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && backGestureInProgress) {
            int action = event == null ? KeyEvent.ACTION_UP : event.getAction();
            if (action == KeyEvent.ACTION_UP) {
                backGestureInProgress = false;
            }
            return true; // consume the owned BACK gesture through its release
        }
        if (keyCode == KeyEvent.KEYCODE_BACK && active != null
                && (event == null || event.getAction() == KeyEvent.ACTION_DOWN)) {
            backGestureInProgress = true;
            retire(active, true);
            return true;
        }
        return super.onKeyPreIme(keyCode, event);
    }

    private void retire(Session session, boolean notifyCancel) {
        if (session == null || session.retired) return;
        session.retired = true;
        if (active == session) active = null;
        if (session.connection != null) session.connection.closeConnection();
        inputMethodManager.hideSoftInputFromWindow(windowToken(), InputMethodManager.HIDE_NOT_ALWAYS);
        if (notifyCancel) session.callerSink.onCancel();
    }

    private IBinder windowToken() {
        return getWindowToken();
    }

    private void requireMainLooper() {
        if (Looper.myLooper() != mainLooper) {
            throw new IllegalStateException("DmTextEditorView must be called on the main looper");
        }
    }

    private final class SessionSink implements DmInputConnection.Sink {
        private final Session session;

        SessionSink(Session session) {
            this.session = session;
        }

        private boolean current() {
            return active == session && !session.retired;
        }

        @Override
        public void onCommit(CharSequence text) {
            if (current()) session.callerSink.onCommit(text);
        }

        @Override
        public void onDelete(CharSequence deletedText) {
            if (current()) session.callerSink.onDelete(deletedText);
        }

        @Override
        public void onSubmit(CharSequence text) {
            if (!current()) return;
            retire(session, false);
            session.callerSink.onSubmit(text);
        }

        @Override
        public void onCancel() {
            if (!current()) return;
            retire(session, false);
            session.callerSink.onCancel();
        }
    }
}
