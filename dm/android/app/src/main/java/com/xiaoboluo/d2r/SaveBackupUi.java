// 启动页存档操作与系统文件选择器；运行中禁止操作，后台串行读写。
// Startup save actions and system document picker; blocked during gameplay, with serial background I/O.
package com.xiaoboluo.d2r;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.function.BooleanSupplier;

final class SaveBackupUi {
    private static final int EXPORT = 9103, IMPORT = 9104;
    private static volatile boolean busy;
    private final Activity activity;
    private final BooleanSupplier idle;

    SaveBackupUi(Activity activity, BooleanSupplier idle) { this.activity = activity; this.idle = idle; }
    static boolean busy() { return busy; }
    private boolean available() {
        if (busy || !idle.getAsBoolean()) { toast(R.string.dmAndroidSaveIdle); return false; }
        return true;
    }
    private void toast(int message) { Toast.makeText(activity, message, Toast.LENGTH_LONG).show(); }

    void exportSave() {
        if (!available()) return;
        Intent pick = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip");
        pick.putExtra(Intent.EXTRA_TITLE, "D2R-saves-" + LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss")) + ".zip");
        open(pick, EXPORT);
    }

    void importSave() {
        if (!available()) return;
        new AlertDialog.Builder(activity).setTitle(R.string.dmAndroidSaveImport)
            .setMessage(R.string.dmAndroidSaveConfirm)
            .setNegativeButton(R.string.dmCancel, null)
            .setPositiveButton(R.string.dmConfirm, (dialog, which) -> {
                if (!available()) return;
                open(new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip"), IMPORT);
            }).show();
    }

    private void open(Intent intent, int request) {
        try { activity.startActivityForResult(intent, request); }
        catch (android.content.ActivityNotFoundException error) { toast(R.string.dmAndroidSavePickerMissing); }
    }

    boolean result(int request, int result, Intent data) {
        if (request != EXPORT && request != IMPORT) return false;
        if (result != Activity.RESULT_OK || data == null || data.getData() == null) return true;
        if (!available()) return true;
        Uri uri = data.getData(); busy = true;
        toast(R.string.dmAndroidSaveBusy);
        new Thread(() -> {
            String message;
            try {
                if (request == EXPORT) {
                    try (OutputStream output = activity.getContentResolver().openOutputStream(uri, "w")) {
                        if (output == null) throw new IOException("Cannot open destination");
                        SaveArchive.write(activity.getFilesDir().toPath(), output);
                    }
                    message = activity.getString(R.string.dmAndroidSaveExported);
                } else {
                    try (InputStream input = activity.getContentResolver().openInputStream(uri)) {
                        if (input == null) throw new IOException("Cannot open backup");
                        SaveArchive.restore(activity.getFilesDir().toPath(), input);
                    }
                    com.xiaoboluo.d2r.widget.HeroWidgetProvider.requestUpdate(activity);
                    message = activity.getString(R.string.dmAndroidSaveImported);
                }
            } catch (IOException | RuntimeException error) {
                message = activity.getString(R.string.dmAndroidSaveFailed, error.getMessage());
            } finally {
                busy = false;
            }
            String completed = message;
            activity.runOnUiThread(() -> {
                if (!activity.isFinishing() && !activity.isDestroyed()) Toast.makeText(activity, completed, Toast.LENGTH_LONG).show();
            });
        }, "save-backup").start();
        return true;
    }
}
