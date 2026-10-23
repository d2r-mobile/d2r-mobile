package org.d2r.android.port;

import android.app.Activity;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.View;

public final class WorldHudVisualActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setSystemUiVisibility(5894);
        final String scenario = getIntent().getStringExtra("scenario");
        final String profile = scenario == null ? "progress" : scenario;
        Context context = profile.equals("missing") ? new ContextWrapper(this) {
            @Override public AssetManager getAssets() { return Resources.getSystem().getAssets(); }
        } : this;
        D2RWorldHud renderer = new D2RWorldHud(context);
        setContentView(new View(this) {
            final Paint paint = new Paint();
            final float[] v = new float[96], h = new float[80];
            final String[] labels = {"Rift","Level %d","Level %d complete","Level %d overtime","Guardian",
                "Next: Level %d","Highest level cleared","Overtime: drops, no clear","Guardians %d/%d (%d/%d)",
                "Slay the Guardians (%d/%d)","Overtime: slay it for drops","Overtime: slay %d/%d for drops",
                "Next: same level","Slay the Rift Guardian","Slay the monsters %d%%"};
            @Override protected void onDraw(Canvas canvas) {
                canvas.drawColor(0xff12191c);
                v[2] = getWidth(); v[3] = getHeight();
                if (profile.equals("letterbox")) {
                    v[2] = Math.min(getWidth(), getHeight() * 16f / 9);
                    v[0] = (getWidth() - v[2]) / 2;
                }
                paint.setColor(0xff293126);
                canvas.drawRect(v[0], 0, v[0]+v[2], v[3], paint);
                paint.setColor(0xff56604f);
                for (int x=0; x<getWidth(); x+=100) canvas.drawLine(x, 0, x, getHeight(), paint);
                for (int y=0; y<getHeight(); y+=100) canvas.drawLine(0, y, getWidth(), y, paint);
                h[0]=1; h[1]=99; h[2]=.72f; h[3]=373; h[7]=100;
                h[16]=2; h[17]=1; h[18]=1280; h[25]=2; h[28]=.5f; h[29]=.5f;
                h[30]=0; h[32]=30; h[33]=5; h[34]=65; h[35]=10000; h[36]=-300; h[37]=25;
                if (profile.equals("overtime")) {
                    h[3]=-2; h[5]=1; h[8]=1; h[9]=5; h[10]=1; h[11]=2;
                }
                if (profile.equals("done")) { h[1]=100; h[4]=1; h[6]=1; }
                if (profile.equals("minimap")) {
                    h[16]=1; h[20]=1; h[21]=.76f; h[22]=.04f; h[23]=.96f; h[24]=.3f;
                }
                if (profile.equals("navigation")) v[23]=1;
                renderer.draw(canvas, v, h, labels);
                paint.setColor(0xffeee7d5); paint.setTextSize(28);
                canvas.drawText("OWNED HUD FIXTURE / NO GAME STATE: " + profile, v[0]+30, 45, paint);
                paint.setColor(0xff6d3f3f); paint.setStyle(Paint.Style.STROKE);
                canvas.drawRect(v[0]+v[2]*.72f, v[3]*.45f, v[0]+v[2], v[3], paint);
                paint.setStyle(Paint.Style.FILL);
            }
        });
    }
}
