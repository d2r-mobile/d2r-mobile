// 小块触控窗口叠加在原生 Vulkan 窗口上，窗口之外仍交给原游戏。
// Small touch windows overlay the native Vulkan window; touches outside them remain with the game.
package org.d2r.android.port;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.drawable.ColorDrawable;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.util.Log;
import android.view.WindowInsets;
import android.widget.PopupWindow;
import java.util.ArrayList;

final class TouchControls {
    interface Listener { void onPad(long buttons, int leftX, int leftY); }
    private final Activity activity;
    private final View decor;
    private final Listener listener;
    private final TouchPadState state = new TouchPadState();
    private final ArrayList<ControlView> groups = new ArrayList<>();
    private final float density;
    private final View.OnLayoutChangeListener layoutListener;
    private boolean visible, destroyed;
    private int previousWidth, previousHeight;
    private long publishedButtons;
    private int publishedX, publishedY;

    TouchControls(Activity activity, Listener listener) {
        this.activity = activity;
        this.decor = activity.getWindow().getDecorView();
        this.listener = listener;
        density = activity.getResources().getDisplayMetrics().density;
        groups.add(new ControlView(0, true, 168, 168));
        groups.add(new ControlView(1, false, 184, 184,
            new Key(TouchPadState.X, "X", 92, 32, 28, 0xff699fc1),
            new Key(TouchPadState.Y, "Y", 32, 92, 28, 0xffc5b471),
            new Key(TouchPadState.A, "A", 152, 92, 28, 0xff91b59e),
            new Key(TouchPadState.B, "B", 92, 152, 28, 0xffc18d89)));
        groups.add(new ControlView(2, false, 136, 56,
            new Key(TouchPadState.ZL, "ZL", 30, 28, 26, 0xffd5cfc1),
            new Key(TouchPadState.L, "L", 106, 28, 26, 0xffd5cfc1)));
        groups.add(new ControlView(3, false, 136, 56,
            new Key(TouchPadState.R, "R", 30, 28, 26, 0xffd5cfc1),
            new Key(TouchPadState.ZR, "ZR", 106, 28, 26, 0xffd5cfc1)));
        groups.add(new ControlView(4, false, 224, 56,
            new Key(TouchPadState.LEFT, "Left", 28, 28, 24, 0xffd5cfc1),
            new Key(TouchPadState.UP, "Up", 84, 28, 24, 0xffd5cfc1),
            new Key(TouchPadState.DOWN, "Down", 140, 28, 24, 0xffd5cfc1),
            new Key(TouchPadState.RIGHT, "Right", 196, 28, 24, 0xffd5cfc1)));
        groups.add(new ControlView(5, false, 224, 56,
            new Key(TouchPadState.MINUS, "Minus", 28, 28, 24, 0xffd5cfc1),
            new Key(TouchPadState.PLUS, "Plus", 84, 28, 24, 0xffd5cfc1),
            new Key(TouchPadState.LS, "LS", 140, 28, 24, 0xffd5cfc1),
            new Key(TouchPadState.RS, "RS", 196, 28, 24, 0xffd5cfc1)));
        layoutListener = (v, l, t, r, b, oldL, oldT, oldR, oldB) -> layout();
        decor.addOnLayoutChangeListener(layoutListener);
    }

    private String text(String key) {
        int resource = activity.getResources().getIdentifier(key, "string", activity.getPackageName());
        if (resource == 0) throw new IllegalStateException("Missing touch-control string: " + key);
        return activity.getString(resource);
    }

    void setVisible(boolean value) {
        Log.i("D2RAndroid", "[touch] setVisible(" + value + ") destroyed=" + destroyed +
            " attached=" + decor.isAttachedToWindow());
        if (destroyed) return;
        visible = value;
        if (value) decor.post(this::layout);
        else {
            releaseAll();
            for (ControlView group : groups) group.popup.dismiss();
        }
    }

