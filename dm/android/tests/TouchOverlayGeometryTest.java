package org.d2r.android.port;

public final class TouchOverlayGeometryTest {
    private static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }
    private static float[] snapshot() {
        float[] v = new float[96];
        v[0] = 100; v[1] = 50; v[2] = 1600; v[3] = 720; v[4] = 667; v[5] = 375;
        v[24] = 120; v[25] = 280; v[26] = 42; // main
        v[27] = 200; v[28] = 160; v[29] = 28; // skill
        v[60] = 80; v[61] = 70; v[62] = 18; // difficulty pick 1
        return v;
    }
    public static void main(String[] args) {
        float[] v = snapshot();
        check(TouchOverlayGeometry.valid(v), "valid snapshot");
        check(Math.abs(TouchOverlayGeometry.x(v, 120) - (100 + 120 * 1600f / 667f)) < .001f,
            "x maps from core logical layout to picture pixels");
        check(Math.abs(TouchOverlayGeometry.y(v, 280) - (50 + 280 * 720f / 375f)) < .001f,
            "y maps from core logical layout to picture pixels");
        check(TouchOverlayGeometry.targetAt(v, TouchOverlayGeometry.x(v, 120),
            TouchOverlayGeometry.y(v, 280), 12) == 0, "main center maps back to main");
        check(TouchOverlayGeometry.targetAt(v, TouchOverlayGeometry.x(v, 200),
            TouchOverlayGeometry.y(v, 160), 12) == 1, "skill center maps back to skill");
        check(TouchOverlayGeometry.targetAt(v, 0, 0, 12) == -1, "outside returns no target");
        v[60] = 0; v[61] = 0; v[62] = 0;
        check(TouchOverlayGeometry.targetAt(v, 0, 0, 12) == -1, "empty pick does not hit");
        // Floating joystick: active origin is used, and +ly moves the knob up (iOS negative-y projection).
        v[8] = 60; v[84] = 70; v[85] = 240; v[86] = 1;
        check(TouchOverlayGeometry.joyActive(v), "joystick active flag");
        check(TouchOverlayGeometry.joyBaseX(v) == 70 && TouchOverlayGeometry.joyBaseY(v) == 240,
            "active floating origin is the base");
        check(TouchOverlayGeometry.joyKnobX(v, 16384) > TouchOverlayGeometry.joyBaseX(v),
            "positive lx moves right");
        check(TouchOverlayGeometry.joyKnobY(v, 16384) < TouchOverlayGeometry.joyBaseY(v),
            "positive ly moves up, matching iOS negative-y");
        v[23] = 1;
        check(TouchOverlayGeometry.navigation(v), "navigation mode is read");
        v[89] = 1;
        check(TouchOverlayGeometry.frontend(v), "front-end scene flag is read");
        v[89] = 0;
        check(!TouchOverlayGeometry.frontend(v), "camp scene is not front end");
        v[2] = 800; v[3] = 360;
        check(Math.abs(TouchOverlayGeometry.x(v, 120) - (100 + 120 * 800f / 667f)) < .001f,
            "resize uses new picture width");
        check(!TouchOverlayGeometry.valid(new float[96]), "empty layout rejected");
        System.out.println("PASS: overlay layout mapping, resize, empty, and target-center consistency");
    }
}
