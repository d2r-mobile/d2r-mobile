package org.d2rmobile.android.diagnostic;

import android.view.inputmethod.EditorInfo;

import java.util.ArrayList;
import java.util.List;

/**
 * Host-only focused tests for the actual DmInputConnection implementation.
 * Compiled and run against the real android.jar; no Android device is used.
 */
public final class DmInputConnectionTest {
    private static int checks;

    private static final class Recorder implements DmInputConnection.Sink {
        final List<String> commits = new ArrayList<>();
        final List<String> deletes = new ArrayList<>();
        final List<String> submits = new ArrayList<>();
        int cancels;

        @Override public void onCommit(CharSequence text) { commits.add(text.toString()); }
        @Override public void onDelete(CharSequence text) { deletes.add(text.toString()); }
        @Override public void onSubmit(CharSequence text) { submits.add(text.toString()); }
        @Override public void onCancel() { cancels++; }
    }

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError("FAIL: " + label);
    }

    private static String before(DmInputConnection connection) {
        CharSequence value = connection.getTextBeforeCursor(4096, 0);
        return value == null ? "" : value.toString();
    }

    private static String after(DmInputConnection connection) {
        CharSequence value = connection.getTextAfterCursor(4096, 0);
        return value == null ? "" : value.toString();
    }

    private static void testLatinCommit() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("hello", 1), "latin commit accepted");
        check("hello".equals(before(c)), "latin text visible");
        check(sink.commits.size() == 1 && "hello".equals(sink.commits.get(0)), "one latin commit");
        check(sink.deletes.isEmpty() && sink.submits.isEmpty(), "latin no unrelated callbacks");
    }

    private static void testChineseComposition() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.setComposingText("n", 1), "first pinyin composing accepted");
        check(c.setComposingText("ni", 1), "second pinyin composing accepted");
        check(c.setComposingText("你", 1), "hanzi composing accepted");
        check(sink.commits.isEmpty(), "composition does not commit");
        check("你".equals(before(c)), "composing text visible at cursor");
        check(c.commitText("你", 1), "final commit accepted");
        check(sink.commits.size() == 1 && "你".equals(sink.commits.get(0)), "commit exactly once");
        check(c.finishComposingText(), "finish after commit accepted");
        check(sink.commits.size() == 1, "finish does not duplicate commit");
    }

    private static void testFinishWithoutExplicitCommit() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.setComposingText("你", 1), "composing accepted");
        check(c.finishComposingText(), "finish accepted");
        check(sink.commits.size() == 1 && "你".equals(sink.commits.get(0)), "finish commits once");
        check(c.finishComposingText(), "second finish accepted");
        check(sink.commits.size() == 1, "second finish is a no-op");
    }

    private static void testSurrogateDeletion() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("A😀B", 1), "emoji commit accepted");
        check(c.deleteSurroundingTextInCodePoints(1, 0), "codepoint delete B accepted");
        check("A😀".equals(before(c)), "one codepoint removes B");
        check(sink.deletes.size() == 1 && "B".equals(sink.deletes.get(0)), "delete callback B");
        check(c.deleteSurroundingTextInCodePoints(1, 0), "codepoint delete emoji accepted");
        check("A".equals(before(c)), "one codepoint removes whole emoji");
        check(sink.deletes.size() == 2 && "😀".equals(sink.deletes.get(1)), "delete callback whole emoji");

        Recorder units = new Recorder();
        DmInputConnection d = new DmInputConnection(units);
        check(d.commitText("A😀", 1), "emoji unit commit accepted");
        check(d.deleteSurroundingText(1, 0), "one unit delete accepted without split");
        check("A".equals(before(d)), "unit delete removes surrogate pair safely");
        check(units.deletes.size() == 1 && "😀".equals(units.deletes.get(0)), "unit delete callback pair");
    }

    private static void testLengthBound() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        StringBuilder maximum = new StringBuilder();
        for (int i = 0; i < DmInputConnection.MAX_TEXT_UTF16_UNITS; i++) maximum.append('x');
        check(c.commitText(maximum, 1), "maximum length accepted");
        check(!c.commitText("y", 1), "over-limit commit rejected");
        check(before(c).length() == DmInputConnection.MAX_TEXT_UTF16_UNITS, "bound preserved");
        check(sink.commits.size() == 1, "over-limit has no commit callback");
    }

    private static void testMalformedUtf16() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("ok", 1), "baseline commit accepted");
        check(!c.commitText("\uD83D", 1), "lone high surrogate rejected");
        check(!c.commitText("\uDE00", 1), "lone low surrogate rejected");
        check(!c.setComposingText("a\uD83Db", 1), "malformed composing rejected");
        check("ok".equals(before(c)), "malformed input preserves committed state");
        check(sink.commits.size() == 1, "malformed input has no commit callback");

        check(c.setComposingText("你", 1), "valid composing before malformed replacement");
        check(!c.setComposingText("\uD83D", 1), "malformed replacement rejected");
        check("ok你".equals(before(c)), "malformed replacement preserves composing");
    }

    private static void testDeletionEndpointAndOverflow() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("A😀B", 0), "probe text accepted");
        check(c.deleteSurroundingText(0, 1), "probe delete accepted");
        check("😀B".equals(after(c)), "after-delete leaves whole emoji and B");
        check(sink.deletes.size() == 1 && "A".equals(sink.deletes.get(0)), "only A deleted");

        Recorder large = new Recorder();
        DmInputConnection d = new DmInputConnection(large);
        check(d.commitText("A😀B", 0), "large-count text accepted");
        check(d.deleteSurroundingText(0, Integer.MAX_VALUE), "huge positive delete accepted");
        check("".equals(after(d)), "huge positive delete removes remaining text");
        check(large.deletes.size() == 1 && "A😀B".equals(large.deletes.get(0)), "huge delete exact text");
        check(d.deleteSurroundingText(Integer.MAX_VALUE, 0), "huge negative-side delete accepted");
    }

    private static void testCursorInsidePairAndComposingPosition() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("😀", 0), "emoji baseline accepted");
        check(!c.commitText("X", 2), "cursor inside existing pair rejected");
        check(sink.commits.size() == 1, "rejected cursor has no commit callback");
        check("😀".equals(after(c)), "rejected cursor preserves existing text");

        Recorder composing = new Recorder();
        DmInputConnection d = new DmInputConnection(composing);
        check(d.setComposingText("ab", 1), "supported composing position accepted");
        check(!d.setComposingText("x", 0), "unsupported composing position rejected");
        check("ab".equals(before(d)), "rejected composing position preserves text");
        check(composing.commits.isEmpty(), "rejected composing position has no commit");
    }

    private static void testCodePointComposingUnits() {
        // Android BaseInputConnection excludes the composing span when choosing
        // surrounding deletion boundaries. A + composing 你, delete one UTF-16
        // unit before the cursor must delete A and retain 你.
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("A", 1), "composing baseline accepted");
        check(c.setComposingText("你", 1), "composing accepted");
        check(c.deleteSurroundingText(1, 0), "before-unit delete accepted");
        check("你".equals(before(c)), "before-unit delete retains composing");
        check(sink.deletes.size() == 1 && "A".equals(sink.deletes.get(0)), "before-unit emits only committed A");
        check(sink.commits.size() == 1, "before-unit does not commit composing");
        check(c.finishComposingText(), "finish after surrounding delete accepted");
        check("你".equals(before(c)) && sink.commits.size() == 2, "finish commits retained composing once");

        // Codepoint deletion of a committed prefix must not consume composing.
        Recorder codepoints = new Recorder();
        DmInputConnection p = new DmInputConnection(codepoints);
        check(p.commitText("A", 1), "codepoint baseline accepted");
        check(p.setComposingText("😀", 1), "emoji composing accepted");
        check(p.deleteSurroundingTextInCodePoints(2, 0), "prefix codepoint delete accepted");
        check("😀".equals(before(p)), "prefix codepoint delete retains composing");
        check(codepoints.deletes.size() == 1 && "A".equals(codepoints.deletes.get(0)),
                "prefix codepoint emits only committed A");

        // Nonempty committed suffix: delete after the cursor, not composing.
        Recorder suffix = new Recorder();
        DmInputConnection s = new DmInputConnection(suffix);
        check(s.commitText("AB", 0), "suffix baseline accepted");
        check(s.setComposingText("你", 1), "suffix composing accepted");
        check(s.deleteSurroundingText(0, 1), "suffix unit delete accepted");
        check("你".equals(before(s)) && "B".equals(after(s)), "suffix delete retains composing and B");
        check(suffix.deletes.size() == 1 && "A".equals(suffix.deletes.get(0)), "suffix delete emits only A");
        check(s.finishComposingText(), "suffix finish accepted");
        check("你".equals(before(s)) && "B".equals(after(s)), "suffix finish commits composing before retained suffix");

        // Codepoint suffix with an emoji on the deleted side.
        Recorder emojiSuffix = new Recorder();
        DmInputConnection e = new DmInputConnection(emojiSuffix);
        check(e.commitText("A😀B", 0), "emoji suffix baseline accepted");
        check(e.setComposingText("你", 1), "emoji suffix composing accepted");
        check(e.deleteSurroundingTextInCodePoints(0, 2), "emoji suffix codepoint delete accepted");
        check("你".equals(before(e)) && "B".equals(after(e)), "emoji suffix retains composing and B");
        check(emojiSuffix.deletes.size() == 1 && "A😀".equals(emojiSuffix.deletes.get(0)),
                "emoji suffix emits exact committed prefix");

        // A huge before length is bounded to committed text and leaves composing.
        Recorder huge = new Recorder();
        DmInputConnection h = new DmInputConnection(huge);
        check(h.commitText("A", 1), "huge composing baseline accepted");
        check(h.setComposingText("你", 1), "huge composing accepted");
        check(h.deleteSurroundingText(Integer.MAX_VALUE, 0), "huge before delete accepted");
        check("你".equals(before(h)), "huge before delete retains composing");
        check(huge.deletes.size() == 1 && "A".equals(huge.deletes.get(0)), "huge before emits only A");
    }

    private static void testSubmitCancelAndStaleSession() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("done", 1), "submit baseline accepted");
        check(c.performEditorAction(EditorInfo.IME_ACTION_DONE), "editor done accepted");
        check(sink.submits.size() == 1 && "done".equals(sink.submits.get(0)), "submit text once");
        check(!c.commitText("late", 1), "late commit after submit rejected");
        check(sink.commits.size() == 1, "late commit has no callback");

        Recorder cancelSink = new Recorder();
        DmInputConnection cancelled = new DmInputConnection(cancelSink);
        check(cancelled.commitText("cancel", 1), "cancel baseline accepted");
        check(cancelled.cancel(), "cancel accepted");
        check(cancelSink.cancels == 1, "cancel callback once");
        check(!cancelled.commitText("late", 1), "late commit after cancel rejected");
        check(cancelSink.commits.size() == 1, "no late commit after cancel");

        Recorder oldSink = new Recorder();
        DmInputConnection old = new DmInputConnection(oldSink);
        check(old.commitText("old", 1), "old session commit");
        old.closeConnection();
        check(!old.commitText("stale", 1), "closed old connection rejects commit");
        Recorder newSink = new Recorder();
        DmInputConnection newer = new DmInputConnection(newSink);
        check(newer.commitText("new", 1), "new session commit");
        check(oldSink.commits.size() == 1, "old sink unchanged by new session");
        check(newSink.commits.size() == 1 && "new".equals(newSink.commits.get(0)), "new sink only new text");

        Recorder resetSink = new Recorder();
        DmInputConnection reset = new DmInputConnection(resetSink);
        check(reset.commitText("before", 1), "reset baseline accepted");
        reset.reset();
        check(!reset.commitText("after", 1), "reset terminal rejects commit");
        check(resetSink.commits.size() == 1, "reset has no late callback");
    }

    public static void main(String[] args) {
        testLatinCommit();
        testChineseComposition();
        testFinishWithoutExplicitCommit();
        testSurrogateDeletion();
        testLengthBound();
        testMalformedUtf16();
        testDeletionEndpointAndOverflow();
        testCursorInsidePairAndComposingPosition();
        testCodePointComposingUnits();
        testSubmitCancelAndStaleSession();
        System.out.println("PASS ime boundary: " + checks + " checks");
    }
}
