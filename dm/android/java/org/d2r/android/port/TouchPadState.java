// 多指虚拟手柄状态；不依赖 Android，取消某个窗口不会释放另一只手。
// Multitouch virtual pad state; Android independent, and cancelling one window leaves the other hand held.
package org.d2r.android.port;

import java.util.Arrays;

final class TouchPadState {
    // 与 dm_input.h 的 nn::hid 按钮位一致，摇杆上方向为正。
    // Match the nn::hid button bits in dm_input.h; the positive stick Y direction is up.
    static final long A = 1L, B = 1L << 1, X = 1L << 2, Y = 1L << 3;
    static final long LS = 1L << 4, RS = 1L << 5, L = 1L << 6, R = 1L << 7;
    static final long ZL = 1L << 8, ZR = 1L << 9, PLUS = 1L << 10, MINUS = 1L << 11;
    static final long LEFT = 1L << 12, UP = 1L << 13, RIGHT = 1L << 14, DOWN = 1L << 15;
    static final int STICK_MAX = 32767;
    private static final float DEAD_ZONE = 0.15f;
    private final int[] sources = new int[32];
    private final long[] held = new long[32];
    private int stickPointer = -1;
    private int leftX, leftY;

    TouchPadState() { clear(); }

    private boolean valid(int pointer) { return pointer >= 0 && pointer < sources.length; }
    private boolean owns(int pointer, int source) { return valid(pointer) && sources[pointer] == source; }

    void pressButton(int pointer, int source, long buttons) {
        if (!valid(pointer) || source < 0 || sources[pointer] >= 0) return;
        sources[pointer] = source;
        held[pointer] = buttons;
    }

    void moveButton(int pointer, int source, long buttons) {
        if (owns(pointer, source) && pointer != stickPointer) held[pointer] = buttons;
    }

    void pressStick(int pointer, int source, float x, float y) {
        if (!valid(pointer) || source < 0 || sources[pointer] >= 0 || stickPointer >= 0) return;
        sources[pointer] = source;
        stickPointer = pointer;
        moveStick(pointer, source, x, y);
    }

    void moveStick(int pointer, int source, float x, float y) {
        if (pointer != stickPointer || !owns(pointer, source)) return;
        if (!Float.isFinite(x) || !Float.isFinite(y)) { leftX = leftY = 0; return; }
        float radius = (float)Math.hypot(x, y);
        if (radius <= DEAD_ZONE) { leftX = leftY = 0; return; }
        float strength = (Math.min(1, radius) - DEAD_ZONE) / (1 - DEAD_ZONE);
        leftX = Math.round(x / radius * strength * STICK_MAX);
        leftY = Math.round(y / radius * strength * STICK_MAX);
    }

    void release(int pointer, int source) {
        if (!owns(pointer, source)) return;
        sources[pointer] = -1;
        held[pointer] = 0;
        if (stickPointer == pointer) { stickPointer = -1; leftX = leftY = 0; }
    }

    void cancelSource(int source) {
        for (int i = 0; i < sources.length; i++) if (sources[i] == source) release(i, source);
    }

    void clear() {
        Arrays.fill(sources, -1);
        Arrays.fill(held, 0);
        stickPointer = -1;
        leftX = leftY = 0;
    }

    long buttons() { long value = 0; for (long button : held) value |= button; return value; }
    int leftX() { return leftX; }
    int leftY() { return leftY; }
    int stickPointer() { return stickPointer; }
}
