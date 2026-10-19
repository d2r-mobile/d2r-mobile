package org.d2rmobile.android.diagnostic;

import android.content.Context;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/** Focused host test for the actual view/session helper and accepted DmInputConnection. */
public final class DmTextEditorViewTest {
    private static int checks;

    private static final class Recorder implements DmInputConnection.Sink {
        int commits;
        int deletes;
        int submits;
        int cancels;
        String lastCommit;
        String lastDelete;
        String lastSubmit;

        @Override public void onCommit(CharSequence text) {
            commits++;
            lastCommit = text.toString();
        }
        @Override public void onDelete(CharSequence text) {
            deletes++;
            lastDelete = text.toString();
        }
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

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        Context context = new Context();
        InputMethodManager imm = new InputMethodManager();
        DmTextEditorView view = new DmTextEditorView(context, imm);
        check(view.onCheckIsTextEditor(), "view advertises a text editor");
        check(!view.isSessionActive(), "no implicit session");
        check(view.onCreateInputConnection(new EditorInfo()) == null, "no connection without a session");

        // Explicit show request and actual EditorInfo/connection contract.
        Recorder first = new Recorder();
        check(view.show(first), "explicit show accepted");
        check(view.isSessionActive() && view.isShowRequestAccepted(), "accepted request marks active session");
        check(imm.showCalls == 1, "one real show request");
        EditorInfo info = new EditorInfo();
        InputConnection raw = view.onCreateInputConnection(info);
        check(raw instanceof DmInputConnection, "actual View returns DmInputConnection");
        check(info.inputType == DmTextEditorView.INPUT_TYPE, "plain no-suggestion input type");
        check((info.imeOptions & EditorInfo.IME_MASK_ACTION) == EditorInfo.IME_ACTION_DONE,
                "done action contract");
        check((info.imeOptions & EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING) != 0,
                "no personalized learning");
        check(info.actionId == EditorInfo.IME_ACTION_DONE, "action id contract");
        check(info.initialSelStart == 0 && info.initialSelEnd == 0, "initial selection");

        DmInputConnection connection = (DmInputConnection) raw;
        check(connection.setComposingText("ni", 1), "composing start");
        check(connection.setComposingText("你", 1), "composing update");
        check(first.commits == 0, "composition is not committed early");
        check(connection.commitText("你", 1), "commit through actual connection");
        check(first.commits == 1 && "你".equals(first.lastCommit), "one committed value");
        check(connection.finishComposingText(), "finish after commit");
        check(first.commits == 1, "finish does not duplicate");

        check(view.hide(), "explicit hide");
        check(!view.isSessionActive(), "hide retires session");
        check(first.cancels == 1, "hide cancels once");
        check(!view.hide(), "second hide is idle");
        check(first.cancels == 1, "second hide does not duplicate cancel");
        check(imm.hideCalls >= 1, "real hide request made");

        // A rejected show must clean up and not claim success.
        imm.showResult = false;
        Recorder rejected = new Recorder();
        int showBefore = imm.showCalls;
        int hideBefore = imm.hideCalls;
        check(!view.show(rejected), "rejected show returns false");
        check(imm.showCalls == showBefore + 1, "rejected show reaches platform");
        check(imm.hideCalls == hideBefore + 1, "rejected show cleans up");
        check(!view.isSessionActive() && !view.isShowRequestAccepted(), "rejected show has no session");
        check(rejected.cancels == 0, "rejected show invents no cancel");
        check(view.onCreateInputConnection(new EditorInfo()) == null, "rejected show has no connection");
        imm.showResult = true;

        // Off-main control is rejected before touching the platform.
        int offMainShow = imm.showCalls;
        final boolean[] offMainRejected = {false};
        Thread thread = new Thread(() -> {
            try {
                view.show(new Recorder());
            } catch (IllegalStateException expected) {
                offMainRejected[0] = true;
            }
        });
        thread.start();
        try {
            thread.join();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new AssertionError("interrupted");
        }
        check(offMainRejected[0], "off-main show rejected");
        check(imm.showCalls == offMainShow, "off-main show does not touch platform");

        // Hide/focus/window/detach/back all retire exactly once.
        Recorder focus = new Recorder();
        check(view.show(focus), "focus session accepted");
        view.onFocusChanged(false, 0, null);
        check(!view.isSessionActive() && focus.cancels == 1, "focus loss cancels once");
        view.hide();
        check(focus.cancels == 1, "hide after focus loss does not duplicate");

        Recorder window = new Recorder();
        check(view.show(window), "window session accepted");
        view.onWindowFocusChanged(false);
        check(!view.isSessionActive() && window.cancels == 1, "window focus loss cancels once");
        view.hide();
        check(window.cancels == 1, "hide after window loss does not duplicate");

        Recorder detached = new Recorder();
        check(view.show(detached), "detach session accepted");
        view.onDetachedFromWindow();
        check(!view.isSessionActive() && detached.cancels == 1, "detach cancels once");
        view.hide();
        check(detached.cancels == 1, "hide after detach does not duplicate");

