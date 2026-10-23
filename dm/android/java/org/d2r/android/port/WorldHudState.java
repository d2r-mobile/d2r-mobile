package org.d2r.android.port;

import java.util.Locale;

final class WorldHudState {
    static final int VALUES = 80;
    private WorldHudState() {}

    static String title(float[] h, String[] labels) {
        int key = h[4] != 0 ? (h[6] != 0 ? 2 : 3) : h[5] != 0 ? 4 : 1;
        return String.format(Locale.ROOT, labels[key], (int)h[1]);
    }

    static String goal(float[] h, String[] labels) {
        if (h[4] != 0) return h[6] == 0 ? labels[12] : h[7] > h[1]
            ? String.format(Locale.ROOT, labels[5], (int)h[7]) : labels[6];
        switch ((int)h[9]) {
            case 5: return String.format(Locale.ROOT, labels[11], (int)h[10], (int)h[11]);
            case 4: return labels[10];
            case 3: return labels[7];
            case 1:
            case 2:
                if (h[13] > 1) return String.format(Locale.ROOT, labels[8],
                    (int)h[12], (int)h[13], (int)h[10], (int)h[11]);
                return h[9] == 2 ? String.format(Locale.ROOT, labels[9], (int)h[10], (int)h[11]) : labels[13];
            default: return String.format(Locale.ROOT, labels[14], (int)(Math.max(0, Math.min(1, h[2])) * 100));
        }
    }

    static String labelSuffix(int language, boolean old) {
        switch (language) {
            case 1: return old ? "_old" : "";
            case 2: return "_es";
            case 3: return "_mx";
            case 4: return "_ru";
            default: return "_en";
        }
    }

    // Same isometric projection and skill-area exclusion as front_ios.m.
    static boolean project(float[] v, float[] h, int index, float[] out) {
        if (v == null || v.length < 6 || h == null || h.length < VALUES || out.length < 5 ||
            index < 0 || index >= Math.min(12, (int)h[25]) || !(v[2] > 0) || !(v[3] > 0) ||
            !(h[17] > 0) || !(h[18] > 0) || (h[16] != 1 && h[16] != 2)) return false;
        float left, top, right, bottom, ox, oy;
        boolean full = h[16] == 2;
        if (full) {
            left = v[0] + v[2] * .04f; top = v[1] + v[3] * .06f;
            right = v[0] + v[2] * .96f; bottom = v[1] + v[3] * .78f;
            ox = v[0] + h[28] * v[2]; oy = v[1] + h[29] * v[3];
        } else {
            if (h[20] == 0 || !(h[23] > h[21]) || !(h[24] > h[22])) return false;
            left = v[0] + h[21] * v[2]; right = v[0] + h[23] * v[2];
            top = v[1] + h[22] * v[3]; bottom = v[1] + h[24] * v[3];
            ox = (left + right) * .5f; oy = (top + bottom) * .5f;
            float dx = (right - left) * .06f, dy = (bottom - top) * .08f;
            left += dx; right -= dx; top += dy; bottom -= dy;
        }
        if (ox < left || ox > right || oy < top || oy > bottom) return false;
        float cell = h[17] * v[2] / h[18];
        float dx = h[32 + index * 3] - h[26], dy = h[33 + index * 3] - h[27];
        float x = ox + (dx - dy) * 1.6f * cell, y = oy + (dx + dy) * .8f * cell;
        boolean inside = x >= left && x <= right && y >= top && y <= bottom &&
            !(full && x > v[0] + v[2] * .72f && y > v[1] + v[3] * .45f);
        if (!inside) {
            float tx = x > ox ? (right - ox) / (x - ox) : x < ox ? (left - ox) / (x - ox) : Float.POSITIVE_INFINITY;
            float ty = y > oy ? (bottom - oy) / (y - oy) : y < oy ? (top - oy) / (y - oy) : Float.POSITIVE_INFINITY;
            float t = Math.min(1, Math.min(tx, ty));
            if (Float.isInfinite(t)) t = 0;
            x = ox + (x - ox) * t; y = oy + (y - oy) * t;
            if (full && x > v[0] + v[2] * .72f && y > v[1] + v[3] * .45f) y = v[1] + v[3] * .45f;
        }
        if (!Float.isFinite(x) || !Float.isFinite(y)) return false;
        out[0] = x; out[1] = y; out[2] = inside ? 1 : 0; out[3] = left; out[4] = right;
        return true;
    }
}