    void destroy() {
        if (destroyed) return;
        setVisible(false);
        destroyed = true;
        decor.removeOnLayoutChangeListener(layoutListener);
    }

    private void releaseAll() {
        state.clear();
        publish();
    }

    private void publish() {
        long buttons = state.buttons();
        int x = state.leftX(), y = state.leftY();
        if (buttons != publishedButtons || x != publishedX || y != publishedY) {
            publishedButtons = buttons; publishedX = x; publishedY = y;
            listener.onPad(buttons, x, y);
        }
        for (ControlView group : groups) group.invalidateIfChanged();
    }

    private void layout() {
        if (!visible || destroyed || activity.isFinishing() || activity.isDestroyed() || !decor.isAttachedToWindow()) {
            Log.i("D2RAndroid", "[touch] layout skipped visible=" + visible + " destroyed=" + destroyed +
                " finishing=" + activity.isFinishing() + " gone=" + activity.isDestroyed() +
                " attached=" + decor.isAttachedToWindow());
            return;
        }
        int width = decor.getWidth(), height = decor.getHeight();
        if (width <= 0 || height <= 0) {
            Log.i("D2RAndroid", "[touch] layout zero decor=" + width + "x" + height);
            return;
        }
        if (width != previousWidth || height != previousHeight) releaseAll();
        previousWidth = width; previousHeight = height;
        int safeLeft = 0, safeRight = 0, safeTop = 0, safeBottom = 0;
        WindowInsets insets = decor.getRootWindowInsets();
        if (insets != null) {
            safeLeft = insets.getSystemWindowInsetLeft();
            safeRight = insets.getSystemWindowInsetRight();
            safeTop = insets.getSystemWindowInsetTop();
            safeBottom = insets.getSystemWindowInsetBottom();
            Insets gestures = insets.getSystemGestureInsets();
            safeLeft = Math.max(safeLeft, gestures.left);
            safeRight = Math.max(safeRight, gestures.right);
            safeBottom = Math.max(safeBottom, gestures.bottom);
            DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout != null) {
                safeLeft = Math.max(safeLeft, cutout.getSafeInsetLeft());
                safeRight = Math.max(safeRight, cutout.getSafeInsetRight());
                safeTop = Math.max(safeTop, cutout.getSafeInsetTop());
                safeBottom = Math.max(safeBottom, cutout.getSafeInsetBottom());
            }
        }
        float scale = Math.min(1, Math.min((width - safeLeft - safeRight) / (640 * density),
            (height - safeTop - safeBottom) / (360 * density)));
        // 高密度手机横屏较矮，限制最大按键组占高；平板原有尺寸保持可用。
        // Cap control height on dense phones while retaining existing tablet sizing.
        scale = Math.min(scale, (height - safeTop - safeBottom) * .36f / (184 * density));
        for (ControlView group : groups) group.unit = density * scale;
        float margin = 16 * density * scale;
        // 与原生前端相同的 16:9 居中画面；短按键优先放到上下黑边。
        // Use the native frontend's centered 16:9 viewport; place the compact keys in letterbox space first.
        float gameWidth = width, gameHeight = width * 9f / 16f;
        if (gameHeight > height) { gameHeight = height; gameWidth = height * 16f / 9f; }
        float gameLeft = (width - gameWidth) / 2, gameTop = (height - gameHeight) / 2;
        float gameBottom = gameTop + gameHeight;
        ControlView stick = groups.get(0), face = groups.get(1);
        float low = height - safeBottom - margin;
        float baseBottom = low - gameBottom >= Math.max(stick.pixelHeight(), face.pixelHeight()) + margin ? low :
            Math.min(low, gameBottom - gameHeight * .17f);
        // 宽屏手机利用两侧留白，按安全屏幕边缘布局。
        // Use ultrawide side space by anchoring controls to safe screen edges.
        stick.place(safeLeft + margin, baseBottom - stick.pixelHeight());
        face.place(width - safeRight - margin - face.pixelWidth(),
            baseBottom - face.pixelHeight());
        ControlView leftShoulder = groups.get(2), rightShoulder = groups.get(3);
        leftShoulder.place(stick.windowX + (stick.pixelWidth() - leftShoulder.pixelWidth()) / 2,
            Math.max(safeTop + margin, stick.windowY - margin - leftShoulder.pixelHeight()));
        rightShoulder.place(face.windowX + (face.pixelWidth() - rightShoulder.pixelWidth()) / 2,
            Math.max(safeTop + margin, face.windowY - margin - rightShoulder.pixelHeight()));
        ControlView dpad = groups.get(4), menu = groups.get(5);
        float compactY = low - gameBottom >= menu.pixelHeight() ?
            gameBottom + (low - gameBottom - menu.pixelHeight()) / 2 :
            (gameTop - safeTop >= menu.pixelHeight() + margin ? safeTop + margin : gameTop + margin);
        // 两条小按键排留出中央游戏界面，也不盖住血球、蓝球或腰带。
        // Two compact rows leave the central game UI, life/mana orbs and belt unobstructed.
        float combined = dpad.pixelWidth() + menu.pixelWidth() + margin * 2;
        float compactX = (width - combined) / 2;
        dpad.place(compactX, compactY);
        menu.place(compactX + dpad.pixelWidth() + margin * 2, compactY);
        Log.i("D2RAndroid", "[touch] layout decor=" + width + "x" + height + " scale=" + scale +
            " stick=" + Math.round(stick.windowX) + "," + Math.round(stick.windowY) +
            " stickPx=" + stick.pixelWidth() + "x" + stick.pixelHeight() +
            " face=" + Math.round(face.windowX) + "," + Math.round(face.windowY));
    }

    private static final class Key {
        final long button;
        final String name;
        final float x, y, radius;
        final int color;
        String label;
        Key(long button, String name, float x, float y, float radius, int color) {
            this.button = button; this.name = name; this.x = x; this.y = y; this.radius = radius; this.color = color;
        }
    }

    private final class ControlView extends View {
        final int source;
        final boolean stick;
        final int logicalWidth, logicalHeight;
        final Key[] keys;
        final PopupWindow popup;
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float unit, windowX, windowY;
        final long keyMask;
        boolean drawnInitialized, drawnActive;
        int drawnX, drawnY;
        long drawnPressed;

        ControlView(int source, boolean stick, int width, int height, Key... keys) {
            super(activity);
            this.source = source; this.stick = stick;
            this.logicalWidth = width; this.logicalHeight = height; this.keys = keys;
            unit = density;
            setFocusable(false);
            setClickable(true);
            setContentDescription(text(stick ? "dmAndroidTouchMove" : "dmAndroidTouchButtons"));
            long mask = 0;
            for (Key key : keys) { key.label = text("dmAndroidPad" + key.name); mask |= key.button; }
            this.keyMask = mask;
            popup = new PopupWindow(this, pixelWidth(), pixelHeight(), false);
            popup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            popup.setTouchModal(false);
            popup.setSplitTouchEnabled(true);
            popup.setOutsideTouchable(false);
            popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
            popup.setElevation(0);
            popup.setOnDismissListener(() -> { state.cancelSource(source); publish(); });
        }

        void invalidateIfChanged() {
            if (stick) {
                boolean active = state.stickPointer() >= 0;
                int x = state.leftX(), y = state.leftY();
                if (!drawnInitialized || active != drawnActive || x != drawnX || y != drawnY) {
                    drawnInitialized = true; drawnActive = active; drawnX = x; drawnY = y;
                    invalidate();
                }
            } else {
                long pressed = state.buttons() & keyMask;
                if (!drawnInitialized || pressed != drawnPressed) {
                    drawnInitialized = true; drawnPressed = pressed;
                    invalidate();
                }
            }
        }

        int pixelWidth() { return Math.round(logicalWidth * unit); }
        int pixelHeight() { return Math.round(logicalHeight * unit); }

        void place(float x, float y) {
            windowX = x; windowY = y;
            int width = pixelWidth(), height = pixelHeight();
            if (popup.isShowing()) popup.update(Math.round(x), Math.round(y), width, height);
            else {
                popup.setWidth(width); popup.setHeight(height);
                popup.showAtLocation(decor, Gravity.TOP | Gravity.LEFT, Math.round(x), Math.round(y));
            }
            Log.i("D2RAndroid", "[touch] place " + (stick ? "stick" : "group" + source) + " at " +
                Math.round(x) + "," + Math.round(y) + " " + width + "x" + height + " showing=" + popup.isShowing());
            invalidate();
        }

        long buttonAt(float x, float y) {
            x /= unit; y /= unit;
            for (Key key : keys) if (Math.hypot(x - key.x, y - key.y) <= key.radius + 2) return key.button;
            return 0;
        }

        void pointerDown(MotionEvent event, int index) {
            int id = event.getPointerId(index);
            if (stick) {
                float x = event.getX(index) / unit - logicalWidth / 2f;
                float y = event.getY(index) / unit - logicalHeight / 2f;
                if (Math.hypot(x, y) <= 76) state.pressStick(id, source, x / 56, -y / 56);
            } else state.pressButton(id, source, buttonAt(event.getX(index), event.getY(index)));
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked(), index = event.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) pointerDown(event, index);
            else if (action == MotionEvent.ACTION_MOVE) {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int id = event.getPointerId(i);
                    if (stick) state.moveStick(id, source,
                        (event.getX(i) / unit - logicalWidth / 2f) / 56,
                        -(event.getY(i) / unit - logicalHeight / 2f) / 56);
                    else state.moveButton(id, source, buttonAt(event.getX(i), event.getY(i)));
                }
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                state.release(event.getPointerId(index), source);
            } else if (action == MotionEvent.ACTION_CANCEL) state.cancelSource(source);
            publish();
            // 控件里的整段手势归本窗口；抬起不会穿到原生窗口变成额外的 A。
            // This window owns the entire control gesture; its release never becomes an extra native-window A press.
            return true;
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            canvas.save(); canvas.scale(unit, unit);
            paint.setStrokeWidth(1.5f);
            if (stick) {
                float x = logicalWidth / 2f, y = logicalHeight / 2f;
                circle(canvas, x, y, 70, 0xffd5cfc1, state.stickPointer() >= 0);
                paint.setStyle(Paint.Style.STROKE); paint.setColor(0x54d5cfc1);
                canvas.drawCircle(x, y, 34, paint);
                paint.setStyle(Paint.Style.FILL); paint.setColor(state.stickPointer() >= 0 ? 0xbbd5cfc1 : 0x78d5cfc1);
                canvas.drawCircle(x + 48f * state.leftX() / TouchPadState.STICK_MAX,
                    y - 48f * state.leftY() / TouchPadState.STICK_MAX, 24, paint);
            } else for (Key key : keys) {
                circle(canvas, key.x, key.y, key.radius, key.color, (state.buttons() & key.button) != 0);
                paint.setStyle(Paint.Style.FILL); paint.setColor(0xe8eee7d8);
                paint.setTextAlign(Paint.Align.CENTER); paint.setTextSize(key.label.length() > 1 ? 17 : 22);
                Paint.FontMetrics metrics = paint.getFontMetrics();
                canvas.drawText(key.label, key.x, key.y - (metrics.ascent + metrics.descent) / 2, paint);
            }
            canvas.restore();
        }

        private void circle(Canvas canvas, float x, float y, float radius, int color, boolean pressed) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(pressed ? (color & 0xffffff) | 0x92000000 : 0x480b0c0d);
            canvas.drawCircle(x, y, radius, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setColor((color & 0xffffff) | (pressed ? 0xe0000000 : 0x88000000));
            canvas.drawCircle(x, y, radius, paint);
        }
    }
}