        Recorder back = new Recorder();
        check(view.show(back), "back session accepted");
        check(view.onKeyPreIme(KeyEvent.KEYCODE_BACK,
                new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK)), "back down handled");
        check(!view.isSessionActive() && back.cancels == 1, "back cancels once");
        check(view.onKeyPreIme(KeyEvent.KEYCODE_BACK,
                new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK)), "back up consumed");
        check(back.cancels == 1, "back up does not duplicate cancel");
        check(!view.onKeyPreIme(KeyEvent.KEYCODE_BACK,
                new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK)), "idle back down not swallowed");
        check(!view.onKeyPreIme(KeyEvent.KEYCODE_BACK,
                new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK)), "idle back up not swallowed");

        // Submit retires the session; a later hide is not another cancel.
        Recorder submit = new Recorder();
        check(view.show(submit), "submit session accepted");
        DmInputConnection submitConnection =
                (DmInputConnection) view.onCreateInputConnection(new EditorInfo());
        check(submitConnection.commitText("done", 1), "submit text accepted");
        check(submitConnection.performEditorAction(EditorInfo.IME_ACTION_DONE), "editor action submitted");
        check(!view.isSessionActive(), "submit retires session");
        check(submit.submits == 1 && "done".equals(submit.lastSubmit), "submit once");
        check(submit.cancels == 0, "submit does not cancel");
        check(!view.hide(), "hide after submit is idle");
        check(submit.cancels == 0, "hide after submit does not duplicate");

        // Connection cancel retires without a second cancel on view hide.
        Recorder cancelled = new Recorder();
        check(view.show(cancelled), "connection-cancel session accepted");
        DmInputConnection cancelledConnection =
                (DmInputConnection) view.onCreateInputConnection(new EditorInfo());
        check(cancelledConnection.cancel(), "connection cancel accepted");
        check(!view.isSessionActive(), "connection cancel retires session");
        check(cancelled.cancels == 1, "connection cancel once");
        check(!view.hide(), "hide after connection cancel is idle");
        check(cancelled.cancels == 1, "hide after connection cancel does not duplicate");

        // Repeated onCreateInputConnection reuses the session document and
        // composing state; committed text survives delete and submit.
        Recorder document = new Recorder();
        check(view.show(document), "document session accepted");
        DmInputConnection documentConnection =
                (DmInputConnection) view.onCreateInputConnection(new EditorInfo());
        check(documentConnection.commitText("abc", 1), "document base committed");
        check(documentConnection.setComposingText("你", 1), "document composing set");
        EditorInfo requeryInfo = new EditorInfo();
        DmInputConnection requery =
                (DmInputConnection) view.onCreateInputConnection(requeryInfo);
        check(requery == documentConnection, "requery reuses the one session connection");
        check("abc你".contentEquals(requery.getTextBeforeCursor(99, 0)), "requery preserves document/composing");
        check(requeryInfo.initialSelStart == 4 && requeryInfo.initialSelEnd == 4,
                "requery reports actual cursor");
        check(requery.deleteSurroundingText(1, 0), "requery delete accepted");
        check("ab你".contentEquals(requery.getTextBeforeCursor(99, 0)), "delete keeps composing and remaining document");
        check(document.deletes == 1 && "c".equals(document.lastDelete), "delete reports exact committed character");
        check(requery.commitText("你", 1), "requery commit accepted");
        check(document.commits == 2, "requery commit is not duplicated");
        EditorInfo afterCommit = new EditorInfo();
        DmInputConnection after =
                (DmInputConnection) view.onCreateInputConnection(afterCommit);
        check(after == requery, "post-commit requery still reuses connection");
        check(afterCommit.initialSelStart == 3 && afterCommit.initialSelEnd == 3,
                "post-commit cursor includes committed text");
        check(after.performEditorAction(EditorInfo.IME_ACTION_DONE), "requery submit accepted");
        check(document.submits == 1 && "ab你".equals(document.lastSubmit), "submit uses retained document");
        check(!view.isSessionActive() && document.cancels == 0, "submit retires without cancel");

        // A new show starts a fresh connection; the old session object is stale.
        Recorder stale = new Recorder();
        check(view.show(stale), "stale session accepted");
        DmInputConnection old = (DmInputConnection) view.onCreateInputConnection(new EditorInfo());
        check(old.commitText("old", 1), "old session text accepted");
        Recorder next = new Recorder();
        check(view.show(next), "new show accepted");
        check(!old.commitText("late", 1), "old session connection cannot write after show");
        DmInputConnection fresh = (DmInputConnection) view.onCreateInputConnection(new EditorInfo());
        check(fresh.commitText("fresh", 1), "new session connection works");
        check(next.commits == 1 && "fresh".equals(next.lastCommit), "fresh text only");
        check(stale.commits == 1 && "old".equals(stale.lastCommit), "old sink unchanged");
        check(stale.cancels == 1, "old session retired once");
        view.hide();
        check(next.cancels == 1, "new session retired once");

        System.out.println("PASS ime view: " + checks + " checks");
    }
}
