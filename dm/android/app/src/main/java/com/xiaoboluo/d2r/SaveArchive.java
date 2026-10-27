// 启动页存档ZIP：只处理已提交目录，先校验暂存，保留导入前备份并支持换目录中断恢复。
// Startup save ZIPs: committed saves only, staged validation, pre-import backup and interrupted-swap recovery.
package com.xiaoboluo.d2r;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.CRC32;
import java.util.zip.CheckedInputStream;
import java.util.zip.ZipOutputStream;
import java.util.stream.Stream;

public final class SaveArchive {
    private static final String MARKER = "d2r-save-backup.txt";
    private static final byte[] VERSION = "D2R_SAVE_BACKUP_V1\n".getBytes(StandardCharsets.UTF_8);
    private static final long MAX_FILE = 64L * 1024 * 1024, MAX_TOTAL = 512L * 1024 * 1024;
    private static final int MAX_ENTRIES = 4096;
    private SaveArchive() {}

    public static synchronized void recover(Path files) throws IOException {
        Path save = files.resolve("save"), previous = files.resolve("save-import.previous");
        if (!Files.exists(save, LinkOption.NOFOLLOW_LINKS) && Files.exists(previous, LinkOption.NOFOLLOW_LINKS))
            Files.move(previous, save, StandardCopyOption.ATOMIC_MOVE);
    }

    public static synchronized void write(Path files, OutputStream output) throws IOException {
        recover(files);
        Path root = files.resolve("save");
        if (!Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(root))
            throw new IOException("No committed saves");
        try (ZipOutputStream zip = new ZipOutputStream(output); Stream<Path> tree = Files.walk(root)) {
            zip.putNextEntry(new ZipEntry(MARKER)); zip.write(VERSION); zip.closeEntry();
            long total = 0; int count = 0;
            for (Path path : (Iterable<Path>) tree.sorted()::iterator) {
                if (Files.isSymbolicLink(path)) throw new IOException("Save symlinks are not supported");
                if (Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) continue;
                if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) throw new IOException("Invalid save file");
                if (++count > MAX_ENTRIES) throw new IOException("Too many save files");
                String name = "save/" + root.relativize(path).toString().replace(java.io.File.separatorChar, '/');
                safeName(name);
                ZipEntry entry = new ZipEntry(name); entry.setTime(Files.getLastModifiedTime(path).toMillis());
                zip.putNextEntry(entry);
                try (InputStream input = Files.newInputStream(path)) { total += copy(input, zip, MAX_FILE, MAX_TOTAL - total); }
                zip.closeEntry();
            }
            if (count == 0) throw new IOException("No committed save files");
        }
    }

    public static synchronized Path restore(Path files, InputStream input) throws IOException {
        recover(files);
        Path stage = files.resolve("save-import.stage"), previous = files.resolve("save-import.previous"), save = files.resolve("save");
        Path archive = files.resolve("save-import.archive");
        remove(stage); Files.createDirectory(stage);
        try {
            boolean marker = false; int count = 0, entries = 0; long total = 0;
            Set<String> seen = new HashSet<>();
            try (OutputStream output = Files.newOutputStream(archive)) { copy(input, output, MAX_TOTAL + 4194304, MAX_TOTAL + 4194304); }
            try (ZipFile zip = new ZipFile(archive.toFile())) {
                java.util.Enumeration<? extends ZipEntry> contents = zip.entries();
                while (contents.hasMoreElements()) {
                    ZipEntry entry = contents.nextElement();
                    String name = entry.getName();
                    if (++entries > MAX_ENTRIES + 1 || !seen.add(name)) throw new IOException("Too many or duplicate ZIP entries");
                    CRC32 crc = new CRC32();
                    try (InputStream member = new CheckedInputStream(zip.getInputStream(entry), crc)) {
                    if (name.equals(MARKER)) {
                        java.io.ByteArrayOutputStream data = new java.io.ByteArrayOutputStream();
                        copy(member, data, VERSION.length, VERSION.length);
                        if (!java.util.Arrays.equals(data.toByteArray(), VERSION)) throw new IOException("Invalid save backup version");
                        marker = true;
                    } else {
                        safeName(name);
                        Path target = stage.resolve(name.substring(5));
                        if (entry.isDirectory()) { Files.createDirectories(target); continue; }
                        Files.createDirectories(target.getParent());
                        try (OutputStream output = Files.newOutputStream(target, java.nio.file.StandardOpenOption.CREATE_NEW)) {
                            total += copy(member, output, MAX_FILE, MAX_TOTAL - total);
                        }
                        if (entry.getTime() >= 0) Files.setLastModifiedTime(target, java.nio.file.attribute.FileTime.fromMillis(entry.getTime()));
                        count++;
                    }
                    if (crc.getValue() != entry.getCrc()) throw new IOException("Save backup checksum mismatch");
                    }
                }
            }
            if (!marker || count == 0) throw new IOException("Not a D2R save backup");
            Path backup = null;
            if (Files.exists(save, LinkOption.NOFOLLOW_LINKS)) {
                Path backups = files.resolve("save-backups"); Files.createDirectories(backups);
                backup = backups.resolve("before-import-" + UUID.randomUUID() + ".zip");
                try (OutputStream output = Files.newOutputStream(backup, java.nio.file.StandardOpenOption.CREATE_NEW)) { write(files, output); }
            }
            remove(previous);
            if (Files.exists(save, LinkOption.NOFOLLOW_LINKS)) Files.move(save, previous, StandardCopyOption.ATOMIC_MOVE);
            try { Files.move(stage, save, StandardCopyOption.ATOMIC_MOVE); }
            catch (IOException error) { recover(files); throw error; }
            return backup;
        } finally { try { remove(stage); } finally { Files.deleteIfExists(archive); } }
    }

    private static void safeName(String name) throws IOException {
        if (!name.startsWith("save/") || name.length() <= 5 || name.indexOf('\\') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("Invalid archive path");
        String value = name.substring(5);
        if (value.endsWith("/")) value = value.substring(0, value.length() - 1);
        for (String part : value.split("/", -1))
            if (part.isEmpty() || part.equals(".") || part.equals("..") || part.indexOf(':') >= 0)
                throw new IOException("Invalid archive path");
    }

    private static long copy(InputStream input, OutputStream output, long fileLimit, long remaining) throws IOException {
        byte[] bytes = new byte[32768]; long size = 0; int n;
        while ((n = input.read(bytes)) != -1) {
            size += n;
            if (size > fileLimit || size > remaining) throw new IOException("Save backup size limit exceeded");
            output.write(bytes, 0, n);
        }
        return size;
    }

    private static void remove(Path path) throws IOException {
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) return;
        try (Stream<Path> tree = Files.walk(path)) {
            for (Path item : (Iterable<Path>) tree.sorted(java.util.Comparator.reverseOrder())::iterator) Files.delete(item);
        }
    }
}
