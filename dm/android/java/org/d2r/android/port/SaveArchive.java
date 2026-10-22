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
import java.util.Enumeration;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
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
        if (saveDir == null) throw new IOException("没有可导出的存档目录");
        recover(saveDir);   // 若上次替换中断留下唯一备份，导出前先恢复，避免导出一个空的 live | restore a sole backup before exporting an empty live after an interrupted replacement
        if (!saveDir.isDirectory()) throw new IOException("没有可导出的存档目录");
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
        if (children == null) throw new IOException("无法读取存档目录：" + dir);
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
            } else {
                throw new IOException("无法读取存档文件：" + child);
            }
        }
        return count;
    }

    // 解压到 targetDir（调用方保证它是新的空目录）：拒绝绝对路径、反斜杠、`..` 等越界写法，并限制大小。
    // 先把输入落到有界临时文件，再由 ZipFile 读取中央目录；截断的流不会因为有前半段条目就被当成完整存档。
    // Unzip into targetDir (the caller passes a fresh empty directory): rejects absolute paths, backslashes and
    // ".." escapes, and caps the size. Spool the input to a bounded temporary file first, then read it through
    // ZipFile's central directory: a truncated stream must not be accepted just because its first entries parsed.
    static int unzip(InputStream raw, File targetDir) throws IOException {
        if (raw == null) throw new IOException("没有可读取的存档包");
        File parent = targetDir.getAbsoluteFile().getParentFile();
        File temporary = File.createTempFile("d2r-save-", ".zip", parent);
        try {
            spool(raw, temporary);
            String targetPath = targetDir.getCanonicalPath() + File.separator;
            int count = 0, marker = 0;
            long total = 0;
            try (ZipFile zip = new ZipFile(temporary)) {
                Enumeration<? extends ZipEntry> entries = zip.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry entry = entries.nextElement();
                    String name = entry.getName();
                    if (entry.isDirectory()) continue;
                    if (name.equals(MARKER)) { marker++; continue; }
                    if (name.startsWith("/") || name.indexOf('\\') >= 0 || name.contains(".."))
                        throw new IOException("存档包里有非法路径：" + name);
                    File out = new File(targetDir, name);
                    if (!out.getCanonicalPath().startsWith(targetPath))
                        throw new IOException("存档包里有越界路径：" + name);
                    File outParent = out.getParentFile();
                    if (outParent != null && !outParent.isDirectory() && !outParent.mkdirs())
                        throw new IOException("无法创建目录：" + outParent);
                    long expectedSize = entry.getSize();
                    long expectedCrc = entry.getCrc();
                    if (expectedSize < 0 || expectedSize > MAX_ENTRY_BYTES ||
                            expectedCrc < 0 || expectedSize > MAX_TOTAL_BYTES - total)
                        throw new IOException("存档条目缺少完整校验信息：" + name);
                    long written = 0;
                    CRC32 crc = new CRC32();
                    try (InputStream in = zip.getInputStream(entry);
                         OutputStream file = new BufferedOutputStream(new FileOutputStream(out))) {
                        byte[] buffer = new byte[8192];
                        int n;
                        while ((n = in.read(buffer)) != -1) {
                            if (n == 0) continue;
                            written += n;
                            total += n;
                            if (written > expectedSize || total > MAX_TOTAL_BYTES)
                                throw new IOException("存档包过大");
                            crc.update(buffer, 0, n);
                            file.write(buffer, 0, n);
                        }
                    }
                    if (written != expectedSize)
                        throw new IOException("存档条目不完整：" + name);
                    if (crc.getValue() != expectedCrc)
                        throw new IOException("存档条目 CRC 不符：" + name);
                    count++;
                }
            }
            if (marker == 0) throw new IOException("存档包缺少说明文件");
            if (count == 0) throw new IOException("存档包里没有文件");
            return count;
        } finally {
            temporary.delete();
        }
    }

    private static final long MAX_ARCHIVE_BYTES = MAX_TOTAL_BYTES + (64L << 20);   // 压缩包本身的上限，含 ZIP 头 | cap for the compressed archive itself, including ZIP headers

    private static void spool(InputStream raw, File destination) throws IOException {
        long total = 0;
        try (OutputStream out = new BufferedOutputStream(new FileOutputStream(destination))) {
            byte[] buffer = new byte[8192];
            int n;
            while ((n = raw.read(buffer)) != -1) {
                if (n == 0) continue;
                total += n;
                if (total > MAX_ARCHIVE_BYTES) throw new IOException("存档包过大");
                out.write(buffer, 0, n);
            }
        }
    }

    // 中断替换的恢复：旧目录已经改名为 <save>.replaced，但新的导入目录还没就位（进程被杀/设备没电）。
    // 如果 live 不存在，或只剩前端启动时建的空目录，先把唯一完整的旧存档恢复回 live；失败时保留备份并抛出可见错误。
    // Recover an interrupted replacement: the old directory was renamed to <save>.replaced but the imported one
    // never took its place (process killed / power loss). If live is absent, or only an empty directory created
    // by the front end at startup, restore the only complete old save; on failure keep the backup and throw.
    static boolean recover(File saveDir) throws IOException {
        if (saveDir == null) throw new IOException("没有存档目录");
        File backup = backupOf(saveDir);
        if (!backup.exists()) return false;
        if (saveDir.exists()) {
            String[] children = saveDir.isDirectory() ? saveDir.list() : null;
            if (!saveDir.isDirectory() || children == null)
                throw new IOException("无法恢复上次替换的存档；旧存档保留在 " + backup);
            if (children.length != 0) return false;
            if (!saveDir.delete())
                throw new IOException("无法恢复上次替换的存档；旧存档保留在 " + backup);
        }
        if (!backup.renameTo(saveDir))
            throw new IOException("无法恢复上次替换的存档；旧存档保留在 " + backup);
        return true;
    }

    private static File backupOf(File saveDir) {
        return new File(saveDir.getAbsoluteFile().getParentFile(), saveDir.getName() + ".replaced");
    }

    // 用 importedDir 替换 saveDir：旧目录先改名保留，新目录就位后才删旧目录；失败则回滚，玩家两头都不会空。
    // Swap importedDir into place: move the old save aside, move the new one in, delete the old only afterwards;
    // on failure roll back so the player never ends up with neither.
    static void replace(File saveDir, File importedDir) throws IOException {
        if (saveDir == null || importedDir == null) throw new IOException("没有存档目录");
        File backup = backupOf(saveDir);
        recover(saveDir);                                      // 上次中断留下的备份先回到 live，不能被当陈旧备份删掉 | restore an interrupted backup before treating it as stale
        if (backup.exists() && saveDir.exists()) deleteTree(backup);
        boolean hadOld = saveDir.exists();
        boolean movedOld = hadOld && saveDir.renameTo(backup);
        if (hadOld && !movedOld) throw new IOException("无法移走旧存档");
        if (!importedDir.renameTo(saveDir)) {
            boolean restored = movedOld && backup.renameTo(saveDir);
            if (!restored && movedOld) throw new IOException("无法放入导入的存档；旧存档保留在 " + backup);
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
