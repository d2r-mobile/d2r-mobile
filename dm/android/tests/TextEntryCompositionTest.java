package org.d2r.android.port;

import android.text.SpanWatcher;
import android.text.Spannable;
import android.text.Spanned;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Focused host regression: actual SpanWatcher callbacks without changing text. */
public final class TextEntryCompositionTest {
    private static int checks;
    private static int failures;

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) {
            failures++;
            System.err.println("FAIL " + checks + ": " + label);
        }
    }

    public static void main(String[] args) {
        FakeSpannable text = new FakeSpannable("ni");
        int[] callbacks = {0};
        boolean[] valid = {true};
        Object composing = new Object();
        TextEntryComposition.observe(text, () -> {
            callbacks[0]++;
            valid[0] = TextEntryRules.valid(text.toString(), true, -1, text.contains(composing));
        });
        callbacks[0] = 0;
        int before = text.length();

        text.setSpan(composing, 0, 2, Spanned.SPAN_COMPOSING);
        check(callbacks[0] > 0, "span added without text change wakes validation");
        check(!valid[0], "span-only compose disables validation");
        check(text.length() == before, "span add did not change characters");

        callbacks[0] = 0;
        text.setSpan(composing, 0, 1, Spanned.SPAN_COMPOSING);
        check(callbacks[0] > 0, "setComposingRegion-style span change wakes validation");
        check(!valid[0], "span-only region change keeps validation disabled");
        check(text.length() == before, "span change did not change characters");

        callbacks[0] = 0;
        text.removeSpan(composing);
        check(callbacks[0] > 0, "finishComposingText-style span removal wakes validation");
        check(valid[0], "span-only finish re-enables validation");
        check(text.length() == before, "span removal did not change characters");

        if (failures != 0) {
            System.err.println("TextEntryCompositionTest: " + checks + " checks, " + failures + " failures");
            System.exit(1);
        }
        System.out.println("PASS: production SpanWatcher observes composition span-only transitions: "
            + checks + " checks");
    }

    /** Minimal in-memory Spannable; only the contract the production observer uses. */
    private static final class FakeSpannable implements Spannable {
        private final String text;
        private final Map<Object, Span> spans = new LinkedHashMap<>();

        FakeSpannable(String text) { this.text = text; }

        boolean contains(Object what) { return spans.containsKey(what); }

        @Override public void setSpan(Object what, int start, int end, int flags) {
            Span previous = spans.put(what, new Span(start, end, flags));
            if (previous == null) notifyAdded(what, start, end);
            else notifyChanged(what, previous.start, previous.end, start, end);
        }

        @Override public void removeSpan(Object what) {
            Span old = spans.remove(what);
            if (old != null) notifyRemoved(what, old.start, old.end);
        }

        @Override public <T> T[] getSpans(int start, int end, Class<T> type) {
            List<T> found = new ArrayList<>();
            for (Object what : spans.keySet()) {
                Span span = spans.get(what);
                if (span.start < end && start < span.end && type.isInstance(what)) found.add(type.cast(what));
            }
            @SuppressWarnings("unchecked")
            T[] result = (T[]) Array.newInstance(type, found.size());
            return found.toArray(result);
        }

        @Override public int getSpanStart(Object what) {
            Span span = spans.get(what);
            return span == null ? -1 : span.start;
        }

        @Override public int getSpanEnd(Object what) {
            Span span = spans.get(what);
            return span == null ? -1 : span.end;
        }

        @Override public int getSpanFlags(Object what) {
            Span span = spans.get(what);
            return span == null ? 0 : span.flags;
        }

        @Override public int nextSpanTransition(int start, int limit, Class type) {
            int next = limit;
            for (Span span : spans.values()) {
                if (span.start > start && span.start < next) next = span.start;
                if (span.end > start && span.end < next) next = span.end;
            }
            return next;
        }

        @Override public int length() { return text.length(); }
        @Override public char charAt(int index) { return text.charAt(index); }
        @Override public CharSequence subSequence(int start, int end) { return text.subSequence(start, end); }
        @Override public String toString() { return text; }

        private void notifyAdded(Object what, int start, int end) {
            for (SpanWatcher watcher : watchers()) watcher.onSpanAdded(this, what, start, end);
        }

        private void notifyChanged(Object what, int oldStart, int oldEnd, int newStart, int newEnd) {
            for (SpanWatcher watcher : watchers()) watcher.onSpanChanged(this, what, oldStart, oldEnd, newStart, newEnd);
        }

        private void notifyRemoved(Object what, int start, int end) {
            for (SpanWatcher watcher : watchers()) watcher.onSpanRemoved(this, what, start, end);
        }

        private List<SpanWatcher> watchers() {
            List<SpanWatcher> found = new ArrayList<>();
            for (Object what : spans.keySet()) if (what instanceof SpanWatcher) found.add((SpanWatcher) what);
            return found;
        }

        private static final class Span {
            final int start;
            final int end;
            final int flags;
            Span(int start, int end, int flags) { this.start = start; this.end = end; this.flags = flags; }
        }
    }
}
