package org.d2r.android.port;

/**
 * Pure skill-glyph frame/asset rules shared by the draw-only overlay and host
 * tests. No Android types, no I/O, no mutation of the native snapshot.
 */
final class TouchGlyph {
    private TouchGlyph() {}

    static final int UNKNOWN = 0, EMPTY = 1, GLYPH = 2;
    static final int ATLAS_COUNT = 9;

    // The original core target order is main, F1..F4; the same button masks as
    // dm_input.h. These are used only to choose the pressed frame.
    static long button(int target) {
        switch (target) {
            case 0: return 1L << 0;  // A, main
            case 1: return 1L << 1;  // B, F1
            case 2: return 1L << 2;  // X, F2
            case 3: return 1L << 3;  // Y, F3
            case 4: return 1L << 7;  // R, F4
            default: return 0;
        }
    }

    static String assetName(int atlas) {
        return atlas >= 0 && atlas < ATLAS_COUNT
            ? "touch_art/dm_native_skill_" + atlas + ".png" : null;
    }

    static boolean validAtlasInfo(int[] info) {
        if (info == null || info.length < ATLAS_COUNT * 5) return false;
        for (int i = 0; i < ATLAS_COUNT; i++) {
            if (info[i * 5] > 0 && info[i * 5 + 1] > 0 && info[i * 5 + 2] > 0 &&
                info[i * 5 + 3] > 0 && info[i * 5 + 4] > 0) return true;
        }
        return false;
    }

    // Fills out = [x, y, width, height] for one atlas frame. Reuses the caller's
    // array; no allocation. Rejects a missing atlas, a bad frame, or a frame
    // that would crop outside the atlas.
    static boolean frameRect(int[] info, int atlas, int frame, int[] out) {
        if (info == null || out == null || out.length < 4 ||
            atlas < 0 || atlas >= ATLAS_COUNT || frame < 0) return false;
        int base = atlas * 5;
        if (base + 4 >= info.length) return false;
        int width = info[base], height = info[base + 1];
        int frameWidth = info[base + 2], pitch = info[base + 3], count = info[base + 4];
        if (width <= 0 || height <= 0 || frameWidth <= 0 || pitch <= 0 ||
            count <= 0 || frame >= count) return false;
        long x = (long) frame * pitch;
        if (x + frameWidth > width || frameWidth > width || frameWidth > pitch) return false;
        out[0] = (int) x; out[1] = 0; out[2] = frameWidth; out[3] = height;
        return true;
    }
}
