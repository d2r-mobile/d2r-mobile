package com.xiaoboluo.d2r.widget;
import com.xiaoboluo.d2r.R;
/** 最近英雄的小尺寸卡片。 | Compact card for the most recent hero. */
public final class HeroWidget2x2Provider extends HeroWidgetProvider {
    @Override protected int layoutResource() { return R.layout.widget_2x2; }
    @Override protected int maxHeroes() { return 1; }
}
