package com.xiaoboluo.d2r;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

final class AndroidPortSettings {
    private AndroidPortSettings() {}
    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences("android_port_options_v1", Context.MODE_PRIVATE);
    }
    static boolean wide(Context context) { return preferences(context).getBoolean("wide_screen", true); }
    static boolean fps(Context context) { return preferences(context).getBoolean("show_fps", true); }
    static void show(Activity activity, Runnable launch) {
        int pad = (int) (24 * activity.getResources().getDisplayMetrics().density);
        LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.VERTICAL); page.setGravity(Gravity.CENTER);
        page.setPadding(pad, pad, pad, pad); page.setBackgroundColor(0xff151719);
        TextView title = new TextView(activity);
        title.setText("D2R Mobile · Turnip"); title.setTextColor(Color.WHITE); title.setTextSize(26);
        page.addView(title);
        RadioGroup modes = new RadioGroup(activity); modes.setOrientation(RadioGroup.HORIZONTAL);
        RadioButton original = new RadioButton(activity), wide = new RadioButton(activity);
        original.setId(View.generateViewId()); wide.setId(View.generateViewId());
        original.setText("16:9 · 1280×720"); wide.setText("手机宽屏 · 高度720");
        original.setTextColor(Color.WHITE); wide.setTextColor(Color.WHITE);
        modes.addView(original); modes.addView(wide);
        modes.check(wide(activity) ? wide.getId() : original.getId()); page.addView(modes);
        TextView explanation = new TextView(activity);
        explanation.setText("宽屏按手机比例原生渲染，不拉伸；增加的像素可能影响帧率。\n内部3D：100% · 驱动：Turnip");
        explanation.setTextColor(0xffc3c7cd); explanation.setGravity(Gravity.CENTER); page.addView(explanation);
        Switch fps = new Switch(activity);
        fps.setText("显示简易FPS（提交帧率）"); fps.setTextColor(Color.WHITE); fps.setChecked(fps(activity)); page.addView(fps);
        Button start = new Button(activity); start.setText("启动游戏");
        start.setOnClickListener(v -> {
            boolean saved = preferences(activity).edit().putBoolean("wide_screen", modes.getCheckedRadioButtonId() == wide.getId())
                .putBoolean("show_fps", fps.isChecked()).commit();
            if (!saved) { Toast.makeText(activity, "设置保存失败，请重试", Toast.LENGTH_LONG).show(); return; }
            launch.run();
        });
        page.addView(start); activity.setContentView(page);
    }
}
