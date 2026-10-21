package org.d2r.android.port;

final class TouchSkillSilhouette {
    private TouchSkillSilhouette() {}

    // Upstream gen_hud_v4.py mask, then DmTouchControlArt.h silver saturation treatment.
    static void apply(int[] pixels, int width, int height) {
        if (pixels == null || width <= 0 || height <= 0 || (long)width * height != pixels.length)
            throw new IllegalArgumentException("Invalid skill frame dimensions");
        double radiusSquared = width * .46 * width * .46;
        for (int y = 0; y < height; y++) {
            double dy = y - height / 2.0;
            for (int x = 0; x < width; x++) {
                int index = y * width + x, color = pixels[index];
                double dx = x - width / 2.0;
                if (dx * dx + dy * dy >= radiusSquared) { pixels[index] = 0; continue; }
                int red = (color >>> 16) & 255, green = (color >>> 8) & 255, blue = color & 255;
                double maximum = Math.max(red, Math.max(green, blue)) / 255.0;
                double minimum = Math.min(red, Math.min(green, blue)) / 255.0;
                double saturation = (maximum - minimum) / Math.max(maximum, .000001);
                double brightnessMask = Math.max(0, Math.min(1, (maximum - .42) / .18));
                double neutralMask = Math.max(0, Math.min(1, (.32 - saturation) / .12));
                int alpha = (int)((color >>> 24) * brightnessMask * neutralMask);
                int gray = (int)Math.round(red * .30 + green * .59 + blue * .11);
                pixels[index] = alpha == 0 ? 0 : (alpha << 24) | (gray << 16) | (gray << 8) | gray;
            }
        }
    }
}
