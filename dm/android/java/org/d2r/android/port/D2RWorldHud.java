package org.d2r.android.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

final class D2RWorldHud {
    private final Context context;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF rectangle = new RectF();
    private final float[] point = new float[5];
    private final Map<String, Bitmap> art = new HashMap<>();
    private static final int GOLD = 0xffc7b377, RED = 0xffc83228;

    D2RWorldHud(Context context) {
        this.context = context;
        paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
    }

    void draw(Canvas canvas, float[] view, float[] h, String[] labels) {
        if (h == null || h.length < WorldHudState.VALUES || view[23] != 0 || view[89] != 0) return;
        int saved = canvas.save();
        canvas.clipRect(view[0], view[1], view[0] + view[2], view[1] + view[3]);
        if (h[0] != 0 && labels != null && labels.length >= 15) rift(canvas, view, h, labels);
        if (h[70] != 0 && labels != null && labels.length >= 16) {
            String title = labels[15].isEmpty() ? "MAP" : labels[15];
            float u = view[3] / 614f, width = (h[74] - h[72]) * view[2];
            float size = 16 * u, right = view[0] + h[74] * view[2] - 8 * u;
            float height = (h[75] - h[73]) * view[3], band = height / 4;
            float minimum = 30 * view[3] / view[5];
            if (band < minimum) band = Math.min(minimum, height * .4f);
            paint.setTextSize(size);
            float titleWidth = Math.min(paint.measureText(title), width);
            text(canvas, title, right - titleWidth, view[1] + h[73] * view[3] + band / 2 + size * .35f,
                 size, GOLD, width);
        }
        for (int i = 0; i < Math.min(12, (int)h[25]); i++) goal(canvas, view, h, i);
        canvas.restoreToCount(saved);
    }

    private void rift(Canvas canvas, float[] v, float[] h, String[] labels) {
        float u = v[3] / 614f, x = v[0] + 28 * u, y = v[1] + 150 * u, width = 250 * u;
        paint.setStyle(Paint.Style.FILL); paint.setColor(0xaa000000);
        canvas.drawRect(x, y, x + width, y + 78 * u, paint);
        paint.setColor(0xe078643c); paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(Math.max(1, u));
        canvas.drawRect(x, y, x + width, y + 78 * u, paint);
        text(canvas, labels[0], x + 10 * u, y + 23 * u, 17 * u, h[5] != 0 ? RED : GOLD, 40 * u);
        text(canvas, WorldHudState.title(h, labels), x + 54 * u, y + 23 * u, 14 * u, 0xffd7d2c8, 125 * u);
        int seconds = Math.max(0, (int)h[3]);
        String timer = String.format(Locale.ROOT, "%d:%02d", seconds / 60, seconds % 60);
        paint.setTextSize(17 * u);
        float timerWidth = paint.measureText(timer);
        text(canvas, timer, x + width - 10 * u - timerWidth, y + 23 * u, 17 * u,
             seconds <= 60 && h[4] == 0 ? RED : GOLD, 80 * u);
        text(canvas, WorldHudState.goal(h, labels), x + 10 * u, y + 45 * u, 12 * u, 0xffb0aba1, width - 20 * u);
        float left = x + 11 * u, top = y + 55 * u, barWidth = width - 22 * u, barHeight = 10 * u;
        paint.setColor(0xff19140e); canvas.drawRect(left, top, left + barWidth, top + barHeight, paint);
        paint.setColor(h[5] != 0 ? RED : 0xffb38941);
        float progress = h[4] != 0 ? 1 : Math.max(0, Math.min(1, h[2]));
        canvas.drawRect(left + u, top + u, left + u + (barWidth - 2 * u) * progress, top + barHeight - u, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(Math.max(1, u)); paint.setColor(0xff5f4e30);
        canvas.drawRect(left, top, left + barWidth, top + barHeight, paint);
        for (int i = 1; i < 10; i++) {
            float tick = left + barWidth * i / 10;
            canvas.drawLine(tick, top, tick, top + barHeight, paint);
        }
        paint.setStyle(Paint.Style.FILL);
    }

    private void text(Canvas canvas, String text, float x, float y, float size, int color, float maxWidth) {
        paint.setStyle(Paint.Style.FILL); paint.setTextSize(size); paint.setTextAlign(Paint.Align.LEFT);
        float width = paint.measureText(text);
        if (width > maxWidth) paint.setTextSize(size * maxWidth / width);
        paint.setColor(Color.BLACK); canvas.drawText(text, x + 1, y + 1, paint);
        paint.setColor(color); canvas.drawText(text, x, y, paint);
    }

    private Bitmap image(String name) {
        if (art.containsKey(name)) return art.get(name);
        Bitmap bitmap = null;
        try (InputStream stream = context.getAssets().open("mapgoal/" + name + ".png")) {
            BitmapFactory.Options options = new BitmapFactory.Options(); options.inScaled = false;
            bitmap = BitmapFactory.decodeStream(stream, null, options);
        } catch (IOException absent) { }
        art.put(name, bitmap);
        return bitmap;
    }

    private void goal(Canvas canvas, float[] v, float[] h, int index) {
        if (!WorldHudState.project(v, h, index, point)) return;
        int id = (int)h[34 + index * 3];
        boolean symbol = id >= 64 && id < 71, inside = point[2] != 0;
        if (!symbol && (id < 0 || id > 30)) return;
        Bitmap label = null, mark = null;
        if (symbol && inside) mark = image("sym_" + (id - 64));
        else {
            int name = symbol ? 0 : id;
            label = image("label_" + name + WorldHudState.labelSuffix((int)h[30], h[31] != 0));
            if (label == null) label = image("label_" + name);
            if (inside) mark = image("cross");
        }
        float lh = v[3] * .0236f, crossWidth = lh * 1.4f, crossHeight = crossWidth * 18 / 34;
        if (label != null) {
            float width = Math.min(point[4] - point[3], lh * label.getWidth() / label.getHeight());
            float x = Math.max(point[3] + width / 2, Math.min(point[4] - width / 2, point[0]));
            float top = mark != null ? point[1] - crossHeight / 2 - 2 - lh : point[1] - lh / 2;
            bitmap(canvas, label, x, top + lh / 2, width, lh);
        }
        if (mark != null) {
            float height = symbol ? v[3] * .085f * (h[16] == 1 ? .6f : 1) : crossHeight;
            float width = symbol ? height * mark.getWidth() / mark.getHeight() : crossWidth;
            bitmap(canvas, mark, point[0], point[1], width, height);
        }
    }

    private void bitmap(Canvas canvas, Bitmap bitmap, float x, float y, float width, float height) {
        rectangle.set(x - width / 2, y - height / 2, x + width / 2, y + height / 2);
        paint.setColor(Color.WHITE); paint.setStyle(Paint.Style.FILL);
        canvas.drawBitmap(bitmap, null, rectangle, paint);
    }
}
