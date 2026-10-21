package android.os;

/** Narrow host-test SDK level boundary for the API 29/30 guard. */
public final class Build {
    private Build() { }

    public static final class VERSION {
        public static int SDK_INT = 30;

        private VERSION() { }
    }
}
