package org.d2rmobile.android.diagnostic;

import android.os.Bundle;
import android.os.Handler;
import android.view.KeyEvent;
import android.view.inputmethod.CompletionInfo;
import android.view.inputmethod.CorrectionInfo;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputContentInfo;

/**
 * Minimal real {@link InputConnection} boundary for the owned diagnostic package.
 *
 * <p>This class owns a small committed-text buffer and a separate composing
 * buffer at the insertion cursor. Composition is deliberately not reported as
 * committed text. A caller-provided {@link Sink} receives committed insertions,
 * committed deletions, explicit submit and explicit cancel. It is a host-side
 * boundary only: it does not display a keyboard, call JNI or consume input for
 * the guest/game.</p>
 *
 * <p>One instance represents one editor session. {@link #closeConnection()},
 * {@link #reset()}, {@link #submit()} and {@link #cancel()} are terminal;
 * callbacks arriving afterwards are rejected. A new session must use a new
 * instance, so a stale connection cannot insert into a newer session. All
 * methods are expected to be called on the owning UI/control thread.</p>
 *
 * <p>The owned length unit is UTF-16 code units, declared by
 * {@link #MAX_TEXT_UTF16_UNITS}. Text that is not well-formed UTF-16 is
 * rejected atomically. User text is never logged by this class.</p>
 */
public final class DmInputConnection implements InputConnection {

    /** Owned committed-text bound, in UTF-16 code units. */
    public static final int MAX_TEXT_UTF16_UNITS = 512;

    /** Caller sink; methods are invoked in editor order on the owning thread. */
    public interface Sink {
        /** Committed text was inserted at the current cursor. */
        void onCommit(CharSequence text);

        /** Committed text was removed; the exact removed text is supplied. */
        void onDelete(CharSequence deletedText);

        /** Explicit submit; text includes any pending composing text. */
        void onSubmit(CharSequence text);

        /** Explicit cancel or terminal reset. */
        void onCancel();
    }

    private final Sink sink;
    private final StringBuilder committed = new StringBuilder();
    private final StringBuilder composing = new StringBuilder();
    private int cursor;
    private boolean closed;

    public DmInputConnection(Sink sink) {
        if (sink == null) throw new IllegalArgumentException("sink is required");
        this.sink = sink;
    }

    public boolean isClosed() {
        return closed;
    }

    @Override
    public CharSequence getTextBeforeCursor(int length, int flags) {
        if (closed || length <= 0) return "";
        String before = committed.substring(0, cursor) + composing;
        return lastCodeUnits(before, length);
    }

    @Override
    public CharSequence getTextAfterCursor(int length, int flags) {
        if (closed || length <= 0) return "";
        return firstCodeUnits(committed.substring(cursor), length);
    }

    @Override
    public CharSequence getSelectedText(int flags) {
        return null; // no selection model in this boundary
    }

    @Override
    public int getCursorCapsMode(int reqModes) {
        return 0;
    }

