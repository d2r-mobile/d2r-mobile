// 启动界面的全屏背景。优先用打进 APK 的 launcher-background.*（构建时从 dm/android/local/ 拷入，
// 该目录不进 git），其次是应用目录里的同名文件；都没有才用代码画的暗黑底图。
// Full-screen backdrop for the launcher. It prefers launcher-background.* packed into the APK (copied at build
// time from dm/android/local/, which is gitignored), then a same-named file in the app directory, and only
// falls back to a backdrop drawn in code.
package org.d2r.android.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.View;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;

final class LauncherBackgroundView extends View {
    private static final String[] ARTWORK_NAMES = {"launcher-background.jpg", "launcher-background.png"};

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private Bitmap artwork;
    private Bitmap prepared;      // 已经为当前屏幕尺寸处理好的那一张（缩过或原图）
    private int preparedForWidth, preparedForHeight;
    private boolean artworkChecked;

    LauncherBackgroundView(Context context) { super(context); }

    private void loadArtwork() {
        artworkChecked = true;
        for (String name : ARTWORK_NAMES) {
            try (InputStream stream = getContext().getAssets().open(name)) {
                Bitmap decoded = BitmapFactory.decodeStream(stream);
                if (decoded != null) { artwork = decoded; return; }
            } catch (IOException ignored) {
            }
        }
        File file = new File(getContext().getFilesDir(), ARTWORK_NAMES[0]);
        if (file.isFile()) artwork = BitmapFactory.decodeFile(file.getAbsolutePath());
        if (artwork == null) {
            file = new File(getContext().getFilesDir(), ARTWORK_NAMES[1]);
            if (file.isFile()) artwork = BitmapFactory.decodeFile(file.getAbsolutePath());
        }
    }

    // 素材明显小于屏幕时，先缩到很小再用双线性放大：放大本身就会柔化，读起来是氛围底图，
    // 而不是把低分辨率原图硬拉成马赛克。构图完全保留。
    // When the artwork is much smaller than the screen, shrink it first and let the upscale blur it: that reads
    // as an atmospheric backdrop instead of a pixelated photo, and the composition survives intact.
    private Bitmap prepare(int width, int height) {
        if (artwork == null || (prepared != null && preparedForWidth == width && preparedForHeight == height)) return prepared;
        int smallest = Math.min(width, height);
        if (Math.min(artwork.getWidth(), artwork.getHeight()) >= smallest / 3) {
            prepared = artwork;
        } else {
            int targetWidth = Math.max(24, smallest / 8);
            int targetHeight = Math.max(24, targetWidth * artwork.getHeight() / artwork.getWidth());
            prepared = Bitmap.createScaledBitmap(artwork, targetWidth, targetHeight, true);
        }
        preparedForWidth = width;
        preparedForHeight = height;
        return prepared;
    }

    @Override protected void onDraw(Canvas canvas) {
        if (!artworkChecked) loadArtwork();
        int width = getWidth(), height = getHeight();
        if (width <= 0 || height <= 0) return;

        Bitmap source = prepare(width, height);
        if (source != null) {
            // 居中裁切铺满：不留黑边也不拉伸变形。
            // Centre-crop to fill: no letterboxing and no stretching.
            float scale = Math.max((float) width / source.getWidth(), (float) height / source.getHeight());
            float scaledWidth = source.getWidth() * scale, scaledHeight = source.getHeight() * scale;
            paint.setShader(null);
            paint.setAlpha(255);
            paint.setFilterBitmap(true);
            canvas.drawBitmap(source, new Rect(0, 0, source.getWidth(), source.getHeight()),
                new RectF((width - scaledWidth) / 2f, (height - scaledHeight) / 2f,
                          (width + scaledWidth) / 2f, (height + scaledHeight) / 2f), paint);
            // 整体轻压暗，右边缘再压重一层——文字都排在这一侧，必须任何底图上都读得清。
            // A light darkening overall, heavier along the right edge where every label sits, so the text stays
            // legible on any artwork.
            paint.setShader(new LinearGradient(width * 0.45f, 0, width, 0,
                new int[]{0x55000000, 0xcc000000}, new float[]{0f, 1f}, Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, width, height, paint);
            paint.setShader(null);
            return;
        }

        // 没有素材时的兜底：石壁底色 + 两团火光 + 暗角，全部按比例定位，任何分辨率一致。
        // Fallback without artwork: stone base, two fires and a vignette, placed by ratio so any resolution matches.
        paint.setShader(new LinearGradient(0, 0, 0, height,
            new int[]{0xff171314, 0xff14100f, 0xff1d1310, 0xff0d0a09},
            new float[]{0f, 0.45f, 0.78f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, width, height, paint);
        drawGlow(canvas, width * 0.20f, height * 0.92f, width * 0.52f, 0x66ff8a3c);
        drawGlow(canvas, width * 0.78f, height * 0.98f, width * 0.34f, 0x3cff6a22);
        paint.setShader(new RadialGradient(width * 0.5f, height * 0.48f, Math.max(width, height) * 0.62f,
            new int[]{0x00000000, 0x00000000, 0xcc000000}, new float[]{0f, 0.55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, width, height, paint);
        paint.setShader(null);
    }

    private void drawGlow(Canvas canvas, float x, float y, float radius, int colour) {
        paint.setShader(new RadialGradient(x, y, radius, new int[]{colour, 0x00000000}, new float[]{0f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(x, y, radius, paint);
        paint.setShader(null);
    }
}
