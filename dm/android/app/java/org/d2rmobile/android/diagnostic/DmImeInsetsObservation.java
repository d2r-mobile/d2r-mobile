package org.d2rmobile.android.diagnostic;

/** Immutable snapshot of the IME inset observation for one host session. */
public final class DmImeInsetsObservation {
    private static final DmImeInsetsObservation UNKNOWN = new DmImeInsetsObservation(false, false, 0, 0, 0, 0, 0, 0);

    public final boolean known;
    public final boolean imeVisible;
    public final int left;
    public final int top;
    public final int right;
    public final int bottom;
    public final long sessionGeneration;
    public final long sequence;

    public DmImeInsetsObservation(boolean known, boolean imeVisible, int left, int top,
                                  int right, int bottom, long sessionGeneration, long sequence) {
        this.known = known;
        this.imeVisible = imeVisible;
        this.left = Math.max(0, left);
        this.top = Math.max(0, top);
        this.right = Math.max(0, right);
        this.bottom = Math.max(0, bottom);
        this.sessionGeneration = sessionGeneration;
        this.sequence = sequence;
    }

    public static DmImeInsetsObservation unknown(long sessionGeneration, long sequence) {
        return new DmImeInsetsObservation(false, false, 0, 0, 0, 0, sessionGeneration, sequence);
    }

    public static DmImeInsetsObservation none() {
        return UNKNOWN;
    }
}
