package org.d2r.android.port;

import java.util.Arrays;

public final class TouchSkillSilhouetteTest {
    private static int center(int color) {
        int[] pixels = new int[65 * 65];
        pixels[32 * 65 + 32] = color;
        TouchSkillSilhouette.apply(pixels, 65, 65);
        return pixels[32 * 65 + 32];
    }

    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        check(center(0xffeeeeee) == 0xffeeeeee, "bright silver glyph retains shading");
        check(center(0xff202020) == 0, "dark plate removed");
        check(center(0xff8c5c32) == 0, "rust-colored plate removed");
        check(center(0x00ffffff) == 0, "transparent source stays transparent");
        check(center(0x80ffffff) == 0x80ffffff, "source alpha retained");
        int feather = center(0xff808080) >>> 24;
        check(feather > 0 && feather < 255, "soft luminance edge preserved");
        int tinted = center(0xffc8beb4);
        check((tinted >>> 24) == 255 && (tinted & 0xffffff) == 0xc0c0c0, "neutral highlights desaturated");
        int[] frame = new int[65 * 65]; Arrays.fill(frame, 0xffffffff);
        TouchSkillSilhouette.apply(frame, 65, 65);
        check(frame[0] == 0 && frame[64] == 0 && frame[64 * 65] == 0 && frame[frame.length - 1] == 0,
              "bright corner rivets removed");
        check(frame[32 * 65 + 32] == 0xffffffff, "center survives circular mask");
        check(frame[32 * 65 + 1] == 0 && frame[32 * 65 + 4] != 0, "plate edge clipped without removing inner glyph");
        try {
            TouchSkillSilhouette.apply(new int[4], 65, 65);
            throw new AssertionError("bad geometry accepted");
        } catch (IllegalArgumentException expected) { }
        System.out.println("PASS: silver skill silhouette, transparent plate/corners and source shading");
    }
}
