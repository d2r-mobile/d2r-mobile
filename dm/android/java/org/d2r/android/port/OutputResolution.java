package org.d2r.android.port;

final class OutputResolution {
    static String renderScale(int index) {
        return index == 0 ? "67" : index == 1 ? "89" : "100";
    }
    static String fullScreen(int width, int height, int targetHeight) {
        int longSide = Math.max(width, height), shortSide = Math.min(width, height);
        if (shortSide <= 0) throw new IllegalArgumentException("Invalid display size");
        int outputWidth = (int)(Math.round((double)longSide * targetHeight / shortSide / 4) * 4);
        return Math.min(4096, outputWidth) + "x" + targetHeight;
    }

    static String migrate(String old, boolean handheld) {
        if (old == null || old.isEmpty()) return null;
        String[] parts = old.split("x");
        if (parts.length != 2) return null;
        try {
            int w = Integer.parseInt(parts[0]), h = Integer.parseInt(parts[1]);
            if (w < 320 || w > 7680 || h < 240 || h > 4320) return null;
            return handheld ? ((w * 4 / 5 + 3) / 4 * 4) + "x" + (h * 4 / 5) : old;
        } catch (NumberFormatException error) { return null; }
    }
}
