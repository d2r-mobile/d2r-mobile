// 存档导入/导出的回归：压缩-解压往返、截断包拒绝、原子替换与中断恢复。
// Save import/export regression: zip/unzip round trip, truncated archives, the atomic swap and interrupted replacement.
package org.d2r.android.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class SaveArchiveTest {
    private static int checks;

    private static void check(boolean condition, String message) {
        checks++;
        if (!condition) throw new AssertionError(message);
        System.out.println("  ✓ " + message);
    }

    private static File tempDir(String name) throws IOException {
        return Files.createTempDirectory("d2r-save-" + name).toFile();
    }

    private static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdirs " + parent);
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    private interface IoAction { void run() throws IOException; }

    private static boolean fails(IoAction action) {
        try { action.run(); return false; } catch (IOException expected) { return true; }
    }

    public static void main(String[] args) throws Exception {
        File save = tempDir("save");
        write(new File(save, "a.txt"), "alpha");
        write(new File(save, "sub/b.bin"), "beta");
        ByteArrayOutputStream exported = new ByteArrayOutputStream();
        int written = SaveArchive.zip(save, exported, 1700000000000L);
        check(written == 2, "导出写入 2 个文件（实际 " + written + "）");

        File restored = tempDir("restored");
        int extracted = SaveArchive.unzip(new ByteArrayInputStream(exported.toByteArray()), restored);
        check(extracted == 2, "解压恢复 2 个文件（实际 " + extracted + "）");
        check(read(new File(restored, "a.txt")).equals("alpha"), "顶层内容一致");
        check(read(new File(restored, "sub/b.bin")).equals("beta"), "子目录内容一致");
        check(!new File(restored, SaveArchive.MARKER).exists(), "说明文件不进入存档");

        File unreadable = new File(save.getPath()) {
            @Override public File[] listFiles() { return null; }
        };
        check(fails(() -> SaveArchive.zip(unreadable, new ByteArrayOutputStream(), 0)),
                "无法读取根目录时导出报错，不生成假成功的空备份");
        File unreadableChild = new File(save, "sub") {
            @Override public File[] listFiles() { return null; }
        };
        File unreadableTree = new File(save.getPath()) {
            @Override public File[] listFiles() { return new File[]{unreadableChild}; }
        };
        check(fails(() -> SaveArchive.zip(unreadableTree, new ByteArrayOutputStream(), 0)),
                "无法读取子目录时导出报错，不静默遗漏子目录");
        File disappearingTree = new File(save.getPath()) {
            @Override public File[] listFiles() { return new File[]{new File(save, "missing.d2s")}; }
        };
        check(fails(() -> SaveArchive.zip(disappearingTree, new ByteArrayOutputStream(), 0)),
                "枚举后消失的文件导致导出失败，不静默跳过");

        // 截断包：有说明文件和一个完整条目、但没有中央目录，必须拒绝，不能把半包当成存档。
        // A truncated archive with the marker and one complete entry but no central directory must be rejected;
        // a partial package must not be treated as a save.
        ByteArrayOutputStream partialBytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(partialBytes)) {
            zip.putNextEntry(new ZipEntry(SaveArchive.MARKER));
            zip.write("{}".getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
            zip.putNextEntry(new ZipEntry("a.txt"));
            zip.write("alpha".getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
            zip.finish();
        }
        byte[] full = partialBytes.toByteArray();
        byte[] truncated = Arrays.copyOf(full, Math.max(0, full.length - 10));
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(truncated), tempDir("truncated"))),
                "截断的存档包被拒绝");

        // 中央目录和 CRC 保持原值，但 STORED 条目的数据字节被改：ZipFile 的流不替我们校验 CRC，必须自己算。
        // The central directory and CRC stay original while one STORED payload byte is flipped: ZipFile's stream
        // does not validate CRC for us, so the extractor must compute it.
        File corruptLive = new File(tempDir("corrupt-live"), "save");
        write(new File(corruptLive, "character.dat"), "old-live");
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(storedCorruptArchive()), tempDir("corrupt"))),
                "损坏的 STORED payload（中央目录 CRC 不变）被拒绝");
        check(read(new File(corruptLive, "character.dat")).equals("old-live"), "损坏包被拒后旧 live 仍完整");

        // DEFLATED 条目被截掉一个压缩字节，中央目录仍可打开：必须按声明的未压缩大小/CRC 拒绝。
        // A DEFLATED entry loses one compressed byte while the central directory still opens: reject it by the
        // declared uncompressed size/CRC.
        byte[] validDeflated = deflatedArchive(false), truncatedDeflated = deflatedArchive(true);
        File deflatedTarget = tempDir("deflated-valid");
        check(SaveArchive.unzip(new ByteArrayInputStream(validDeflated), deflatedTarget) == 1,
                "完整 DEFLATED 包可以解出");
        check(read(new File(deflatedTarget, "hero.d2s")).equals("deflated-save-payload"),
                "完整 DEFLATED 内容一致");
        check(fails(() -> SaveArchive.unzip(new ByteArrayInputStream(truncatedDeflated), tempDir("deflated-truncated"))),
                "截断的 DEFLATED payload 被拒绝");

        // 替换成功：旧存档先移到备份，导入目录就位后旧备份删除。
        File swapParent = tempDir("swap");
        File live = new File(swapParent, "save");
        write(new File(live, "character.dat"), "old");
        File imported = new File(swapParent, "save.import");
        write(new File(imported, "character.dat"), "new");
        SaveArchive.replace(live, imported);
        check(read(new File(live, "character.dat")).equals("new"), "替换后是导入的存档");
        check(!new File(swapParent, "save.replaced").exists(), "成功替换不保留备份");
        check(!imported.exists(), "导入临时目录已就位");

        // 中断替换的恢复：上次在旧目录改名为 .replaced 后中断，live 不存在而 .replaced 仍完整。
        // 下一次替换即使导入包缺失，也必须先把旧存档恢复，再报错，不能删除唯一的副本。
        // Interrupted replacement recovery: the previous run moved the old directory to .replaced and stopped,
        // leaving live absent and .replaced intact. The next replacement, even with a missing import, must restore
        // the old save before reporting failure; it must not delete the only copy.
        File crashParent = tempDir("crash");
        File crashLive = new File(crashParent, "save");
        File crashBackup = new File(crashParent, "save.replaced");
        write(new File(crashBackup, "character.dat"), "keep-me");
        File missing = new File(crashParent, "save.import");
        check(fails(() -> SaveArchive.replace(crashLive, missing)), "导入包缺失时替换报错");
        check(read(new File(crashLive, "character.dat")).equals("keep-me"), "中断替换后旧存档恢复");
        check(!crashBackup.exists(), "恢复后不残留备份目录");

        // 前端在启动时可能已经建了空的 live 目录；恢复时必须认出“只有空目录”，不能把它当成新存档。
        // The front end may already have created an empty live directory at startup; recovery must recognise
        // "only an empty directory" instead of treating it as a new save.
        File emptyParent = tempDir("crash-empty");
        File emptyLive = new File(emptyParent, "save");
        check(emptyLive.mkdirs(), "建空的 live 目录");
        File emptyBackup = new File(emptyParent, "save.replaced");
        write(new File(emptyBackup, "character.dat"), "keep-empty");
        check(fails(() -> SaveArchive.replace(emptyLive, new File(emptyParent, "save.import"))),
                "空 live 下导入包缺失时替换报错");
        check(read(new File(emptyLive, "character.dat")).equals("keep-empty"), "空 live 下旧存档也恢复");
        check(!emptyBackup.exists(), "空 live 恢复后不残留备份");

        // 导出也会先恢复中断备份；否则玩家只能导出一个空 live，随后导入会把它当成新存档。
        // Export also restores an interrupted backup first; otherwise the player exports an empty live and a later
        // import treats it as the new save.
        File exportParent = tempDir("export-crash");
        File exportLive = new File(exportParent, "save");
        check(exportLive.mkdirs(), "建空的导出 live 目录");
        File exportBackup = new File(exportParent, "save.replaced");
        write(new File(exportBackup, "character.dat"), "export-keep");
        ByteArrayOutputStream exportBytes = new ByteArrayOutputStream();
        check(SaveArchive.zip(exportLive, exportBytes, 1700000000000L) == 1, "导出前恢复备份并导出一个文件");
        check(read(new File(exportLive, "character.dat")).equals("export-keep"), "导出恢复后 live 是旧存档");

        System.out.println("== " + checks + " 项全部通过 ==");
    }

    private static byte[] storedCorruptArchive() throws IOException {
        byte[] marker = "{}".getBytes(StandardCharsets.UTF_8);
        byte[] data = "character-data-original".getBytes(StandardCharsets.UTF_8);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeLocal(out, SaveArchive.MARKER, ZipEntry.STORED, crc(marker), marker.length, marker.length);
        out.write(marker);
        int heroOffset = out.size();
        writeLocal(out, "hero.d2s", ZipEntry.STORED, crc(data), data.length, data.length);
        out.write(data);
        int central = out.size();
        writeCentral(out, SaveArchive.MARKER, ZipEntry.STORED, crc(marker), marker.length, marker.length, 0);
        writeCentral(out, "hero.d2s", ZipEntry.STORED, crc(data), data.length, data.length, heroOffset);
        writeEnd(out, 2, out.size() - central, central);
        byte[] archive = out.toByteArray();
        for (int i = 0; i <= archive.length - data.length; i++) {
            boolean match = true;
            for (int j = 0; j < data.length; j++) if (archive[i + j] != data[j]) { match = false; break; }
            if (match) { archive[i] ^= 1; break; }
        }
        return archive;
    }

    private static byte[] deflatedArchive(boolean truncate) throws IOException {
        byte[] marker = "{}".getBytes(StandardCharsets.UTF_8);
        byte[] data = "deflated-save-payload".getBytes(StandardCharsets.UTF_8);
        byte[] compressed = deflate(data);
        if (truncate && compressed.length > 1) {
            int cut = compressed.length / 2;
            byte[] shortened = new byte[compressed.length - 1];
            System.arraycopy(compressed, 0, shortened, 0, cut);
            System.arraycopy(compressed, cut + 1, shortened, cut, compressed.length - cut - 1);
            compressed = shortened;
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeLocal(out, SaveArchive.MARKER, ZipEntry.STORED, crc(marker), marker.length, marker.length);
        out.write(marker);
        int heroOffset = out.size();
        writeLocal(out, "hero.d2s", ZipEntry.DEFLATED, crc(data), compressed.length, data.length);
        out.write(compressed);
        int central = out.size();
        writeCentral(out, SaveArchive.MARKER, ZipEntry.STORED, crc(marker), marker.length, marker.length, 0);
        writeCentral(out, "hero.d2s", ZipEntry.DEFLATED, crc(data), compressed.length, data.length, heroOffset);
        writeEnd(out, 2, out.size() - central, central);
        return out.toByteArray();
    }

    private static long crc(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data);
        return crc.getValue();
    }

    private static byte[] deflate(byte[] data) throws IOException {
        Deflater deflater = new Deflater(Deflater.DEFAULT_COMPRESSION, true);
        try {
            deflater.setInput(data);
            deflater.finish();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            while (!deflater.finished()) {
                int n = deflater.deflate(buffer);
                if (n > 0) out.write(buffer, 0, n);
            }
            return out.toByteArray();
        } finally {
            deflater.end();
        }
    }

    private static void writeLocal(ByteArrayOutputStream out, String name, int method, long crc,
                                   int compressed, int uncompressed) throws IOException {
        byte[] n = name.getBytes(StandardCharsets.UTF_8);
        le32(out, 0x04034b50L); le16(out, 20); le16(out, 0); le16(out, method);
        le16(out, 0); le16(out, 0); le32(out, crc); le32(out, compressed); le32(out, uncompressed);
        le16(out, n.length); le16(out, 0); out.write(n);
    }

    private static void writeCentral(ByteArrayOutputStream out, String name, int method, long crc,
                                     int compressed, int uncompressed, int localOffset) throws IOException {
        byte[] n = name.getBytes(StandardCharsets.UTF_8);
        le32(out, 0x02014b50L); le16(out, 20); le16(out, 20); le16(out, 0); le16(out, method);
        le16(out, 0); le16(out, 0); le32(out, crc); le32(out, compressed); le32(out, uncompressed);
        le16(out, n.length); le16(out, 0); le16(out, 0); le16(out, 0); le16(out, 0);
        le32(out, 0); le32(out, localOffset); out.write(n);
    }

    private static void writeEnd(ByteArrayOutputStream out, int entries, int centralSize, int centralOffset) {
        le32(out, 0x06054b50L); le16(out, 0); le16(out, 0); le16(out, entries); le16(out, entries);
        le32(out, centralSize); le32(out, centralOffset); le16(out, 0);
    }

    private static void le16(ByteArrayOutputStream out, int value) {
        out.write(value & 0xff); out.write((value >>> 8) & 0xff);
    }

    private static void le32(ByteArrayOutputStream out, long value) {
        out.write((int) (value & 0xff)); out.write((int) ((value >>> 8) & 0xff));
        out.write((int) ((value >>> 16) & 0xff)); out.write((int) ((value >>> 24) & 0xff));
    }
}
