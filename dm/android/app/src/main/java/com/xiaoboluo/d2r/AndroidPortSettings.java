package com.xiaoboluo.d2r;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ScrollView;

final class AndroidPortSettings {
    private AndroidPortSettings() {}
    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences("android_port_options_v1", Context.MODE_PRIVATE);
    }
    static boolean wide(Context context) { return preferences(context).getBoolean("wide_screen", true); }
    static boolean fps(Context context) { return preferences(context).getBoolean("show_fps", true); }
    static boolean turnip(Context context) { return preferences(context).getBoolean("use_turnip", true); }
    static String driverLabel(Context context) { return turnip(context) ? "Turnip" : "系统Vulkan"; }
    private static final int GOLD = 0xffe8c078, TEXT = 0xffe8e0d0, MUTED = 0xffa2988c;
    private static int dp(Context c, int value) { return (int) (value * c.getResources().getDisplayMetrics().density); }
    private static TextView text(Activity a, String value, int size, int color) {
        TextView t = new TextView(a); t.setText(value); t.setTextSize(size); t.setTextColor(color); return t;
    }
    private static LinearLayout card(Activity a, String title) {
        LinearLayout card = new LinearLayout(a); card.setOrientation(LinearLayout.VERTICAL);
        int p = dp(a, 16); card.setPadding(p, p, p, p);
        GradientDrawable bg = new GradientDrawable(); bg.setColor(0xff18191c);
        bg.setCornerRadius(dp(a, 14)); bg.setStroke(dp(a, 1), 0xff4f4537); card.setBackground(bg);
        TextView heading = text(a, title, 15, GOLD); heading.setTypeface(null, Typeface.BOLD); card.addView(heading);
        return card;
    }
    private static RadioButton option(Activity a, RadioGroup group, String label) {
        RadioButton button = new RadioButton(a); button.setId(View.generateViewId()); button.setText(label);
        button.setTextColor(TEXT); button.setTextSize(14); button.setMinHeight(dp(a, 48));
        button.setButtonTintList(ColorStateList.valueOf(GOLD)); group.addView(button); return button;
    }
    static void show(Activity activity, Runnable launch) {
        int pad = dp(activity, 20);
        LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.HORIZONTAL); page.setGravity(Gravity.CENTER_VERTICAL);
        page.setPadding(pad, pad, pad, pad); page.setBackgroundColor(0xff101113);
        LinearLayout left = card(activity, "D2R MOBILE"); left.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text(activity, "暗黑破坏神 II\n狱火重生", 27, TEXT);
        title.setPadding(0, dp(activity, 16), 0, dp(activity, 16)); left.addView(title);
        left.addView(text(activity, "上游核心 · Android\n显示与驱动设置", 13, MUTED));
        Button start = new Button(activity); start.setText("启动游戏"); start.setTextSize(18);
        start.setTextColor(0xff15110b); start.setBackgroundTintList(ColorStateList.valueOf(GOLD));
        LinearLayout.LayoutParams launchLayout = new LinearLayout.LayoutParams(-1, dp(activity, 56));
        launchLayout.topMargin = dp(activity, 24); left.addView(start, launchLayout);
        page.addView(left, new LinearLayout.LayoutParams(0, -1, 1));
        ScrollView scroll = new ScrollView(activity); scroll.setFillViewport(true);
        LinearLayout.LayoutParams scrollLayout = new LinearLayout.LayoutParams(0, -1, 1.6f);
        scrollLayout.leftMargin = pad; page.addView(scroll, scrollLayout);
        LinearLayout settings = card(activity, "启动设置"); scroll.addView(settings);
        settings.addView(text(activity, "显示比例", 14, MUTED));
        RadioGroup modes = new RadioGroup(activity);
        RadioButton original = option(activity, modes, "16:9 · 1280×720");
        RadioButton wide = option(activity, modes, "手机宽屏 · 高度720");
        modes.check(wide(activity) ? wide.getId() : original.getId()); settings.addView(modes);
        settings.addView(text(activity, "宽屏按手机比例渲染，不拉伸 · 内部3D 100%", 12, MUTED));
        TextView driverTitle = text(activity, "显示驱动", 14, MUTED);
        driverTitle.setPadding(0, dp(activity, 16), 0, 0); settings.addView(driverTitle);
        RadioGroup drivers = new RadioGroup(activity);
        RadioButton turnip = option(activity, drivers, "内置 Turnip（推荐）");
        RadioButton system = option(activity, drivers, "系统高通 Vulkan");
        drivers.check(turnip(activity) ? turnip.getId() : system.getId()); settings.addView(drivers);
        settings.addView(text(activity, "驱动在下次启动游戏时生效；系统驱动可能出现黑块。", 12, MUTED));
        Switch fps = new Switch(activity);
        fps.setText("显示简易FPS（提交帧率）"); fps.setTextSize(14); fps.setTextColor(TEXT);
        fps.setMinHeight(dp(activity, 56)); fps.setChecked(fps(activity)); settings.addView(fps);
        start.setOnClickListener(v -> {
            boolean saved = preferences(activity).edit().putBoolean("wide_screen", modes.getCheckedRadioButtonId() == wide.getId())
                .putBoolean("show_fps", fps.isChecked()).putBoolean("use_turnip", drivers.getCheckedRadioButtonId() == turnip.getId()).commit();
            if (!saved) { Toast.makeText(activity, "设置保存失败，请重试", Toast.LENGTH_LONG).show(); return; }
            launch.run();
        });
        activity.setContentView(page);
    }
}
