package com.xiaoboluo.d2r;
final class DmActivity {
    static long buttons;
    static int lx, ly, glyph;
    static void nativePad(long b,int x,int y,int rx,int ry){buttons=b;lx=x;ly=y;}
    static void nativePadMode(int mode){}
    static void nativePadGlyph(int value){glyph=value;}
    static void onPadMode(boolean value){}
}
