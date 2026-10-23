package com.xiaoboluo.d2r.widget;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.view.View;
import android.widget.RemoteViews;
import com.xiaoboluo.d2r.DmActivity;
import com.xiaoboluo.d2r.R;
import java.text.NumberFormat;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** 原生AppWidget与vivo元数据共用的数据层；不代表已通过平台审核。 | Shared native AppWidget data layer; metadata does not mean platform approval. */
public abstract class HeroWidgetProvider extends AppWidgetProvider {
    private static final String ACTION_REFRESH="com.xiaoboluo.d2r.widget.REFRESH";
    private static final ExecutorService IO=Executors.newSingleThreadExecutor();
    private static final int[] NAMES={R.id.widget_name_0,R.id.widget_name_1,R.id.widget_name_2};
    private static final int[] STATS={R.id.widget_stats_0,R.id.widget_stats_1,R.id.widget_stats_2};
    private static final int[] EXP={R.id.widget_exp_0,R.id.widget_exp_1,R.id.widget_exp_2};
    private static final int[] GOLD={R.id.widget_gold_0,R.id.widget_gold_1,R.id.widget_gold_2};
    private static final int[] COLUMNS={R.id.widget_hero_0,R.id.widget_hero_1,R.id.widget_hero_2};
    private static final int[] CLASS_NAMES={R.string.dmAndroidWidgetClassAmazon,R.string.dmAndroidWidgetClassSorceress,
        R.string.dmAndroidWidgetClassNecromancer,R.string.dmAndroidWidgetClassPaladin,R.string.dmAndroidWidgetClassBarbarian,
        R.string.dmAndroidWidgetClassDruid,R.string.dmAndroidWidgetClassAssassin,R.string.dmAndroidWidgetClassWarlock};
    protected abstract int layoutResource();
    protected abstract int maxHeroes();

    @Override public final void onUpdate(Context context,AppWidgetManager manager,int[] ids) { updateAsync(context,ids); }
    @Override public final void onReceive(Context context,Intent intent) {
        if (ACTION_REFRESH.equals(intent.getAction())) {
            int id=intent.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID,AppWidgetManager.INVALID_APPWIDGET_ID);
            int[] own=AppWidgetManager.getInstance(context).getAppWidgetIds(new ComponentName(context,getClass()));
            for (int candidate:own) if (candidate==id) { updateAsync(context,new int[]{id}); break; }
            return;
        }
        super.onReceive(context,intent);
    }
    private void updateAsync(Context context,int[] requested) {
        final Context app=context.getApplicationContext(); final int[] ids=requested.clone();
        final android.content.BroadcastReceiver.PendingResult pending=goAsync();
        IO.execute(()->{
            try {
                /* 只读提交目录，文件I/O不在桌面广播线程执行。 | Read only the committed directory, off the launcher broadcast thread. */
                SaveSummaryReader.Result summary=SaveSummaryReader.read(app.getFilesDir().toPath().resolve("save"));
                AppWidgetManager manager=AppWidgetManager.getInstance(app);
                for (int id:ids) manager.updateAppWidget(id,render(app,id,summary));
            } finally { if (pending!=null) pending.finish(); }
        });
    }
    private RemoteViews render(Context context,int id,SaveSummaryReader.Result summary) {
        /* 每次更新新建RemoteViews，不累积跨进程操作表。 | Recreate RemoteViews for each update instead of accumulating cross-process operations. */
        RemoteViews views=new RemoteViews(context.getPackageName(),layoutResource());
        Intent open=new Intent(context,DmActivity.class).setAction(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TOP);
        views.setOnClickPendingIntent(R.id.widget_card,PendingIntent.getActivity(context,0,open,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE));
        Intent refresh=new Intent(context,getClass()).setAction(ACTION_REFRESH).putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID,id);
        views.setOnClickPendingIntent(R.id.widget_refresh,PendingIntent.getBroadcast(context,id,refresh,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE));
        int count=Math.min(maxHeroes(),summary.heroes.size());
        views.setViewVisibility(R.id.widget_content,count==0?View.GONE:View.VISIBLE);
        views.setViewVisibility(R.id.widget_message,count==0?View.VISIBLE:View.GONE);
        if (count==0) {
            views.setTextViewText(R.id.widget_message,context.getString(summary.state==SaveSummaryReader.State.EMPTY?R.string.dmAndroidWidgetEmpty:R.string.dmAndroidWidgetError));
            views.setTextViewText(R.id.widget_footer,context.getString(summary.state==SaveSummaryReader.State.EMPTY?R.string.dmAndroidWidgetEmptyHint:R.string.dmAndroidWidgetErrorHint));
        } else {
            NumberFormat number=NumberFormat.getIntegerInstance();
            for (int i=0;i<maxHeroes();++i) {
                views.setViewVisibility(COLUMNS[i],i<count?View.VISIBLE:View.GONE);
                if (i>=count) continue;
                SaveSummaryReader.Hero hero=summary.heroes.get(i);
                views.setTextViewText(NAMES[i],hero.name);
                views.setTextViewText(STATS[i],context.getString(R.string.dmAndroidWidgetStats,hero.level,context.getString(CLASS_NAMES[hero.classIndex])));
                views.setTextViewText(EXP[i],hero.experience>=0?context.getString(R.string.dmAndroidWidgetExp,number.format(hero.experience)):context.getString(R.string.dmAndroidWidgetExpUnknown));
                views.setTextViewText(GOLD[i],hero.gold>=0?context.getString(R.string.dmAndroidWidgetGold,number.format(hero.gold)):context.getString(R.string.dmAndroidWidgetGoldUnknown));
            }
            views.setTextViewText(R.id.widget_footer,context.getString(summary.state==SaveSummaryReader.State.PARTIAL?R.string.dmAndroidWidgetPartial:R.string.dmAndroidWidgetFooter,summary.heroes.size()));
        }
        return views;
    }
    /** 主应用退到后台或确认保存后调用；不启动游戏进程。 | Call after backgrounding or a confirmed save; never starts the game process. */
    public static void requestUpdate(Context context) {
        AppWidgetManager manager=AppWidgetManager.getInstance(context);
        Class<?>[] providers={HeroWidget2x2Provider.class,HeroWidget4x2Provider.class};
        for (Class<?> provider:providers) {
            int[] ids=manager.getAppWidgetIds(new ComponentName(context,provider));
            if (ids.length==0) continue;
            Intent update=new Intent(context,provider).setAction(AppWidgetManager.ACTION_APPWIDGET_UPDATE).putExtra(AppWidgetManager.EXTRA_APPWIDGET_IDS,ids);
            context.sendBroadcast(update);
        }
    }
}
