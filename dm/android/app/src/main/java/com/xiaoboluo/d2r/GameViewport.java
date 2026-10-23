package com.xiaoboluo.d2r;

final class GameViewport {
    private GameViewport() {}
    static int[] fit(int width, int height) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException("empty viewport");
        int w = (int) Math.min(width, (long) height * 16 / 9);
        int h = (int) Math.min(height, (long) width * 9 / 16);
        return new int[] {w, h};
    }
}
