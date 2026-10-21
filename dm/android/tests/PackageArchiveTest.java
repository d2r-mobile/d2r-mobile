package org.d2r.android.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// #245 owned archive contract fixture: synthetic bytes only, no game data.
public final class PackageArchiveTest {
    private static int failures;

    private static void check(boolean ok, String what) {
        if (!ok) { failures++; System.err.println("FAIL: " + what); }
    }

    private static byte[] archive(boolean includeMainTarget, String mainTarget) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            entry(zip, "manifest.json", "{}".getBytes(StandardCharsets.UTF_8));
            for (String module : PackageArchive.MODULE_NAMES) {
                entry(zip, "game/libdmguest_" + module + ".so", new byte[]{1});
                entry(zip, "game/" + module + ".fixups.tsv", new byte[]{'#'});
                entry(zip, "game/" + module + ".gates.tsv", new byte[0]);
                if (!module.equals("main") || includeMainTarget)
                    entry(zip, "game/" + module + ".target", mainTarget.getBytes(StandardCharsets.US_ASCII));
            }
            entry(zip, "romfs/a", new byte[]{1});
            entry(zip, "shaders/a", new byte[]{1});
        }
        return bytes.toByteArray();
    }

    private static void entry(ZipOutputStream zip, String name, byte[] value) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(value);
        zip.closeEntry();
    }

    private static PackageArchive.Counts unpack(byte[] archive, File root, boolean oldInstall) throws IOException {
        File assets = new File(root, "assets");
        File game = new File(root, "game");
        assets.mkdirs();
        game.mkdirs();
        if (oldInstall) try (FileOutputStream out = new FileOutputStream(new File(game, "old.sentinel"))) {
            out.write('x');
        }
        return PackageArchive.unpack(new ByteArrayInputStream(archive), assets, game, 0,
            new PackageArchive.Sink() { public void progress(int percent) { } public boolean cancelled() { return false; } },
            manifest -> new PackageArchive.Declaration(1, 12, 1, 1));
    }

    private static void expectFailure(byte[] archive, String key) throws IOException {
        File root = Files.createTempDirectory("package-archive-test").toFile();
        try {
            unpack(archive, root, true);
            check(false, "expected failure " + key);
        } catch (PackageArchive.Failure failure) {
            check(key.equals(failure.key), "expected " + key + " but got " + failure.key);
            check(new File(root, "game/old.sentinel").isFile(), "failed import must preserve previous modules");
            check(!new File(root, "game.import").exists(), "failed import must leave no staging tree");
        }
    }

    private static File readyFixture(String marker) throws IOException {
        File root = Files.createTempDirectory("package-ready-test").toFile();
        File assets = new File(root, "assets");
        File game = new File(root, "game");
        assets.mkdirs();
        game.mkdirs();
        try (FileOutputStream out = new FileOutputStream(new File(assets, "manifest.json"))) { out.write('x'); }
        for (String module : PackageArchive.MODULE_NAMES) {
            try (FileOutputStream out = new FileOutputStream(new File(game, "libdmguest_" + module + ".so"))) { out.write(1); }
            for (String kind : PackageArchive.LAYOUT_KINDS)
                try (FileOutputStream out = new FileOutputStream(new File(game, module + "." + kind + ".tsv"))) { }
            try (FileOutputStream out = new FileOutputStream(new File(game, module + ".target"))) {
                out.write(marker.getBytes(StandardCharsets.US_ASCII));
            }
        }
        return root;
    }

    private static void expectReady(File root, boolean expected, String what) {
        boolean ready = PackageArchive.isReady(new File(root, "assets"), new File(root, "game"));
        check(ready == expected, what + " (ready=" + ready + ")");
        check(new File(root, "assets/manifest.json").isFile(), what + ": readiness must not delete manifest");
    }

    public static void main(String[] args) throws Exception {
        check(PackageArchive.requiredModules().size() == 12, "requiredModules must be twelve entries");
        check(PackageArchive.requiredModules().contains("main.target"), "main.target required");
        check(PackageArchive.requiredModules().contains("sdk.target"), "sdk.target required");
        check(PackageArchive.requiredModules().contains("subsdk0.target"), "subsdk0.target required");

        File validRoot = Files.createTempDirectory("package-archive-valid").toFile();
        PackageArchive.Counts counts = unpack(archive(true, "android-context-v1\n"), validRoot, false);
        check(counts.game == 12, "valid package game count");
        for (String module : PackageArchive.MODULE_NAMES)
            check(new File(validRoot, "game/" + module + ".target").isFile(), "valid target installed: " + module);
        check(new File(validRoot, "assets/manifest.json").isFile(), "valid manifest committed");

        expectFailure(archive(false, "android-context-v1\n"), "dmAndroidLaunchImportIncomplete");
        expectFailure(archive(true, "apple-context-v1\n"), "dmAndroidLaunchImportBadEntry");
        expectFailure(archive(true, ""), "dmAndroidLaunchImportBadEntry");
        expectFailure(archive(true, "android-context-v1\njunk"), "dmAndroidLaunchImportBadEntry");

        // Installed-package readiness (no import transaction): old/missing/wrong/malformed must be not ready,
        // and the check must not delete the previous manifest or data.
        File ready = readyFixture("android-context-v1\n");
        expectReady(ready, true, "valid installed package");
        File missing = readyFixture("android-context-v1");
        check(new File(missing, "game/main.target").delete(), "fixture remove main.target");
        expectReady(missing, false, "pre-upgrade package without main.target");
        File wrong = readyFixture("apple-context-v1\n");
        expectReady(wrong, false, "installed wrong target");
        check(new File(wrong, "game/sdk.target").isFile(), "wrong target file must remain after readiness check");
        File malformed = readyFixture("");
        expectReady(malformed, false, "installed malformed/empty target");
        File legacy = readyFixture("android-context-v1\n");
        for (String module : PackageArchive.MODULE_NAMES)
            check(new File(legacy, "game/" + module + ".target").delete(), "fixture remove legacy targets");
        expectReady(legacy, false, "legacy package with nine entries only");

        if (failures != 0) throw new AssertionError(failures + " archive contract failures");
        System.out.println("PASS: Java package requires and validates main/sdk/subsdk0.target; rollback preserved");
    }
}
