package com.xiaoboluo.d2r.widget;
import com.xiaoboluo.d2r.R;
/** 最多三个英雄的宽卡片。 | Wide card for up to three heroes. */
public final class HeroWidget4x2Provider extends HeroWidgetProvider {
    @Override protected int layoutResource() { return R.layout.widget_4x2; }
    @Override protected int maxHeroes() { return 3; }
}
