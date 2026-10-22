package org.d2r.android.port;

import android.text.SpanWatcher;
import android.text.Spannable;
import android.text.Spanned;

/** Minimal bridge from Android span events to the text-dialog validate action. */
final class TextEntryComposition {
    private TextEntryComposition() {}

    /**
     * Attach a span watcher so text-entry validation runs when the IME changes
     * only its composing/marked span (finishComposingText, setComposingRegion)
     * without changing a single character. TextWatcher alone misses those.
     */
    static void observe(Spannable text, Runnable changed) {
        text.setSpan(new SpanWatcher() {
            @Override public void onSpanAdded(Spannable source, Object what, int start, int end) { changed.run(); }
            @Override public void onSpanRemoved(Spannable source, Object what, int start, int end) { changed.run(); }
            @Override public void onSpanChanged(Spannable source, Object what, int oldStart, int oldEnd, int newStart, int newEnd) { changed.run(); }
        }, 0, text.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
    }
}
