package org.d2r.android.port;

public final class TouchGlyphTest {
    private static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }
    private static int[] atlasInfo() {
        int[] info = new int[TouchGlyph.ATLAS_COUNT * 5];
        info[0] = 408; info[1] = 65; info[2] = 65; info[3] = 68; info[4] = 60;
        info[5] = 2720; info[6] = 65; info[7] = 65; info[8] = 68; info[9] = 40;
        return info;
    }
    public static void main(String[] args) {
        int[] info = atlasInfo();
        int[] rect = new int[4];
        check(TouchGlyph.frameRect(info, 0, 2, rect), "normal frame rect accepted");
        check(rect[0] == 136 && rect[1] == 0 && rect[2] == 65 && rect[3] == 65,
              "atlas frame x/width/height");
        check(TouchGlyph.frameRect(info, 1, 39, rect), "secondary atlas frame accepted");
        check(!TouchGlyph.frameRect(info, 0, 60, rect), "frame outside count rejected");
        check(!TouchGlyph.frameRect(info, 9, 0, rect), "atlas outside range rejected");
        check(!TouchGlyph.frameRect(new int[45], 0, 0, rect), "missing atlas rejected");
        check(!TouchGlyph.frameRect(null, 0, 0, rect), "null atlas info rejected");
        check(TouchGlyph.assetName(0).equals("touch_art/dm_native_skill_0.png"),
              "stable asset contract");
        check(TouchGlyph.assetName(9) == null, "out-of-range asset has no name");
        check(TouchGlyph.button(0) == 1 && TouchGlyph.button(1) == 2 &&
              TouchGlyph.button(3) == 8 && TouchGlyph.button(4) == 128,
              "original target button masks");
        check(TouchGlyph.validAtlasInfo(info), "valid synthetic atlas metadata");
        check(!TouchGlyph.validAtlasInfo(new int[45]), "default no-assets metadata");
        System.out.println("PASS: skill glyph atlas frame, asset contract, default-unknown metadata and button masks");
    }
}
