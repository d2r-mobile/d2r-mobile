package com.xiaoboluo.d2r;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

public final class SaveArchiveTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    private static byte[] zip(String... names) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            for (String name : names) {
                zip.putNextEntry(new ZipEntry(name));
                zip.write((name.equals("d2r-save-backup.txt") ? "D2R_SAVE_BACKUP_V1\n" : "test").getBytes(java.nio.charset.StandardCharsets.UTF_8));
                zip.closeEntry();
            }
        }
        return out.toByteArray();
    }
    private static void rejected(Path target, byte[] archive) throws IOException {
        try { SaveArchive.restore(target, new ByteArrayInputStream(archive)); throw new AssertionError("Accepted invalid archive"); }
        catch (IOException expected) { check(Files.readString(target.resolve("save/hero.d2s")).equals("original")); }
        check(!Files.exists(target.resolve("save-import.stage")));
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("save-archive-test");
        try {
            Path source = root.resolve("source"), target = root.resolve("target");
            Files.createDirectories(source.resolve("save/nested")); Files.createDirectories(target.resolve("save"));
            Files.writeString(source.resolve("save/hero.d2s"), "hero");
            Files.writeString(source.resolve("save/nested/shared.d2i"), "stash");
            Files.writeString(source.resolve("save/settings.json"), "settings");
            Files.createDirectories(source.resolve("save.work")); Files.writeString(source.resolve("save.work/uncommitted"), "exclude");
            ByteArrayOutputStream output = new ByteArrayOutputStream(); SaveArchive.write(source, output);
            byte[] good = output.toByteArray();
            Files.writeString(target.resolve("save/hero.d2s"), "original");
            for (String bad : List.of("save/../outside", "/save/hero", "save/a\\b", "save/a/../../outside", "save//file", "other/file", "save/C:/file"))
                rejected(target, zip("d2r-save-backup.txt", bad));
            rejected(target, zip("save/hero.d2s"));
            rejected(target, zip("d2r-save-backup.txt"));
            rejected(target, new byte[]{1,2,3});
            rejected(target, Arrays.copyOf(good, good.length / 3));
            rejected(target, Arrays.copyOf(good, good.length - 22));
            byte[] duplicate = zip("d2r-save-backup.txt", "save/fileA", "save/fileB");
            byte[] from = "save/fileB".getBytes(java.nio.charset.StandardCharsets.UTF_8);
            for (int i = 0; i <= duplicate.length - from.length; i++) {
                if (Arrays.equals(Arrays.copyOfRange(duplicate, i, i + from.length), from)) duplicate[i + from.length - 1] = 'A';
            }
            rejected(target, duplicate);
            try {
                SaveArchive.restore(target, new FilterInputStream(new ByteArrayInputStream(good)) {
                    private int remaining = 90;
                    @Override public int read(byte[] bytes, int off, int len) throws IOException {
                        if (remaining <= 0) throw new IOException("Injected read failure");
                        int n = super.read(bytes, off, Math.min(len, remaining));
                        if (n > 0) remaining -= n;
                        return n;
                    }
                });
                throw new AssertionError("Accepted failed read");
            } catch (IOException expected) {
                check(Files.readString(target.resolve("save/hero.d2s")).equals("original"));
                check(!Files.exists(target.resolve("save-import.stage")));
            }
            String[] many = new String[4098]; many[0] = "d2r-save-backup.txt";
            for (int i = 1; i < many.length; i++) many[i] = "save/file" + i;
            rejected(target, zip(many));
            Path backup = SaveArchive.restore(target, new ByteArrayInputStream(good));
            check(backup != null && Files.isRegularFile(backup));
            check(Files.readString(target.resolve("save/hero.d2s")).equals("hero"));
            check(Files.readString(target.resolve("save/nested/shared.d2i")).equals("stash"));
            check(Files.readString(target.resolve("save/settings.json")).equals("settings"));
            check(!Files.exists(target.resolve("save/uncommitted")));
            Path restored = root.resolve("restored"); Files.createDirectory(restored);
            check(SaveArchive.restore(restored, Files.newInputStream(backup)) == null);
            check(Files.readString(restored.resolve("save/hero.d2s")).equals("original"));
            Files.move(restored.resolve("save"), restored.resolve("save-import.previous"));
            SaveArchive.recover(restored);
            check(Files.readString(restored.resolve("save/hero.d2s")).equals("original"));
            Files.createSymbolicLink(source.resolve("save/link"), target.resolve("save/hero.d2s"));
            try { SaveArchive.write(source, new ByteArrayOutputStream()); throw new AssertionError("Exported symlink"); }
            catch (IOException expected) { }
            System.out.println("SaveArchiveTest: PASS (round trip, rollback backup, interrupted swap, unsafe paths, limits, truncation, symlinks)");
        } finally {
            try (java.util.stream.Stream<Path> paths = Files.walk(root)) {
                for (Path path : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) Files.delete(path);
            }
        }
    }
}
