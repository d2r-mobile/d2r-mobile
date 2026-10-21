package org.d2r.android.port;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.PopupWindow;

import java.io.IOException;
import java.io.InputStream;

/**
 * Draw-only visual layer for the actual D2R live touch core.
 *
 * This View never receives touches and never synthesizes input: the original
 * NativeActivity AInputQueue remains the only input path. Every coordinate is
 * read from the core layout snapshot published by frontend_activity.c, so the
 * circles match the same logical layout used by the gesture core. The game
 * provides the skill/HUD glyphs; where no Android glyph path exists we draw an
 * explicit "?" placeholder rather than inventing a skill icon or cooldown.
 */
final class D2RTouchOverlay extends View {
    private final D2RActivity activity;
    private final PopupWindow popup;
    private final View decor;
    private final float[] values = new float[96];
    private final long[] state = new long[4];
    private final int[] glyphs = new int[20];
    private final int[] atlasInfo = new int[45];
    private final Bitmap[] atlases = new Bitmap[TouchGlyph.ATLAS_COUNT];
    private final Bitmap[] glyphCache = new Bitmap[TouchGlyph.ATLAS_COUNT * 128];
    private final int[] glyphRect = new int[4];
    private final Rect src = new Rect();
    private final Rect dst = new Rect();
    private boolean atlasInfoLoaded;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final View.OnLayoutChangeListener layoutListener;
    private boolean visible, destroyed;

    D2RTouchOverlay(D2RActivity activity) {
        super(activity);
        this.activity = activity;
        this.decor = activity.getWindow().getDecorView();
        setWillNotDraw(false);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        fill.setColor(0xaa10161c);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(Math.max(1f, getResources().getDisplayMetrics().density * 1.5f));
        stroke.setColor(0xccd8c49a);
        label.setColor(0xfff1e8d8);
        label.setTextAlign(Paint.Align.CENTER);
        label.setTextSize(getResources().getDisplayMetrics().density * 11f);
        popup = new PopupWindow(this, android.view.ViewGroup.LayoutParams.MATCH_PARENT,
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, false);
        popup.setTouchable(false);
        popup.setFocusable(false);
        popup.setOutsideTouchable(false);
        popup.setClippingEnabled(false);
        popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        popup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        layoutListener = (v, l, t, r, b, oldL, oldT, oldR, oldB) -> {
            if (visible && !destroyed && (r - l) > 0 && (b - t) > 0) {
                popup.update(0, 0, r - l, b - t);
                invalidate();
            }
        };
        decor.addOnLayoutChangeListener(layoutListener);
    }

    void setVisible(boolean value) {
        if (destroyed) return;
        if (visible == value && (!value || popup.isShowing())) return;
        visible = value;
        if (value) {
            if (!decor.isAttachedToWindow()) {
                decor.post(() -> { if (!destroyed && visible) setVisible(true); });
                return;
            }
            if (!popup.isShowing()) popup.showAtLocation(decor, Gravity.TOP | Gravity.LEFT, 0, 0);
            invalidate();
        } else {
            if (popup.isShowing()) popup.dismiss();
        }
    }

    void refresh() {
        if (visible && !destroyed) invalidate();
    }

    void destroy() {
        if (destroyed) return;
        destroyed = true;
        visible = false;
        popup.dismiss();
        decor.removeOnLayoutChangeListener(layoutListener);
    }

