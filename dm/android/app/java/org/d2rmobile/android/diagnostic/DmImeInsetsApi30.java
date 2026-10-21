package org.d2rmobile.android.diagnostic;

import android.graphics.Insets;
import android.view.WindowInsets;

/** API 30+ implementation isolated so API 29 never resolves WindowInsets.Type. */
final class DmImeInsetsApi30 {
    private DmImeInsetsApi30() { }

    static DmImeInsetsObservation observe(WindowInsets insets, int width, int height,
                                          long generation, long sequence) {
        if (width <= 0 || height <= 0) {
            return DmImeInsetsObservation.unknown(generation, sequence);
        }
        int type = WindowInsets.Type.ime();
        boolean visible = insets != null && insets.isVisible(type);
        if (!visible) {
            return new DmImeInsetsObservation(true, false, 0, 0, 0, 0, generation, sequence);
        }
        Insets geometry = insets.getInsets(type);
        int left = clampPairStart(geometry.left, width);
        int right = clampPairEnd(geometry.right, width - left);
        int top = clampPairStart(geometry.top, height);
        int bottom = clampPairEnd(geometry.bottom, height - top);
        return new DmImeInsetsObservation(true, true, left, top, right, bottom, generation, sequence);
    }

    private static int clampPairStart(int edge, int extent) {
        if (extent <= 0) return 0;
        return Math.min(Math.max(0, edge), extent);
    }

    private static int clampPairEnd(int edge, int remaining) {
        if (remaining <= 0) return 0;
        return Math.min(Math.max(0, edge), remaining);
    }
}
