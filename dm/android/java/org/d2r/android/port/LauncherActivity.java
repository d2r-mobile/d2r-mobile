// 启动界面：选择设置与导入数据包，写进 launch-settings.conf 后再真正启动游戏页面。
// 构图照标题画面来做：美术铺满整屏，标题在右上，菜单是右侧一列，版本信息在最右下。
// Launcher: collects settings and imports a data package, writes them into launch-settings.conf, then starts the
// game page. The composition follows a title screen: artwork fills the screen, the logo sits top-right, the menu
// is a single right-aligned column, and app info sits at the bottom right.
package org.d2r.android.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class LauncherActivity extends Activity {
    // launch-settings.conf 由 native 严格解析（键白名单、失败即中止启动、且不接受任意路径），
    // 所以只有它认识的键才写进去；界面自己的状态另存一份，互不污染。
    // launch-settings.conf is parsed strictly by native (key whitelist, abort startup on failure, no arbitrary
    // paths allowed), so only keys it knows may go in. The launcher's own state lives in a separate file.
    private static final String CONFIG_NAME = "launch-settings.conf";
    private static final String STATE_NAME = "launcher-state.conf";

    // 导入的包解到这里，必须是 boot() 认的那个固定位置：它靠 manifest.json 是否存在决定用哪套数据。
    // The imported package unpacks here, the fixed location boot() looks for: it decides which data to use by
    // whether manifest.json is present.
    private static final String PACKAGE_DIRECTORY = "d2r-package";
    private static final String MANIFEST_NAME = PackageArchive.MANIFEST;
    private static final int PICK_PACKAGE = 1;
    private static final int EXPORT_SAVE = 2;
    private static final int IMPORT_SAVE = 3;
    private static final int PICK_DRIVER = 4;

    private static final int GOLD = 0xffe8c078;
    private static final int PARCHMENT = 0xffe8e0d0;
    private static final int MUTED = 0xffa2988c;
    private static final int WARNING = 0xffd08a78;

    private Switch overlaySwitch;
    private TextView profileItem, resolutionItem, driverItem, importItem, statusView, startItem;
    private boolean handheld;
    private int resolutionIndex;
    private int driverIndex;
    private String packageSummary = "";
    private boolean importing;
    private volatile boolean cancelImport;
    // 出图分辨率只提供几档预设：空字符串 = 按当前手机窗口比例自动选择（720 级高度）；
    // 1600x900 是 16:9 兼容档（掌机档仍为 1280x720）；其余两档是显式高分辨率，仍在原有档位语义内。
    // The output resolution offers a few presets only: empty = derive from the current phone surface
    // aspect at 720-level height; 1600x900 is the 16:9 compatibility preset (handheld still 1280x720);
    // the other two keep the existing explicit high-resolution semantics.
    private static final String[] RESOLUTION_PRESETS = {"", "1600x900", "1920x1080", "2560x1440"};
    // Explicit system/built-in/imported choices apply on the next process launch.
    private static final String[] DRIVER_STATES = {"system", "turnip", "custom"};
    private DriverArchive.Info customDriver;
    private static final String TURNIP_ASSET = "turnip/vulkan.purple.so";

    private String text(String key) {
        int resource = getResources().getIdentifier(key, "string", getPackageName());
        return resource == 0 ? key : getString(resource);
    }

    // 启动界面只拥有这几个键。文件里其余的键是别处写的（例如诊断用的 input_receipts），重写时必须原样留下 ——
    // 否则「从启动界面启动游戏」会把它们悄悄抹掉。实测就丢过一次 input_receipts=1：配置里写着、native 读到的
    // 却是「未设」，HID 收样一直没生成，查了半天才发现是这个覆盖。
    // The launcher owns only these keys. Any other key in the file was written elsewhere (a diagnostic such as
    // input_receipts, for instance) and has to survive a rewrite -- otherwise starting the game from the launcher
    // silently drops it. Measured once with input_receipts=1: the file said so, native read "unset", the HID
    // receipts were never produced, and the overwrite was the last thing found.
    private static final String[] OWNED_KEYS = {"debug_overlay", "operation_mode", "resolution"};

    // 游戏还活着时启动界面立刻让位。
    // 从桌面点图标回来时，安卓会在**游戏上面**再开一个启动界面（启动界面是本应用的入口活动），玩家看到的是启动界面、
    // 像是"游戏没了"；再点一次「启动游戏」就会开第三个活动，把中间那个游戏活动销毁 —— 而销毁游戏活动在本项目里等于
    // 结束整个进程（一个进程只跑一个游戏），新开的那个也一起死，安卓随后重新起进程从头加载，表现成"应用终止、这一局没了"。
    // 所以只要游戏活动还在（同进程的静态标记），这里直接 finish 自己，把下面的游戏露出来，走我们已经真机验证过的
    // 「回前台重建交换链」那条路。想改设置（分辨率 / 换数据包）请先把应用从最近任务里划掉 —— 那才是真正退出。
    // While the game is alive the launcher steps aside. Tapping the icon from the home screen makes Android push a
    // *second* launcher instance on top of the game (the launcher is this app's entry activity), so the player sees
    // the launcher and the game looks gone; tapping "start game" then creates a third activity and destroys the game
    // one in the middle -- and in this project destroying the game activity ends the whole process (one guest per
    // process), taking the new activity down with it, after which Android starts a fresh process and reloads from
    // scratch, i.e. "the app terminated and the session is gone". So whenever the game activity is still alive (a
    // static flag in this process) the launcher finishes itself at once, revealing the game underneath and going
    // through the background-return path already verified on device. To change settings (resolution, another
    // package) swipe the app away from recents first -- that is a real exit.
    private boolean stepAsideForRunningGame() {
        if (!D2RActivity.hostRunning) return false;
        finish();
        return true;
    }

    private File configFile() {
        File directory = new File(getFilesDir(), "temp");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("cannot create " + directory);
        return new File(directory, CONFIG_NAME);
    }

    private File stateFile() { return new File(getFilesDir(), STATE_NAME); }

    // 大资源解在外部私有目录：那里放得下 9 GB 级的数据，内部数据目录通常放不下。
    // 但**模块必须解在内部目录**：外部存储是 noexec 挂载（实测 /storage/emulated 带 noexec），
    // 从那里 dlopen 会失败；内部目录没有 noexec，图形驱动就是从内部目录加载的。
    // Big assets go to the external private directory, which has room for multi-gigabyte data. The modules must
    // go to the internal one: external storage is mounted noexec (measured on /storage/emulated), so dlopen from
    // there fails, while the internal directory has no such flag — the graphics driver already loads from it.
    private File packageDirectory() {
        File external = getExternalFilesDir(null);
        return external == null ? new File(getFilesDir(), PACKAGE_DIRECTORY) : new File(external, PACKAGE_DIRECTORY);
    }

    private File packageGameDirectory() { return new File(getFilesDir(), PACKAGE_DIRECTORY + "/game"); }

    private File packageManifest() { return new File(packageDirectory(), MANIFEST_NAME); }

    // 导入是否完成，以 manifest.json 与十二个必需文件/每模块 target 为准；只读小标记，不遍历资源。
    // "Ready" means manifest.json plus the twelve required files and each module's target; read-only,
    // no resource walk.
    private boolean packageReady() { return PackageArchive.isReady(packageDirectory(), packageGameDirectory()); }

    // 只做最简单的 key=value 读取；权威解析在 native 的 launch_settings.c。
    // Minimal key=value read; the authoritative parser is launch_settings.c on the native side.
    private void write(File file, String content) {
        try (OutputStreamWriter writer = new OutputStreamWriter(
                new FileOutputStream(file, false), StandardCharsets.UTF_8)) {
            writer.write(content);
        } catch (IOException error) {
            setStatus(error.getMessage(), WARNING);
        }
    }

    private void setStatus(String message, int colour) {
        statusView.setTextColor(colour);
        statusView.setText(message == null ? "" : message);
    }

    private TextView item(String value, float size, int colour, boolean bold, boolean clickable) {
        float density = getResources().getDisplayMetrics().density;
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextColor(colour);
        view.setTextSize(size);
        view.setGravity(Gravity.END);
        if (bold) view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setPadding(0, (int)(7 * density), 0, (int)(7 * density));
        view.setClickable(clickable);
        return view;
    }

    private String profileLabel() {
        return text("dmAndroidLaunchProfile") + "：" +
            text(handheld ? "dmAndroidLaunchHandheld" : "dmAndroidLaunchConsole");
    }

    private String resolutionLabel() {
        String preset = RESOLUTION_PRESETS[resolutionIndex];
        if (preset.isEmpty()) return text("dmAndroidLaunchResolution") + "：" + text("dmAndroidLaunchFollowProfile");
        String[] parts = preset.split("x");
        return text("dmAndroidLaunchResolution") + "：" + parts[0] + "×" + parts[1];
    }

    private boolean turnipBundled() {
        try (InputStream stream = getAssets().open(TURNIP_ASSET)) {
            return stream != null;
        } catch (IOException error) {
            return false;
        }
    }

    private File customDriverDirectory() { return new File(getFilesDir(), "gpu-driver-custom"); }

    private static DriverArchive.Info driverMetadata(byte[] bytes) throws IOException {
        try {
            JSONObject meta = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
            if (meta.getInt("schemaVersion") != 1) throw new IOException("Unsupported driver schema");
            return new DriverArchive.Info(meta.getString("name"), meta.getString("libraryName"), meta.getInt("minApi"),
                    "Qualcomm".equalsIgnoreCase(meta.optString("vendor")) ? 8 : 18);
        } catch (JSONException error) { throw new IOException("Invalid meta.json", error); }
    }

    private String driverLabel() {
        String name = driverIndex == 2 && customDriver != null ? customDriver.name : text(
                driverIndex == 1 ? "dmAndroidLaunchDriverTurnip" : "dmAndroidLaunchDriverSystem");
        return text("dmAndroidLaunchDriver") + "：" + name;
    }

    private void chooseDriver() {
        if (importing) return;
        java.util.ArrayList<String> names = new java.util.ArrayList<>();
        java.util.ArrayList<Integer> choices = new java.util.ArrayList<>();
        names.add(text("dmAndroidLaunchDriverSystem")); choices.add(0);
        if (turnipBundled()) { names.add(text("dmAndroidLaunchDriverTurnip")); choices.add(1); }
        if (customDriver != null) { names.add(customDriver.name); choices.add(2); }
        names.add(text("dmAndroidDriverImport"));
        if (customDriverDirectory().exists()) names.add(text("dmAndroidDriverDelete"));
        new AlertDialog.Builder(this).setTitle(text("dmAndroidLaunchDriver"))
            .setItems(names.toArray(new String[0]), (dialog, which) -> {
                if (which == choices.size()) {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("*/*");
                    try { startActivityForResult(intent, PICK_DRIVER); }
                    catch (RuntimeException error) { setStatus(error.getMessage(), WARNING); }
                } else if (which > choices.size()) {
                    new AlertDialog.Builder(this).setMessage(text("dmAndroidDriverDeleteConfirm"))
                        .setPositiveButton(android.R.string.ok, (confirm, button) -> deleteDriver())
                        .setNegativeButton(android.R.string.cancel, null).show();
                } else { driverIndex = choices.get(which); applyLabels(); writeState(); }
            }).show();
    }

    private void deleteDriver() {
        if (importing) return;
        try {
            DriverArchive.remove(customDriverDirectory());
            customDriver = null;
            if (driverIndex == 2) driverIndex = turnipBundled() ? 1 : 0;
            writeState(); applyLabels();
            setStatus(text("dmAndroidDriverDeleted"), PARCHMENT);
        } catch (IOException error) {
            refreshFromDisk();
            setStatus(error.getMessage(), WARNING);
        }
    }

    private void importDriver(Uri uri) {
        if (importing) return;
        importing = true; updateStartState();
        setStatus(text("dmAndroidDriverImporting"), MUTED);
        new Thread(() -> {
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null) throw new IOException("Cannot read driver ZIP");
                DriverArchive.Info info = DriverArchive.install(in, customDriverDirectory(),
                        LauncherActivity::driverMetadata, android.os.Build.VERSION.SDK_INT);
                runOnUiThread(() -> {
                    customDriver = info; driverIndex = 2; writeState(); applyLabels();
                    setStatus(text("dmAndroidDriverImported") + "：" + info.name, PARCHMENT);
                });
            } catch (Exception error) {
                runOnUiThread(() -> setStatus(text("dmAndroidDriverImportFailed") + "：" + error.getMessage(), WARNING));
            } finally { runOnUiThread(() -> { importing = false; updateStartState(); }); }
        }, "d2r-driver-import").start();
    }

    // 数据包里那张子组档位表（一行裸数字，不是 key=value）。端口会用驱动实际选中的子组尺寸逐项校验它，
    // 不一致就拒绝启动 —— 所以配置里的 subgroup_size 必须照抄这里，不能自己猜。
    // The package's subgroup profile (a bare number, not key=value). The port validates it against the size the
    // driver actually selected and refuses to start on a mismatch, so subgroup_size has to be copied from here
    // rather than guessed.
    private String readProfileSize() {
        File profile = new File(packageDirectory(), "shaders/host-subgroup-size.txt");
        try (InputStream stream = new FileInputStream(profile)) {
            byte[] text = new byte[16];
            int count = stream.read(text);
            if (count <= 0) return null;
            String value = new String(text, 0, count, StandardCharsets.UTF_8).trim();
            return value.isEmpty() ? null : value;
        } catch (IOException error) {
            return null;
        }
    }

    // A selected custom driver must match its runtime identity and the shader subgroup profile.
    private String prepareDriver() {
        File temporary = configFile().getParentFile();
        File configuration = new File(temporary, "gpu-driver.conf");
        if (driverIndex == 0) {
            if (configuration.exists() && !configuration.delete()) return "Cannot select system driver";
            return null;
        }
        String profile = readProfileSize();
        if (!("32".equals(profile) || "64".equals(profile) || "128".equals(profile))) return "host-subgroup-size.txt";
        File directory;
        String library;
        int expectedDriver = 18;
        try {
            if (driverIndex == 2) {
                DriverArchive.Info info = DriverArchive.installed(customDriverDirectory(),
                        LauncherActivity::driverMetadata, android.os.Build.VERSION.SDK_INT);
                directory = customDriverDirectory(); library = info.library; expectedDriver = info.driverId;
            } else {
                directory = new File(temporary, "driver"); library = "vulkan.purple.so";
                if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create driver directory");
                try (InputStream source = getAssets().open(TURNIP_ASSET);
                     OutputStream target = new FileOutputStream(new File(directory, library), false)) {
                    byte[] buffer = new byte[262144]; int count;
                    while ((count = source.read(buffer)) != -1) target.write(buffer, 0, count);
                }
            }
            // Keep the package's subgroup contract; never bypass it to make a driver start.
            String content = "directory=" + directory.getAbsolutePath() + "\n"
                    + "library=" + library + "\nrequire_custom=1\nexpected_driver_id=" + expectedDriver
                    + "\nsubgroup_size=" + profile + "\n";
            try (OutputStreamWriter out = new OutputStreamWriter(new FileOutputStream(configuration), StandardCharsets.UTF_8)) {
                out.write(content);
            }
            return null;
        } catch (IOException error) { return error.getMessage(); }
    }

    private String importLabel() {
        return packageSummary.isEmpty()
                ? text("dmAndroidLaunchImport") + "：" + text("dmAndroidLaunchNotSet")
                : String.format(text("dmAndroidLaunchImported"), packageSummary);
    }

    // 启动界面自己的状态文件：只放界面显示需要的东西（导入摘要、所选驱动），native 不读它。
    // The launcher's own state file: display-only data (import summary, chosen driver), never read by native.
    private void writeState() {
        StringBuilder content = new StringBuilder("# 仅供界面显示 | display only\n");
        if (!packageSummary.isEmpty()) content.append("package_summary=").append(packageSummary).append('\n');
        content.append("driver=").append(DRIVER_STATES[driverIndex]).append('\n');
        write(stateFile(), content.toString());
    }

    // 没有数据包就把"启动游戏"置灰：点了会失败的事不该看起来能点。
    // Without a package the start entry greys out: something that would fail should not look available.
    private void updateStartState() {
        boolean ready = packageReady() && !importing;
        startItem.setEnabled(ready);
        startItem.setTextColor(ready ? GOLD : MUTED);
    }

    // 把当前内存里的设置反映到界面上。点一下之后只调这个，**绝不重新读盘** ——
    // 读盘会把刚改的内存值覆盖回去，结果就是点了没反应（分辨率与画面档位两行都曾如此）。
    // Reflect the in-memory settings onto the labels. Click handlers call only this and never re-read from disk:
    // reloading overwrites the value just changed, so tapping did nothing (true for both the resolution and the
    // profile row).
    private void applyLabels() {
        if (profileItem != null) profileItem.setText(profileLabel());
        if (resolutionItem != null) resolutionItem.setText(resolutionLabel());
        if (driverItem != null) driverItem.setText(driverLabel());
        if (importItem != null) importItem.setText(importLabel());
        if (startItem != null) updateStartState();
    }

    // 从盘重读全部设置到内存，再刷新界面。只在进入/回到界面时调。
    // Read every setting from disk into memory, then refresh the labels. Called only when the screen is (re)entered.
    private void refreshFromDisk() {
        handheld = !"console".equals(LaunchSettings.read(configFile(), "operation_mode"));
        // 调试浮层默认关闭：只有配置里明确写了 1 才是开着（用户要求全局默认不显示）。
        // The debug overlay defaults to off: only an explicit 1 in the config means on (the user asked for it to be
        // off globally by default).
        if (overlaySwitch != null) overlaySwitch.setChecked("1".equals(LaunchSettings.read(configFile(), "debug_overlay")));
        String preset = LaunchSettings.read(configFile(), "resolution");
        resolutionIndex = 0;
        if (preset != null) for (int i = 0; i < RESOLUTION_PRESETS.length; i++) if (RESOLUTION_PRESETS[i].equals(preset)) resolutionIndex = i;
        // 驱动选择记在启动界面自己的状态文件里：launch-settings.conf 由 native 按严格白名单解析，塞不进新键。
        // APK 没带内置驱动时强制回到系统驱动，免得状态文件里的旧值让界面显示一个不存在的选项。
        // The driver choice lives in the launcher's own state file: launch-settings.conf is parsed by native
        // against a strict key whitelist, so a new key cannot go there. When the APK ships no built-in driver the
        // choice is forced back to the system driver, so a stale value cannot make the screen offer a missing one.
        String driver = LaunchSettings.read(stateFile(), "driver");
        driverIndex = driver == null && turnipBundled() && android.os.Build.VERSION.SDK_INT >= 30
                && new File("/dev/kgsl-3d0").exists() ? 1 : 0;
        try { customDriver = DriverArchive.installed(customDriverDirectory(), LauncherActivity::driverMetadata, android.os.Build.VERSION.SDK_INT); }
        catch (IOException error) { customDriver = null; }
        if (driver != null) for (int i = 0; i < DRIVER_STATES.length; i++) if (DRIVER_STATES[i].equals(driver)) driverIndex = i;
        if ((driverIndex == 1 && !turnipBundled()) || (driverIndex == 2 && customDriver == null)) driverIndex = 0;
        applyLabels();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (stepAsideForRunningGame()) return;
        int width = getResources().getDisplayMetrics().widthPixels;
        int height = getResources().getDisplayMetrics().heightPixels;
        int margin = (int)(width * 0.045f);

        FrameLayout root = new FrameLayout(this);
        root.addView(new LauncherBackgroundView(this),
            new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.END);
        column.setPadding(margin, (int)(height * 0.075f), margin, (int)(height * 0.045f));

        column.addView(item(text("dmAndroidLaunchTitle"), 42, GOLD, true, false));
        column.addView(item(text("dmAndroidLaunchSubtitle"), 15, MUTED, false, false));

        column.addView(new View(this), new LinearLayout.LayoutParams(1, 0, 1.6f));

        startItem = item("▸  " + text("dmAndroidLaunchStart"), 28, GOLD, true, true);
        startItem.setOnClickListener(v -> startGame());
        column.addView(startItem);

        overlaySwitch = new Switch(this);
        overlaySwitch.setText(text("dmAndroidLaunchOverlay"));
        overlaySwitch.setTextColor(PARCHMENT);
        overlaySwitch.setTextSize(19);
        overlaySwitch.setGravity(Gravity.END);
        column.addView(overlaySwitch);

        profileItem = item(profileLabel(), 19, PARCHMENT, false, true);
        profileItem.setOnClickListener(v -> { handheld = !handheld; applyLabels(); });
        column.addView(profileItem);

        resolutionItem = item(resolutionLabel(), 19, PARCHMENT, false, true);
        resolutionItem.setOnClickListener(v -> { resolutionIndex = (resolutionIndex + 1) % RESOLUTION_PRESETS.length; applyLabels(); });
        column.addView(resolutionItem);

        {
            driverItem = item(driverLabel(), 19, PARCHMENT, false, true);
            driverItem.setOnClickListener(v -> chooseDriver());
            column.addView(driverItem);
        }

        importItem = item(importLabel(), 19, PARCHMENT, false, true);
        importItem.setOnClickListener(v -> pickPackage());
        column.addView(importItem);

        // 存档导入/导出：走系统的文件选择器（应用不需要任何存储权限），包是玩家自己选的路径。
        // Save import/export goes through the system file picker (the app needs no storage permission); the player
        // picks the location.
        TextView saveExportItem = item(text("dmAndroidSaveExport"), 19, PARCHMENT, false, true);
        saveExportItem.setOnClickListener(v -> exportSave());
        column.addView(saveExportItem);
        TextView saveImportItem = item(text("dmAndroidSaveImport"), 19, PARCHMENT, false, true);
        saveImportItem.setOnClickListener(v -> confirmImportSave());
        column.addView(saveImportItem);

        column.addView(new View(this), new LinearLayout.LayoutParams(1, 0, 1f));

        statusView = item("", 14, WARNING, false, false);
        column.addView(statusView);
        column.addView(item(text("dmAndroidLaunchPending"), 13, MUTED, false, false));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(column, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                            ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(scroll, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                          ViewGroup.LayoutParams.MATCH_PARENT));

        refreshFromDisk();
        setContentView(root);
    }

    @Override protected void onResume() {
        super.onResume();
        if (stepAsideForRunningGame()) return;
        refreshFromDisk();
    }

    // 选包用系统文件选择器：应用不需要任何存储权限，也不必知道文件放在哪。
    // The system picker chooses the package, so the app needs no storage permission and does not care where the
    // file lives. Selecting it again while an import runs asks that import to stop.
    private void pickPackage() {
        if (importing) {
            cancelImport = true;
            setStatus(text("dmAndroidLaunchCancel"), MUTED);
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // 部分提供方把 zip 报成 octet-stream，所以类型放宽；否则文件在选择器里是灰的。
        // Some providers report a zip as octet-stream, so the type is left open; otherwise the file greys out.
        intent.setType("*/*");
        try {
            startActivityForResult(intent, PICK_PACKAGE);
        } catch (RuntimeException error) {
            setStatus(String.format(text("dmAndroidLaunchImportFailed"), String.valueOf(error.getMessage())), WARNING);
        }
    }

    // ---------- 存档导入 / 导出 ----------
    // 存档在内部目录 files/save/（角色、共享仓库、Settings 三个目录）；导出就是把它整棵树打成 zip，
    // 导入是"解到临时目录 → 校验 → 原子换入"，失败的导入不会动到现有存档（换入前旧目录还在）。
    // The save lives in the internal files/save/ (character, shared stash, Settings); export zips the whole tree,
    // import is "unpack to a staging directory -> validate -> swap in", and a failed import never touches the
    // existing save (the old directory is still there until the swap).
    private File saveDirectory() { return new File(getFilesDir(), "save"); }

    // 文件名带时间戳，方便留多份；玩家仍可在选择器里改名。| Timestamped so several copies are easy; renameable in the picker.
    private String saveArchiveName() {
        return "d2r-save-" + new SimpleDateFormat("yyyyMMdd-HHmm", Locale.US).format(new Date()) + ".zip";
    }

    private void exportSave() {
        if (!saveDirectory().isDirectory()) { setStatus(text("dmAndroidSaveNone"), WARNING); return; }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        intent.putExtra(Intent.EXTRA_TITLE, saveArchiveName());
        try {
            startActivityForResult(intent, EXPORT_SAVE);
        } catch (RuntimeException error) {
            setStatus(String.format(text("dmAndroidSaveExportFailed"), String.valueOf(error.getMessage())), WARNING);
        }
    }

    private void confirmImportSave() {
        new AlertDialog.Builder(this)
                .setMessage(text("dmAndroidSaveImportConfirm"))
                .setPositiveButton(android.R.string.ok, (dialog, which) -> pickSaveArchive())
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void pickSaveArchive() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // 与选数据包同样的理由：有的提供方把 zip 报成 octet-stream，类型放宽，否则文件在选择器里是灰的。
        // Same reason as the package picker: some providers report a zip as octet-stream, so the type is left open.
        intent.setType("*/*");
        try {
            startActivityForResult(intent, IMPORT_SAVE);
        } catch (RuntimeException error) {
            setStatus(String.format(text("dmAndroidSaveImportFailed"), String.valueOf(error.getMessage())), WARNING);
        }
    }

    private void writeSaveArchive(Uri target) {
        try (OutputStream out = getContentResolver().openOutputStream(target)) {
            if (out == null) throw new IOException("无法写入所选位置");
            int files = SaveArchive.zip(saveDirectory(), out, System.currentTimeMillis());
            setStatus(String.format(text("dmAndroidSaveExported"), files), PARCHMENT);
        } catch (Exception error) {
            setStatus(String.format(text("dmAndroidSaveExportFailed"), String.valueOf(error.getMessage())), WARNING);
        }
    }

    private void readSaveArchive(Uri source) {
        File staging = new File(getFilesDir(), "save.import");
        SaveArchive.deleteTree(staging);
        try (InputStream in = getContentResolver().openInputStream(source)) {
            if (in == null) throw new IOException("无法读取所选文件");
            int files = SaveArchive.unzip(in, staging);
            SaveArchive.replace(saveDirectory(), staging);
            setStatus(String.format(text("dmAndroidSaveImported"), files), PARCHMENT);
        } catch (Exception error) {
            SaveArchive.deleteTree(staging);
            setStatus(String.format(text("dmAndroidSaveImportFailed"), String.valueOf(error.getMessage())), WARNING);
        }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        if (request == PICK_DRIVER) { importDriver(data.getData()); return; }
        if (request == EXPORT_SAVE) { writeSaveArchive(data.getData()); return; }
        if (request == IMPORT_SAVE) { readSaveArchive(data.getData()); return; }
        if (request == PICK_PACKAGE) beginImport(data.getData());
    }

    private void beginImport(Uri uri) {
        if (importing) return;
        importing = true;
        cancelImport = false;
        updateStartState();
        setStatus(String.format(text("dmAndroidLaunchImporting"), 0), MUTED);
        new Thread(() -> {
            try {
                String summary = extract(uri, documentSize(uri));
                runOnUiThread(() -> {
                    packageSummary = summary;
                    writeState();
                    refreshFromDisk();
                    setStatus(summary, PARCHMENT);
                });
            } catch (Exception error) {
                runOnUiThread(() -> setStatus(String.format(text("dmAndroidLaunchImportFailed"), failureText(error)), WARNING));
            } finally {
                importing = false;
                runOnUiThread(this::updateStartState);
            }
        }, "d2r-package-import").start();
    }

    private long documentSize(Uri uri) {
        try (android.database.Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getLong(0);
        } catch (RuntimeException ignored) {
        }
        return -1;
    }

    // 规则、验收与"最后才写完成标记"的顺序都在 PackageArchive 里（那部分不碰 Android API，能在电脑上跑回归），
    // 这里只负责「流从哪来、进度怎么显示、取消怎么判断、失败怎么讲给玩家听」。
    // The rules, the acceptance check and the write-the-marker-last ordering all live in PackageArchive (which holds
    // no Android API and is regression-tested on a PC). This supplies only where the stream comes from, how progress
    // is shown, how cancellation is noticed, and how a failure is told to the player.
    private String extract(Uri uri, long totalBytes) throws IOException {
        PackageArchive.Counts counts;
        try (InputStream raw = getContentResolver().openInputStream(uri)) {
            if (raw == null) throw new IOException("cannot open the selected file");
            counts = PackageArchive.unpack(raw, packageDirectory(), packageGameDirectory(), totalBytes,
                                           new PackageArchive.Sink() {
                @Override public void progress(final int percent) {
                    runOnUiThread(() -> setStatus(String.format(text("dmAndroidLaunchImporting"), percent), MUTED));
                }
                @Override public boolean cancelled() { return cancelImport; }
            }, LauncherActivity::declaration);
        }
        // 显示**实际解出**的数量。清单自报的数只参与校验、不当结果显示 —— 以前正是把它当结果显示，
        // 才让一个只装了一个文件的包报出"46 个资源、20795 个着色器"。
        // Report what was **actually** unpacked. The manifest's own numbers are only compared, never displayed:
        // displaying them is what once made a package holding one file report "46 assets, 20795 shaders".
        return String.format(text("dmAndroidLaunchImportSummary"), counts.romfs, counts.shaders);
    }

    // 清单的解析交给 org.json（Android 自带）；规则层只要这几个数，所以不依赖任何 JSON 实现。
    // org.json does the parsing. The rule layer wants only these few numbers, so it depends on no JSON library.
    private static PackageArchive.Declaration declaration(byte[] manifest) throws IOException {
        try {
            JSONObject root = new JSONObject(new String(manifest, StandardCharsets.UTF_8));
            JSONObject counts = root.optJSONObject("counts");
            return new PackageArchive.Declaration(
                root.optInt("format", PackageArchive.Declaration.ABSENT),
                counts == null ? PackageArchive.Declaration.ABSENT : counts.optInt("game", PackageArchive.Declaration.ABSENT),
                counts == null ? PackageArchive.Declaration.ABSENT : counts.optInt("romfs", PackageArchive.Declaration.ABSENT),
                counts == null ? PackageArchive.Declaration.ABSENT : counts.optInt("shaders", PackageArchive.Declaration.ABSENT));
        } catch (JSONException error) {
            throw new IOException("package manifest is not valid JSON: " + error.getMessage());
        }
    }

    // 规则层抛出的失败带的是「哪条规则」的键，在这里翻成中英文字；表里没这个键就退回异常原文，方便看日志。
    // A failure from the rule layer carries the key of the rule it broke; turn it into text here, falling back to
    // the raw message when the table has no such key so the log stays useful.
    private String failureText(Exception error) {
        if (!(error instanceof PackageArchive.Failure)) return String.valueOf(error.getMessage());
        PackageArchive.Failure failure = (PackageArchive.Failure) error;
        String format = text(failure.key);
        return format.equals(failure.key)
            ? String.valueOf(error.getMessage())
            : String.format(format, failure.arguments);
    }

    private void startGame() {
        if (importing) {
            setStatus(String.format(text("dmAndroidLaunchImporting"), 0), WARNING);
            return;
        }
        if (!packageReady()) {
            setStatus(text("dmAndroidLaunchNeedsImport"), WARNING);
            return;
        }
        // 先把驱动准备好（选内置 Turnip 就把 .so 复制到内部可执行目录并写 gpu-driver.conf，选系统驱动就删掉它）。
        // 失败就停下不启动：否则玩家以为在用内置驱动，实际跑的是系统驱动。
        // Prepare the driver first (copy the .so into internal executable storage and write gpu-driver.conf for the
        // built-in Turnip; delete it for the system driver). On failure stop rather than start: the player would
        // otherwise believe the built-in driver is in use while the system one runs.
        setStatus(text("dmAndroidLaunchDriverPreparing"), MUTED);
        String driverProblem = prepareDriver();
        if (driverProblem != null) {
            setStatus(String.format(text("dmAndroidLaunchDriverFailed"), driverProblem), WARNING);
            return;
        }
        if (driverIndex != 0) {
            importing = true; updateStartState();
            new Thread(() -> {
                String problem = checkDriver();
                runOnUiThread(() -> {
                    importing = false; updateStartState();
                    if (isFinishing() || isDestroyed()) return;
                    if (problem != null) {
                        new AlertDialog.Builder(this).setTitle(text("dmAndroidDriverIncompatible"))
                            .setMessage(text("dmAndroidDriverTryBuiltin") + "\n\n" + problem)
                            .setPositiveButton(android.R.string.ok, null).show();
                        setStatus(text("dmAndroidDriverIncompatible"), WARNING);
                    } else startPreparedGame();
                });
            }, "d2r-driver-check").start();
            return;
        }
        startPreparedGame();
    }

    private String checkDriver() {
        File report = new File(getCacheDir(), "driver-check.txt");
        Process process = null;
        try {
            String libraries = getApplicationInfo().nativeLibraryDir;
            process = new ProcessBuilder(libraries + "/libdmdrivercheck.so",
                    new File(configFile().getParentFile(), "gpu-driver.conf").getAbsolutePath(), libraries)
                    .redirectErrorStream(true).redirectOutput(report).start();
            if (!process.waitFor(15, java.util.concurrent.TimeUnit.SECONDS)) {
                process.destroyForcibly(); return "Driver compatibility check timed out";
            }
            if (process.exitValue() == 0) return null;
            try (InputStream in = new FileInputStream(report)) {
                byte[] bytes = new byte[4096]; int n = in.read(bytes);
                if (n > 0) {
                    String reportText = new String(bytes, 0, n, StandardCharsets.UTF_8).trim();
                    int lastLine = reportText.lastIndexOf('\n');
                    return reportText.substring(lastLine + 1);
                }
                return "Driver check failed (exit " + process.exitValue() + ")";
            }
        } catch (IOException | InterruptedException error) {
            if (error instanceof InterruptedException) Thread.currentThread().interrupt();
            return String.valueOf(error.getMessage());
        } finally { if (process != null && process.isAlive()) process.destroyForcibly(); }
    }

    private void startPreparedGame() {
        setStatus("", PARCHMENT);
        StringBuilder content = new StringBuilder();
        content.append("# 由启动界面写入 | written by the launcher\n");
        content.append("debug_overlay=").append(overlaySwitch.isChecked() ? "1" : "0").append('\n');
        content.append("operation_mode=").append(handheld ? "handheld" : "console").append('\n');
        // 跟随档位时不写这个键：native 语义里"省略"= 按手机窗口比例自动选择；1600x900 是 16:9 兼容档。
        // Auto writes no key: omission means "derive from the phone surface aspect" in native; 1600x900 is the
        // 16:9 compatibility preset.
        if (!RESOLUTION_PRESETS[resolutionIndex].isEmpty()) content.append("resolution=").append(RESOLUTION_PRESETS[resolutionIndex]).append('\n');
        content.append(LaunchSettings.foreign(configFile(), OWNED_KEYS));
        write(configFile(), content.toString());
        setStatus("", PARCHMENT);
        startActivity(new Intent(this, D2RActivity.class));
    }
}
