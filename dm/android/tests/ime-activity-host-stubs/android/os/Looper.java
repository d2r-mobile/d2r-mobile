package android.os;

/** Narrow host-test main-looper boundary. */
public final class Looper {
    private static final ThreadLocal<Looper> CURRENT = new ThreadLocal<>();
    private static Looper main;

    private Looper() {
    }

    public static void prepareMainLooper() {
        main = new Looper();
        CURRENT.set(main);
    }

    public static Looper getMainLooper() {
        return main;
    }

    public static Looper myLooper() {
        return CURRENT.get();
    }
}