    @Override
    public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
        return null;
    }

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        return delete(beforeLength, afterLength, false);
    }

    @Override
    public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
        return delete(beforeLength, afterLength, true);
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (closed) return false;
        String value = copy(text);
        if (value == null || !isWellFormed(value)) return false;
        // In this boundary composing is represented at the insertion cursor.
        // Only the API's "after the text" position is representable; do not
        // accept and silently ignore any other requested cursor position.
        if (newCursorPosition != 1) return false;
        if (committed.length() + value.length() > MAX_TEXT_UTF16_UNITS) return false;
        composing.setLength(0);
        composing.append(value);
        return true;
    }

    @Override
    public boolean setComposingRegion(int start, int end) {
        // Absolute regions over already-committed text cannot be represented
        // without re-committing or duplicating caller text; keep it explicit.
        return false;
    }

    @Override
    public boolean finishComposingText() {
        if (closed || composing.length() == 0) return !closed;
        return commitComposing();
    }

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition) {
        if (closed) return false;
        String value = copy(text);
        if (value == null || !isWellFormed(value)) return false;
        if (committed.length() + value.length() > MAX_TEXT_UTF16_UNITS) return false;
        int start = cursor;
        StringBuilder candidate = new StringBuilder(committed);
        candidate.insert(start, value);
        int newCursor = cursorPosition(start, value.length(), newCursorPosition,
                candidate.length());
        if (!isCodePointBoundary(candidate, newCursor)) return false;
        composing.setLength(0);
        committed.setLength(0);
        committed.append(candidate);
        cursor = newCursor;
        if (value.length() > 0) sink.onCommit(value);
        return true;
    }

    @Override
    public boolean commitCompletion(CompletionInfo text) {
        return false;
    }

    @Override
    public boolean commitCorrection(CorrectionInfo correctionInfo) {
        return false;
    }

    @Override
    public boolean setSelection(int start, int end) {
        return false; // no selection/absolute-cursor model in this slice
    }

    @Override
    public boolean performEditorAction(int editorAction) {
        if (closed) return false;
        switch (editorAction) {
            case EditorInfo.IME_ACTION_DONE:
            case EditorInfo.IME_ACTION_GO:
            case EditorInfo.IME_ACTION_SEND:
            case EditorInfo.IME_ACTION_SEARCH:
                return submit();
            default:
                return false;
        }
    }

    @Override
    public boolean performContextMenuAction(int id) {
        return false;
    }

    @Override
    public boolean beginBatchEdit() {
        return false;
    }

    @Override
    public boolean endBatchEdit() {
        return false;
    }

    @Override
    public boolean sendKeyEvent(KeyEvent event) {
        return false; // no invented key simulation
    }

    @Override
    public boolean clearMetaKeyStates(int states) {
        return false;
    }

    @Override
    public boolean reportFullscreenMode(boolean enabled) {
        return false;
    }

    @Override
    public boolean performPrivateCommand(String action, Bundle data) {
        return false;
    }

    @Override
    public boolean requestCursorUpdates(int cursorUpdateMode) {
        return false;
    }

    @Override
    public Handler getHandler() {
        return null;
    }

    @Override
    public void closeConnection() {
        if (closed) return;
        closed = true;
        composing.setLength(0);
        committed.setLength(0);
        cursor = 0;
    }

    /** Terminal session reset; subsequent callbacks are rejected. */
    public void reset() {
        closeConnection();
    }

    /** Explicit editor submit; terminal for this connection instance. */
    public boolean submit() {
        if (closed) return false;
        closed = true;
        String pending = composing.toString();
        if (!pending.isEmpty()) {
            committed.insert(cursor, pending);
            cursor += pending.length();
            composing.setLength(0);
            sink.onCommit(pending);
        }
        String all = committed.toString();
        sink.onSubmit(all);
        committed.setLength(0);
        cursor = 0;
        return true;
    }

    /** Explicit editor cancel; terminal for this connection instance. */
    public boolean cancel() {
        if (closed) return false;
        closed = true;
        composing.setLength(0);
        committed.setLength(0);
        cursor = 0;
        sink.onCancel();
        return true;
    }

    @Override
    public boolean commitContent(InputContentInfo inputContentInfo, int flags, Bundle opts) {
        return false;
    }

    private boolean commitComposing() {
        String value = composing.toString();
        if (committed.length() + value.length() > MAX_TEXT_UTF16_UNITS) return false;
        committed.insert(cursor, value);
        cursor += value.length();
        composing.setLength(0);
        sink.onCommit(value);
        return true;
    }

    private boolean delete(int beforeLength, int afterLength, boolean codePoints) {
        if (closed || beforeLength < 0 || afterLength < 0) return false;
        StringBuilder removed = new StringBuilder();
        int before = beforeLength;
        if (before > 0) {
            int start = deleteStartBefore(before, codePoints);
            if (start < cursor) {
                removed.append(committed, start, cursor);
                committed.delete(start, cursor);
                cursor = start;
            }
        }
        if (afterLength > 0) {
            int end = deleteEndAfter(afterLength, codePoints);
            if (end > cursor) {
                removed.append(committed, cursor, end);
                committed.delete(cursor, end);
            }
        }
        if (removed.length() > 0) sink.onDelete(removed.toString());
        return true;
    }

    private int deleteStartBefore(int length, boolean codePoints) {
        if (cursor == 0) return 0;
        if (codePoints) {
            int available = Character.codePointCount(committed, 0, cursor);
            int take = Math.min(length, available);
            return Character.offsetByCodePoints(committed, cursor, -take);
        }
        long startLong = (long) cursor - length;
        int start = (int) Math.max(0L, startLong);
        if (start > 0 && start < cursor
                && Character.isLowSurrogate(committed.charAt(start))
                && Character.isHighSurrogate(committed.charAt(start - 1))) {
            start--; // never split a surrogate pair
        }
        return start;
    }

    private int deleteEndAfter(int length, boolean codePoints) {
        int end;
        if (codePoints) {
            int available = Character.codePointCount(committed, cursor, committed.length());
            end = Character.offsetByCodePoints(committed, cursor, Math.min(length, available));
        } else {
            long requested = (long) cursor + length;
            end = (int) Math.min((long) committed.length(), requested);
            if (end < committed.length() && end > cursor
                    && Character.isLowSurrogate(committed.charAt(end))
                    && Character.isHighSurrogate(committed.charAt(end - 1))) {
                end++; // deletion ends inside a pair; include its low surrogate
            }
        }
        return end;
    }

    private static int cursorPosition(int start, int insertedLength, int newCursorPosition,
                                      int resultLength) {
        long end = (long) start + insertedLength;
        long position = newCursorPosition > 0
                ? end + newCursorPosition - 1L
                : (long) start + newCursorPosition;
        return (int) Math.max(0L, Math.min((long) resultLength, position));
    }

    private static boolean isCodePointBoundary(CharSequence text, int index) {
        if (index <= 0 || index >= text.length()) return true;
        return !(Character.isLowSurrogate(text.charAt(index))
                && Character.isHighSurrogate(text.charAt(index - 1)));
    }

    private static String copy(CharSequence text) {
        return text == null ? "" : text.toString();
    }

    private static boolean isWellFormed(String text) {
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (Character.isHighSurrogate(c)) {
                if (i + 1 >= text.length() || !Character.isLowSurrogate(text.charAt(i + 1))) {
                    return false;
                }
                i++;
            } else if (Character.isLowSurrogate(c)) {
                return false;
            }
        }
        return true;
    }

    private static String lastCodeUnits(String text, int length) {
        int start = Math.max(0, text.length() - length);
        if (start > 0 && start < text.length()
                && Character.isLowSurrogate(text.charAt(start))
                && Character.isHighSurrogate(text.charAt(start - 1))) {
            start--;
        }
        return text.substring(start);
    }

    private static String firstCodeUnits(String text, int length) {
        int end = Math.min(text.length(), length);
        if (end > 0 && end < text.length()
                && Character.isLowSurrogate(text.charAt(end))
                && Character.isHighSurrogate(text.charAt(end - 1))) {
            end++;
        }
        return text.substring(0, end);
    }
}
