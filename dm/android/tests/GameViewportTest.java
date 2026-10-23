package com.xiaoboluo.d2r;

public final class GameViewportTest {
    public static void main(String[] args) {
        check(3200, 1440, 2560, 1440);
        check(1920, 1080, 1920, 1080);
        check(1280, 720, 1280, 720);
        check(1080, 1920, 1080, 607);
        try { GameViewport.fit(0, 720); throw new AssertionError("accepted empty viewport"); }
        catch (IllegalArgumentException expected) {}
        System.out.println("GameViewport OK: wide, 16:9, portrait, invalid");
    }
    private static void check(int w, int h, int expectedW, int expectedH) {
        int[] actual = GameViewport.fit(w, h);
        if (actual[0] != expectedW || actual[1] != expectedH) throw new AssertionError("aspect fit");
    }
}
