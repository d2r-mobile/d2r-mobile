package org.d2r.android.port;

/** Pack normalized straight RGBA (the units used by the upstream ControlArt)
 * into Android ARGB without OR-ing independent components. */
final class TouchSocketColor {
    private TouchSocketColor() {}

    static int argb(float red, float green, float blue, float alpha) {
        return (channel(alpha) << 24) | (channel(red) << 16) |
               (channel(green) << 8) | channel(blue);
    }

    // The actual face-border selection from DmTouchControlArt.h; keep the
    // empty/held branches here so the regression exercises the same choice.
    static int faceColor(boolean held, boolean empty) {
        if (empty) {
            float gray = held ? .72f : .62f;
            return argb(gray, gray, gray, held ? .80f : .60f);
        }
        return argb(held ? .64f : .26f, held ? .64f : .29f,
                    held ? .64f : .28f, held ? .72f : .55f);
    }

    static float faceWidth(float side, boolean held, boolean empty) {
        return side * (held ? .018f : (empty ? .012f : .009f));
    }

    static int emptyCrossColor(boolean held) {
        return argb(.60f, .65f, .64f, held ? .75f : .25f);
    }

    private static int channel(float value) {
        int v = Math.round(Math.max(0f, Math.min(1f, value)) * 255f);
        return v & 0xff;
    }
}
