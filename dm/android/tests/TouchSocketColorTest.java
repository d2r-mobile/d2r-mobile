package org.d2r.android.port;

/** Exact packed colors used by the upstream socket treatment. */
public final class TouchSocketColorTest {
    private static int failures;
    private static void equal(int actual, int expected, String what) {
        if (actual != expected) {
            failures++;
            System.err.printf("FAIL %s: %08x != %08x%n", what, actual, expected);
        }
    }
    private static void near(float actual, float expected, String what) {
        if (Math.abs(actual - expected) > 0.0001f) {
            failures++;
            System.err.printf("FAIL %s: %f != %f%n", what, actual, expected);
        }
    }
    public static void main(String[] args) {
        // DmTouchControlArt.h metal/well/rim/face/primary values, normalized
        // straight RGBA -> Android ARGB.
        equal(TouchSocketColor.argb(.26f, .28f, .28f, .9f), 0xE6424747, "metal normal top");
        equal(TouchSocketColor.argb(.26f, .28f, .28f, .22f), 0x38424747, "metal empty top");
        equal(TouchSocketColor.argb(.055f, .065f, .07f, .95f), 0xF20E1112, "metal normal mid");
        equal(TouchSocketColor.argb(.021f, .021f, .021f, .94f), 0xF0050505, "well normal top");
        equal(TouchSocketColor.argb(.021f, .021f, .021f, .08f), 0x14050505, "well empty top");
        equal(TouchSocketColor.argb(.008f, .012f, .015f, .9f), 0xE6020304, "rim normal");
        equal(TouchSocketColor.argb(.008f, .012f, .015f, .38f), 0x61020304, "rim empty");
        equal(TouchSocketColor.argb(.31f, .23f, .22f, .52f), 0x854F3B38, "primary ring");

        // Actual face-border selection, not just detached argb() calls.
        equal(TouchSocketColor.faceColor(false, false), 0x8C424A47, "face normal idle");
        equal(TouchSocketColor.faceColor(true, false), 0xB8A3A3A3, "face normal held");
        equal(TouchSocketColor.faceColor(false, true), 0x999E9E9E, "face empty idle");
        equal(TouchSocketColor.faceColor(true, true), 0xCCB8B8B8, "face empty held");
        near(TouchSocketColor.faceWidth(100f, false, false), .9f, "face width normal idle");
        near(TouchSocketColor.faceWidth(100f, true, false), 1.8f, "face width normal held");
        near(TouchSocketColor.faceWidth(100f, false, true), 1.2f, "face width empty idle");
        near(TouchSocketColor.faceWidth(100f, true, true), 1.8f, "face width empty held");
        equal(TouchSocketColor.emptyCrossColor(false), 0x4099A6A3, "empty cross idle");
        equal(TouchSocketColor.emptyCrossColor(true), 0xBF99A6A3, "empty cross held");
        if (failures != 0) throw new AssertionError(failures + " socket color checks failed");
        System.out.println("PASS: upstream socket colors and empty/held face selection match exact Android ARGB");
    }
}
