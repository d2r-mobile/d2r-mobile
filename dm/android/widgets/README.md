# Hero desktop widgets / vivo atomic-component candidate

The Android shell provides two read-only `RemoteViews` widgets: the latest saved hero (2×2), and up to three most recently played heroes (4×2). Both open the app when tapped and have a separate refresh action. Empty and unreadable-save states are localized. No game executable, game art, experience table, or player save is included.

This is implementation for platform evaluation. It has **not** received vivo platform permission, component review, or store approval. Installing an APK or setting `vivo_widget=true` does not demonstrate inclusion in the vivo atomic-component catalog or one-tap pinning support.

## Data and limitations

`SaveSummaryReader` reads only `context.getFilesDir()/save/<directory>/<directory>.d2s`, the committed `DM_SAVE` path set in `jni_android.c`. It rejects a root named `save.work`, symbolic-link entries, oversized/truncated files, invalid checksums, malformed UTF-8, unknown classes, and unsupported versions. No save is created, modified, imported, or deleted by widget code. File reads run off the broadcast thread.

The field layout follows the existing Harmony `SaveSummary.ts`: class at `0x18`, level at `0x1b`, unsigned last-played time at `0x20`, UTF-8 name at `0x12b`, and LSB-first `gf` stat fields. Versions `0x61` (the Harmony reader's documented format) and `0x69` (also evidenced by `dm/verify/savecmp.py`) are accepted. Other versions require independently verified layout support. The checksum and declared length must match; the bitstream must terminate with ordered known IDs and a level matching the header. If no unambiguous valid stat block is found, experience/gold remain unavailable while valid header fields can still be shown. Gold is carried plus bank gold only when both fields were read.

Experience percentages and progress bars are intentionally absent: no generated experience table is connected. Raw experience is displayed when verified. The original Harmony reader's embedded experience table was **not copied**. There are no extracted class portraits, last-played date labels, or gameplay actions in this first implementation. The wide layout uses three columns rather than assuming Harmony's dimension naming maps to Android.

## Application integration

These receivers are registered under the manifest's `application`. Do not add an application process or load the native game library for widget updates.

```xml
<receiver android:name=".widget.HeroWidget2x2Provider"
    android:exported="false" android:label="@string/dmAndroidWidgetName2x2">
    <intent-filter>
        <action android:name="android.appwidget.action.APPWIDGET_UPDATE" />
    </intent-filter>
    <meta-data android:name="android.appwidget.provider" android:resource="@xml/widget_2x2_info" />
    <meta-data android:name="vivo_widget" android:value="true" />
    <meta-data android:name="vivoWidgetVersion" android:value="1" />
    <meta-data android:name="vivo.widget.description" android:resource="@string/dmAndroidWidgetDescription" />
</receiver>
<receiver android:name=".widget.HeroWidget4x2Provider"
    android:exported="false" android:label="@string/dmAndroidWidgetName4x2">
    <intent-filter>
        <action android:name="android.appwidget.action.APPWIDGET_UPDATE" />
    </intent-filter>
    <meta-data android:name="android.appwidget.provider" android:resource="@xml/widget_4x2_info" />
    <meta-data android:name="vivo_widget" android:value="true" />
    <meta-data android:name="vivoWidgetVersion" android:value="1" />
    <meta-data android:name="vivo.widget.description" android:resource="@string/dmAndroidWidgetDescription" />
</receiver>
```

The Activity calls this from `onStop` after backgrounding:

```java
com.xiaoboluo.d2r.widget.HeroWidgetProvider.requestUpdate(this);
```

This sends explicit updates only to installed widget instances and never starts the game. The widget's refresh button also reads the current committed saves. There is no native save-commit callback yet; avoid describing backgrounding as proof that a save succeeded. The periodic fallback is 12 hours, matching vivo's minimum; no frequent timer or file watcher runs. No extra Gradle dependency, network permission, email, account operation, or vendor SDK was introduced.

The shared `dm/i18n/ui_text.tsv` keys below are integrated. The existing generator includes `dmAndroid*` and lowercases keys into `R.string` IDs.

| Key | 中文 | English |
| --- | --- | --- |
| dmAndroidWidgetTitle | 英雄 | Heroes |
| dmAndroidWidgetRefresh | 刷新 | Refresh |
| dmAndroidWidgetName2x2 | 最近英雄 | Latest Hero |
| dmAndroidWidgetName4x2 | 英雄列表 | Hero List |
| dmAndroidWidgetDescription | 查看已保存的英雄进度 | View saved hero progress |
| dmAndroidWidgetEmpty | 还没有英雄 | No heroes yet |
| dmAndroidWidgetEmptyHint | 点开游戏创建英雄 | Open the game to create a hero |
| dmAndroidWidgetError | 无法读取存档摘要 | Save summary unavailable |
| dmAndroidWidgetErrorHint | 成功保存后请刷新 | Refresh after a successful save |
| dmAndroidWidgetStats | %1$d级 · %2$s | Lv %1$d · %2$s |
| dmAndroidWidgetExp | 经验 %1$s | Exp %1$s |
| dmAndroidWidgetGold | 金币 %1$s | Gold %1$s |
| dmAndroidWidgetExpUnknown | 经验暂不可读 | Experience unavailable |
| dmAndroidWidgetGoldUnknown | 金币暂不可读 | Gold unavailable |
| dmAndroidWidgetFooter | %1$d位英雄 | %1$d heroes |
| dmAndroidWidgetPartial | %1$d位英雄 · 部分存档不可读 | %1$d heroes · some saves unreadable |
| dmAndroidWidgetClassAmazon | 亚马逊 | Amazon |
| dmAndroidWidgetClassSorceress | 法师 | Sorceress |
| dmAndroidWidgetClassNecromancer | 死灵法师 | Necromancer |
| dmAndroidWidgetClassPaladin | 圣骑士 | Paladin |
| dmAndroidWidgetClassBarbarian | 野蛮人 | Barbarian |
| dmAndroidWidgetClassDruid | 德鲁伊 | Druid |
| dmAndroidWidgetClassAssassin | 刺客 | Assassin |
| dmAndroidWidgetClassWarlock | 术士 | Warlock |

## Validation

From the repository root, run with a JDK (no Android/native build needed):

```text
javac -d <temporary-output> dm/android/app/src/main/java/com/xiaoboluo/d2r/widget/SaveSummaryReader.java dm/android/tests/SaveSummaryReaderTest.java
java -cp <temporary-output> com.xiaoboluo.d2r.widget.SaveSummaryReaderTest
```

The test generates synthetic bytes and its own temporary directory, then removes only that directory. It covers supported/unknown versions, checksum/length validation, unsigned fields, UTF-8, malformed stats, sort order, partial failures, working-directory rejection, and no mutation of input or save bytes. It does not establish compatibility with a real player's save, launcher rendering, platform acceptance, or refresh delivery. APK compilation and lint passed on 2026-10-16; vivo registered both providers. Launcher rendering, refresh and real-save compatibility remain unverified.

## Official sources and external requirements

- [vivo technical adaptation](https://dev.vivo.com.cn/documentCenter/doc/845): AppWidget receiver metadata, 2×2/4×2 dimensions, minimum 12-hour periodic refresh, `resizeMode=none`, no component scrolling, fresh RemoteViews per update, background-thread I/O.
- [vivo onboarding](https://dev.vivo.com.cn/documentCenter/doc/850): the app must already be in the vivo store; the developer must apply for component-management access and submit the APK for review. No application was sent by this implementation.
- [vivo review rules](https://dev.vivo.com.cn/documentCenter/doc/829): separate review and design requirements apply.
- [Android AppWidget documentation](https://developer.android.com/develop/ui/views/appwidgets): standard provider/RemoteViews behavior is separate from vendor approval.

The vivo pages are JavaScript shells; their official public content endpoint `/webapi/doc/info?id=<id>` was read to verify these requirements. Availability in a particular OriginOS build remains untested. No one-tap pinning/catalog navigation code is included because it would require capability and approval checks.