    private float mx(float[] v, float x) { return TouchOverlayGeometry.x(v, x); }
    private float my(float[] v, float y) { return TouchOverlayGeometry.y(v, y); }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!visible || destroyed) return;
        int ok;
        try { ok = activity.readLiveTouchOverlay(values, state); }
        catch (UnsatisfiedLinkError error) { return; }
        if (ok == 0 || !TouchOverlayGeometry.valid(values)) return;
        if (!atlasInfoLoaded && activity.readLiveTouchAtlasInfo(atlasInfo) != 0) atlasInfoLoaded = true;
        if (activity.readLiveTouchGlyphSnapshot(glyphs) == 0) {
            for (int i = 0; i < glyphs.length; i++) glyphs[i] = i % 4 == 0 ? TouchGlyph.UNKNOWN : -1;
        }
        float[] v = values;
        if (TouchOverlayGeometry.frontend(v)) return; // original front-end pages have no combat controls
        int mask = (int)v[17];
        int skillSet = (int)v[15];
        int players = (int)v[18];
        boolean navigation = TouchOverlayGeometry.navigation(v);

        // Joystick: the base follows the actual floating origin while a core joystick finger is down.
        // The vertical sign matches DmTouchOverlay: dy = -ly / 32767 * radius.
        if (v[8] > 0 && !navigation) {
            float cx = mx(v, TouchOverlayGeometry.joyBaseX(v));
            float cy = my(v, TouchOverlayGeometry.joyBaseY(v));
            float r = v[8] * v[2] / v[4];
            circle(canvas, cx, cy, r, 0x66141b22, 0x88d8c49a);
            float kx = mx(v, TouchOverlayGeometry.joyKnobX(v, state[1]));
            float ky = my(v, TouchOverlayGeometry.joyKnobY(v, state[2]));
            circle(canvas, kx, ky, r * .32f, 0x99d8c49a, 0xccf1e8d8);
            text(canvas, "MOVE", cx, cy + r + label.getTextSize() * 1.2f);
        }

        // Main attack and the four skill slots are the real target centers. A bound skill uses the
        // original iOS native art mapping (dm_ta_current_set) when the local generated atlas is present;
        // empty slots draw a cross and unknown/missing assets remain an explicit "?" rather than a fake icon.
        for (int i = 0; i < 12; i++) {
            if ((mask & (1 << i)) == 0) continue;
            float cx = mx(v, v[24 + i * 3]);
            float cy = my(v, v[25 + i * 3]);
            float r = v[26 + i * 3] * v[2] / v[4];
            if (r <= 0) continue;
            int bg = (i == 10) ? 0x995a4430 : 0x77101820;
            circle(canvas, cx, cy, r, bg, i == 0 ? 0xeed8c49a : 0xaad8c49a);
            if (i <= 4) {
                if (!navigation && drawSkillGlyph(canvas, i, cx, cy, r, state[0])) continue;
                text(canvas, "?", cx, cy + label.getTextSize() * .36f);
                continue;
            }
            String s;
            if (i == 5) s = "TP";
            else if (i >= 6 && i <= 9) s = String.valueOf(i - 5);
            else if (i == 10) s = "MENU";
            else s = "MAP";
            if (i >= 6 && i <= 9) {
                int count = (int)v[19 + (i - 6)];
                if (count > 0) s += ":" + count;
            }
            text(canvas, s, cx, cy + label.getTextSize() * .36f);
        }

        // Skill group toggle is the core's swap target, not a reinvented layout.
        if (!navigation && v[11] > 0) {
            float cx = mx(v, v[9]), cy = my(v, v[10]), r = v[11] * v[2] / v[4];
            circle(canvas, cx, cy, r, 0x88101820, 0xccd8c49a);
            text(canvas, skillSet == 0 ? "I" : "II", cx, cy + label.getTextSize() * .36f);
        }

        // Difficulty target: host-reported player count; the eight choices are the core's pick cells.
        if (!navigation && v[14] > 0) {
            float cx = mx(v, v[12]), cy = my(v, v[13]), r = v[14] * v[2] / v[4];
            circle(canvas, cx, cy, r, 0x88101820, 0xccd8c49a);
            text(canvas, players > 0 ? "P" + players : "P?", cx, cy + label.getTextSize() * .36f);
            if ((int)v[16] != 0) {
                for (int i = 0; i < 8; i++) {
                    float px = mx(v, v[60 + i * 3]), py = my(v, v[61 + i * 3]);
                    float pr = v[62 + i * 3] * v[2] / v[4];
                    if (pr <= 0) continue;
                    circle(canvas, px, py, pr, players == i + 1 ? 0x99725436 : 0x88101820, 0xccd8c49a);
                    text(canvas, String.valueOf(i + 1), px, py + label.getTextSize() * .36f);
                }
            }
        }
    }

    private void drawPlus(Canvas canvas, float x, float y, float r) {
        stroke.setColor(0xccd8c49a);
        float a = r * .30f;
        canvas.drawLine(x - a, y, x + a, y, stroke);
        canvas.drawLine(x, y - a, x, y + a, stroke);
    }

    private Bitmap loadAtlas(int atlas) {
        if (atlas < 0 || atlas >= TouchGlyph.ATLAS_COUNT) return null;
        Bitmap cached = atlases[atlas];
        if (cached != null) return cached;
        String name = TouchGlyph.assetName(atlas);
        if (name == null || !atlasInfoLoaded) return null;
        InputStream in = null;
        try {
            in = activity.getAssets().open(name);
            cached = BitmapFactory.decodeStream(in);
            atlases[atlas] = cached;
        } catch (IOException | RuntimeException error) {
            cached = null;
        } finally {
            if (in != null) try { in.close(); } catch (IOException ignored) { }
        }
        return cached;
    }

    private Bitmap glyphBitmap(int atlas, int frame) {
        int key = atlas * 128 + frame;
        if (key < 0 || key >= glyphCache.length) return null;
        Bitmap cached = glyphCache[key];
        if (cached != null) return cached;
        Bitmap source = loadAtlas(atlas);
        if (source == null || frame < 0 || frame >= 128) return null;
        if (!TouchGlyph.frameRect(atlasInfo, atlas, frame, glyphRect)) return null;
        try {
            cached = Bitmap.createBitmap(source, glyphRect[0], glyphRect[1],
                                         glyphRect[2], glyphRect[3]);
            glyphCache[key] = cached;
        } catch (IllegalArgumentException | OutOfMemoryError error) {
            cached = null;
        }
        return cached;
    }

    private boolean drawSkillGlyph(Canvas canvas, int target, float cx, float cy,
                                   float r, long buttons) {
        int base = target * 4;
        if (base < 0 || base + 3 >= glyphs.length) return false;
        int status = glyphs[base];
        if (status == TouchGlyph.EMPTY) {
            drawPlus(canvas, cx, cy, r);
            return true;
        }
        if (status != TouchGlyph.GLYPH) return false;
        int atlas = glyphs[base + 1];
        int frame = (buttons & TouchGlyph.button(target)) != 0 ? glyphs[base + 3] : glyphs[base + 2];
        if (!TouchGlyph.validAtlasInfo(atlasInfo) ||
            !TouchGlyph.frameRect(atlasInfo, atlas, frame, glyphRect)) return false;
        Bitmap glyph = glyphBitmap(atlas, frame);
        if (glyph == null) return false;
        src.set(glyphRect[0], glyphRect[1], glyphRect[0] + glyphRect[2],
                glyphRect[1] + glyphRect[3]);
        float size = r * 1.28f;
        dst.set(Math.round(cx - size * .5f), Math.round(cy - size * .5f),
                Math.round(cx + size * .5f), Math.round(cy + size * .5f));
        canvas.drawBitmap(glyph, src, dst, null);
        return true;
    }

    private void circle(Canvas canvas, float x, float y, float r, int bg, int line) {
        fill.setColor(bg);
        stroke.setColor(line);
        canvas.drawCircle(x, y, Math.max(1f, r), fill);
        canvas.drawCircle(x, y, Math.max(1f, r), stroke);
    }

    private void text(Canvas canvas, String s, float x, float y) {
        canvas.drawText(s, x, y, label);
    }
}
