package android.graphics;

/** Narrow host-test Insets boundary. */
public final class Insets {
    public static final Insets NONE = new Insets(0, 0, 0, 0);
    public final int left;
    public final int top;
    public final int right;
    public final int bottom;

    private Insets(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public static Insets of(int left, int top, int right, int bottom) {
        return new Insets(left, top, right, bottom);
    }
}
