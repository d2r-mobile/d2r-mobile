// 存档导入/导出：把 files/save 整棵树打成 zip，或从 zip 还原。
// 特意只用 java.io / java.util.zip，不碰 Android API —— 压缩、解压、替换这三件容易出错的事因此可以在电脑上
// 直接跑单元测试（dm/android/tests/SaveArchiveTest.java），界面只负责提供"流从哪来、到哪去"。
// Save import/export: zip the whole files/save tree, or restore from a zip. Deliberately plain Java
// (java.io / java.util.zip only, no Android APIs) so the three error-prone parts -- zip, unzip, swap -- can be
// unit-tested on a PC (dm/android/tests/SaveArchiveTest.java); the UI only supplies where streams come from and go.
package org.d2r.android.port;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

final class SaveArchive {
    // 导出包根上的说明文件：让玩家看得出这是什么，导入时跳过（不进存档目录）。
    // A readme at the archive root: it tells the player what this is, and import skips it (it is not save data).
    static final String MARKER = "d2r-save.json";
    private static final long MAX_ENTRY_BYTES = 1L << 30;   // 单条 1 GiB，防压缩炸弹 | 1 GiB per entry, against zip bombs
    private static final long MAX_TOTAL_BYTES = 2L << 30;   // 合计 2 GiB | 2 GiB in total

    private SaveArchive() { }

    // 导出：saveDir 下所有文件按相对路径写进 zip。返回写入的文件数。
    // Export: every file under saveDir goes into the zip under its relative path. Returns the file count.
    static int zip(File saveDir, OutputStream raw, long nowMillis) throws IOException {
        if (saveDir == null || !saveDir.isDirectory()) throw new IOException("没有可导出的存档目录");
        ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(raw));
        try {
            byte[] marker = ("{\"format\":\"d2r-save\",\"version\":1,\"created\":" + (nowMillis / 1000L) + "}\n")
                    .getBytes("UTF-8");
            zip.putNextEntry(new ZipEntry(MARKER));
            zip.write(marker);
            zip.closeEntry();
            int count = addTree(zip, saveDir, "");
            zip.finish();
            return count;
        } finally {
            zip.close();
        }
    }

    private static int addTree(ZipOutputStream zip, File dir, String prefix) throws IOException {
        File[] children = dir.listFiles();
        if (children == null) return 0;
        // 固定顺序：导出文件的内容稳定，便于比较两次导出 | fixed order so two exports compare cleanly
        Arrays.sort(children, (left, right) -> left.getName().compareTo(right.getName()));
        int count = 0;
        for (File child : children) {
            String name = prefix + child.getName();
            if (child.isDirectory()) {
                count += addTree(zip, child, name + "/");
            } else if (child.isFile()) {
                zip.putNextEntry(new ZipEntry(name));
                try (InputStream in = new BufferedInputStream(new FileInputStream(child))) {
                    byte[] buffer = new byte[8192];
                    int n;
                    while ((n = in.read(buffer)) > 0) zip.write(buffer, 0, n);
                }
                zip.closeEntry();
                count++;
            }
        }
        return count;
    }

    // 解压到 targetDir（调用方保证它是新的空目录）：拒绝绝对路径、反斜杠、`..` 等越界写法，并限制大小。
    // Unzip into targetDir (the caller passes a fresh empty directory): rejects absolute paths, backslashes and
    // ".." escapes, and caps the size.
    static int unzip(InputStream raw, File targetDir) throws IOException {
        String targetPath = targetDir.getCanonicalPath() + File.separator;
        ZipInputStream zip = new ZipInputStream(new BufferedInputStream(raw));
        int count = 0;
        long total = 0;
        try {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                if (entry.isDirectory()) continue;
                if (name.equals(MARKER)) continue;
                if (name.startsWith("/") || name.indexOf('\\') >= 0 || name.contains(".."))
                    throw new IOException("存档包里有非法路径：" + name);
                File out = new File(targetDir, name);
                if (!out.getCanonicalPath().startsWith(targetPath))
                    throw new IOException("存档包里有越界路径：" + name);
                File parent = out.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                    throw new IOException("无法创建目录：" + parent);
                long written = 0;
                try (OutputStream file = new BufferedOutputStream(new FileOutputStream(out))) {
                    byte[] buffer = new byte[8192];
                    int n;
                    while ((n = zip.read(buffer)) > 0) {
                        written += n;
                        total += n;
                        if (written > MAX_ENTRY_BYTES || total > MAX_TOTAL_BYTES)
                            throw new IOException("存档包过大");
                        file.write(buffer, 0, n);
                    }
                }
                count++;
            }
        } finally {
            zip.close();
        }
        if (count == 0) throw new IOException("存档包里没有文件");
        return count;
    }

    // 用 importedDir 替换 saveDir：旧目录先改名保留，新目录就位后才删旧目录；失败则回滚，玩家两头都不会空。
    // Swap importedDir into place: move the old save aside, move the new one in, delete the old only afterwards;
    // on failure roll back so the player never ends up with neither.
    static void replace(File saveDir, File importedDir) throws IOException {
        File backup = new File(saveDir.getParentFile(), saveDir.getName() + ".replaced");
        deleteTree(backup);
        boolean hadOld = saveDir.exists();
        boolean movedOld = hadOld && saveDir.renameTo(backup);
        if (hadOld && !movedOld) throw new IOException("无法移走旧存档");
        if (!importedDir.renameTo(saveDir)) {
            if (movedOld) backup.renameTo(saveDir);
            throw new IOException("无法放入导入的存档");
        }
        deleteTree(backup);
    }

    static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }
}
