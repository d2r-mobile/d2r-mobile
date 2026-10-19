package org.d2r.android.port;

/** Pure coordinate bridge shared by the draw-only overlay and its host fixture. */
final class TouchOverlayGeometry {
    private TouchOverlayGeometry() {}

    static boolean valid(float[] v) {
        return v != null && v.length >= 96 && v[2] > 0 && v[3] > 0 && v[4] > 0 && v[5] > 0;
    }

    static float x(float[] v, float logicalX) {
        return v[0] + logicalX * v[2] / v[4];
    }

    static float y(float[] v, float logicalY) {
        return v[1] + logicalY * v[3] / v[5];
    }

    static float radius(float[] v, float logicalRadius) {
        return logicalRadius * v[2] / v[4];
    }

    static boolean navigation(float[] v) {
        return v[23] != 0;
    }

    static boolean frontend(float[] v) {
        return v[89] != 0;
    }

    static boolean joyActive(float[] v) {
        return v[86] != 0;
    }

    static float joyBaseX(float[] v) {
        return joyActive(v) ? v[84] : v[6];
    }

    static float joyBaseY(float[] v) {
        return joyActive(v) ? v[85] : v[7];
    }

    static float joyKnobX(float[] v, long axisX) {
        float n = Math.max(-1f, Math.min(1f, axisX / 32767f));
        return joyBaseX(v) + n * v[8];
    }

    static float joyKnobY(float[] v, long axisY) {
        float n = Math.max(-1f, Math.min(1f, axisY / 32767f));
        return joyBaseY(v) - n * v[8];
    }

    // Returns the first visual target whose mapped center is within its mapped
    // radius. This is the overlay's own draw/hit consistency check; real input
    // remains owned by the native core's dm_tc_hit path.
    static int targetAt(float[] v, float viewX, float viewY, int count) {
        if (!valid(v)) return -1;
        for (int i = 0; i < count && i < 12; i++) {
            float dx = viewX - x(v, v[24 + i * 3]);
            float dy = viewY - y(v, v[25 + i * 3]);
            float r = radius(v, v[26 + i * 3]);
            if (dx * dx + dy * dy <= r * r) return i;
        }
        return -1;
    }
}
