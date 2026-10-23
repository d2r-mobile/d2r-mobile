package org.d2r.android.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class OverlayArchiveTest {
    private static int tests;
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static byte[] bytes(String text) { return text.getBytes(StandardCharsets.UTF_8); }
    private static void write(File file, String value) throws IOException {
        Files.createDirectories(file.toPath().getParent()); Files.write(file.toPath(), bytes(value));
    }
    private static String hash(byte[] value) throws Exception {
        StringBuilder out = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(value)) out.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return out.toString();
    }
    private static final PackageArchive.Sink ACTIVE = new PackageArchive.Sink() {
        public boolean cancelled() { return false; }
        public void progress(int percent) { check(percent >= 0 && percent <= 100, "bounded progress"); }
    };
    private static final class Fixture implements AutoCloseable {
        final File root = Files.createTempDirectory("d2r-overlay-").toFile();
        final File assets = new File(root, "assets"), modules = new File(root, "game");
        final File overlay = new File(assets, "overlay");
        Fixture() throws Exception {
            write(new File(assets, "manifest.json"), "original package manifest");
            write(new File(assets, "romfs/data/build_key.txt"), "test-game-build\n");
            write(new File(assets, "shaders/original.bin"), "shader");
            write(new File(root, "save/character.d2s"), "save");
            for (String name : PackageArchive.requiredModules()) write(new File(modules, name),
                    name.endsWith(".target") ? PackageArchive.TARGET_VALUE : "module");
            for (Map.Entry<String, String> entry : contents().entrySet()) write(new File(assets, entry.getKey()), entry.getValue());
            write(new File(overlay, "features.txt"), "old feature marker");
        }
        void original() throws Exception {
            check(Files.readString(new File(overlay, "features.txt").toPath()).equals("old feature marker"), "old overlay retained");
            unaffected();
            check(!new File(assets, "overlay.update").exists(), "failed staging removed");
        }
        void unaffected() throws Exception {
            check(Files.readString(new File(assets, "manifest.json").toPath()).equals("original package manifest"), "package completion preserved");
            check(Files.readString(new File(modules, "libdmguest_main.so").toPath()).equals("module"), "modules preserved");
            check(Files.readString(new File(assets, "shaders/original.bin").toPath()).equals("shader"), "shaders preserved");
            check(Files.readString(new File(root, "save/character.d2s").toPath()).equals("save"), "saves preserved");
            check(OverlayArchive.baseHash(assets).equals(hash(bytes("test-game-build\n"))), "base data preserved");
        }
        public void close() { PackageArchive.deleteTree(root); }
    }
    private static LinkedHashMap<String, String> contents() {
        LinkedHashMap<String, String> files = new LinkedHashMap<>();
        files.put("overlay/ctrl_touch", "1");
        files.put("overlay/touch_strings.tsv", "test\t00000000\ttext\n");
        files.put("overlay/data/.build.config", "build-config=fixture\n");
        files.put("overlay/data/ui.data", "new interface data");
        return files;
    }
    private static String manifest(Map<String, String> files) throws Exception {
        TreeMap<String, String> hashes = new TreeMap<>(); long total = 0;
        for (Map.Entry<String, String> entry : files.entrySet()) { hashes.put(entry.getKey(), hash(bytes(entry.getValue()))); total += bytes(entry.getValue()).length; }
        StringBuilder canonical = new StringBuilder();
        for (Map.Entry<String, String> entry : hashes.entrySet()) canonical.append(entry.getKey()).append('\t').append(entry.getValue()).append('\n');
        return "format=1\nhost_contract=1\nversion=test-v2\nbase_sha256=" + hash(bytes("test-game-build\n"))
                + "\nfiles=" + files.size() + "\nbytes=" + total + "\ncontent_sha256=" + hash(bytes(canonical.toString())) + "\n";
    }
    private static byte[] archive(String manifest, Map<String, String> files) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            zip.putNextEntry(new ZipEntry(OverlayArchive.MANIFEST)); zip.write(bytes(manifest)); zip.closeEntry();
            for (Map.Entry<String, String> entry : files.entrySet()) {
                zip.putNextEntry(new ZipEntry(entry.getKey())); zip.write(bytes(entry.getValue())); zip.closeEntry();
            }
        }
        return out.toByteArray();
    }
    private static void rejected(String metadata, Map<String, String> files, PackageArchive.Sink sink) throws Exception {
        try (Fixture fixture = new Fixture()) {
            try {
                OverlayArchive.install(new ByteArrayInputStream(archive(metadata, files)), fixture.assets, fixture.modules, sink);
                throw new AssertionError("invalid update accepted");
            } catch (IOException expected) { fixture.original(); }
        }
        tests++;
    }
    private static void successfulUpdate() throws Exception {
        try (Fixture fixture = new Fixture()) {
            check(OverlayArchive.version(fixture.assets) == null, "old overlay is unversioned");
            check(OverlayArchive.install(new ByteArrayInputStream(archive(manifest(contents()), contents())), fixture.assets, fixture.modules, ACTIVE).equals("test-v2"), "committed version");
            check(OverlayArchive.version(fixture.assets).equals("test-v2"), "version survives reload");
            check(!new File(fixture.overlay, "features.txt").exists(), "obsolete overlay file retired");
            fixture.unaffected();
        }
        tests++;
    }
    private static void interruptionRecovery(boolean committed) throws Exception {
        try (Fixture fixture = new Fixture()) {
            File previous = new File(fixture.assets, "overlay.replaced");
            check(fixture.overlay.renameTo(previous), "create interrupted state");
            write(new File(fixture.assets, "overlay.update/partial.data"), "partial extraction");
            if (committed) {
                for (Map.Entry<String, String> entry : contents().entrySet()) write(new File(fixture.assets, entry.getKey()), entry.getValue());
                Files.delete(new File(previous, "ctrl_touch").toPath());
            }
            OverlayArchive.recover(fixture.assets);
            check(fixture.overlay.isDirectory() && !previous.exists(), "one committed or restored overlay");
            check(!new File(fixture.assets, "overlay.update").exists(), "interrupted extraction cleaned");
            if (!committed) fixture.original(); else fixture.unaffected();
        }
        tests++;
    }
    private static void unreadableUpdate() throws Exception {
        byte[] archive = archive(manifest(contents()), contents());
        InputStream broken = new ByteArrayInputStream(archive) {
            @Override public synchronized int read(byte[] bytes, int off, int len) {
                if (pos > archive.length / 2) throw new java.io.UncheckedIOException(new IOException("injected read failure"));
                return super.read(bytes, off, Math.min(len, 32));
            }
        };
        try (Fixture fixture = new Fixture()) {
            try { OverlayArchive.install(broken, fixture.assets, fixture.modules, ACTIVE); throw new AssertionError("broken stream accepted"); }
            catch (java.io.UncheckedIOException expected) { fixture.original(); }
        }
        tests++;
    }
    private static void failedRenameRollsBack() throws Exception {
        try (Fixture fixture = new Fixture()) {
            File staging = new File(fixture.assets, "overlay.update") {
                @Override public boolean renameTo(File target) { return false; }
            };
            check(staging.mkdir(), "create staged update");
            try { OverlayArchive.commit(staging, fixture.overlay); throw new AssertionError("failed rename accepted"); }
            catch (IOException expected) {
                check(Files.readString(new File(fixture.overlay, "features.txt").toPath()).equals("old feature marker"), "failed rename restored live overlay");
                check(!new File(fixture.assets, "overlay.replaced").exists(), "rollback consumed backup");
                fixture.unaffected();
            }
        }
        tests++;
    }
    private static void invalidRecoveryPreservesBackup() throws Exception {
        try (Fixture fixture = new Fixture()) {
            File previous = new File(fixture.assets, "overlay.replaced");
            check(fixture.overlay.renameTo(previous), "create interrupted state");
            write(fixture.overlay, "invalid live file");
            try { OverlayArchive.recover(fixture.assets); throw new AssertionError("invalid live target accepted"); }
            catch (IOException expected) {
                check(Files.readString(new File(previous, "features.txt").toPath()).equals("old feature marker"), "sole backup retained");
                check(Files.readString(fixture.overlay.toPath()).equals("invalid live file"), "invalid target retained for diagnosis");
            }
        }
        tests++;
    }
    private static void builderRoundTrip(Path repo) throws Exception {
        try (Fixture fixture = new Fixture()) {
            File source = new File(fixture.root, "source");
            for (Map.Entry<String, String> entry : contents().entrySet()) write(new File(source, entry.getKey().substring(8)), entry.getValue());
            File zip = new File(fixture.root, "interface.zip");
            Process process = new ProcessBuilder("python3", repo.resolve("dm/android/tools/build_overlay_update.py").toString(),
                    source.toString(), new File(fixture.assets, "romfs").toString(), zip.toString(), "python-v3").inheritIO().start();
            check(process.waitFor() == 0, "production Python builder");
            try (InputStream in = Files.newInputStream(zip.toPath())) {
                check(OverlayArchive.install(in, fixture.assets, fixture.modules, ACTIVE).equals("python-v3"), "Python/Java package agreement");
            }
            fixture.unaffected();
        }
        tests++;
    }
    private static void wrongImportEntryPreservesGame() throws Exception {
        try (Fixture fixture = new Fixture()) {
            try {
                PackageArchive.unpack(new ByteArrayInputStream(archive(manifest(contents()), contents())), fixture.assets, fixture.modules, 0, ACTIVE,
                        ignored -> new PackageArchive.Declaration(1, 12, 1, 1));
                throw new AssertionError("interface ZIP accepted as full package");
            } catch (PackageArchive.Failure expected) {
                check(expected.key.equals("dmAndroidInterfaceUseUpdater"), "correct update entry suggested");
                fixture.original();
            }
        }
        tests++;
    }
    private static void duplicateEntriesRejected() throws Exception {
        Map<String, String> files = contents(); files.put("overlay/ctrl_touci", "duplicate marker");
        byte[] zip = new String(archive(manifest(files), files), StandardCharsets.ISO_8859_1)
                .replace("overlay/ctrl_touci", "overlay/ctrl_touch").getBytes(StandardCharsets.ISO_8859_1);
        try (Fixture fixture = new Fixture()) {
            try { OverlayArchive.install(new ByteArrayInputStream(zip), fixture.assets, fixture.modules, ACTIVE); throw new AssertionError("duplicate entry accepted"); }
            catch (IOException expected) { fixture.original(); }
        }
        tests++;
    }
    private static void bundledUpgrade() throws Exception {
        try (Fixture fixture = new Fixture()) {
            Map<String, String> files = contents();
            byte[] zip = archive(manifest(files), files);
            check(OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets), "legacy overlay upgrades from APK");
            OverlayArchive.install(new ByteArrayInputStream(zip), fixture.assets, fixture.modules, ACTIVE);
            check(!OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets), "same bundled interface is not rewritten");
            check(OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(archive(manifest(files).replace("test-v2", "test-v1"), files)), fixture.assets), "older APK restores its matching interface");
            files.put("overlay/data/ui.data", "updated APK interface with same version");
            zip = archive(manifest(files), files);
            check(OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets), "content change detected even at same version");
            OverlayArchive.install(new ByteArrayInputStream(zip), fixture.assets, fixture.modules, ACTIVE);
            check(!OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets), "updated content installed");
            PackageArchive.deleteTree(fixture.overlay);
            check(OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets), "first game-only import receives APK interface");
            OverlayArchive.install(new ByteArrayInputStream(zip), fixture.assets, fixture.modules, ACTIVE);
            fixture.unaffected();
            write(new File(fixture.overlay, OverlayArchive.MANIFEST), "damaged metadata");
            check(OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets), "damaged metadata can be repaired from APK");
            OverlayArchive.install(new ByteArrayInputStream(zip), fixture.assets, fixture.modules, ACTIVE);
            write(new File(fixture.assets, "romfs/data/build_key.txt"), "different game");
            try { OverlayArchive.bundledNeedsUpdate(new ByteArrayInputStream(zip), fixture.assets); throw new AssertionError("wrong game accepted"); }
            catch (IOException expected) { check(OverlayArchive.version(fixture.assets).equals("test-v2"), "wrong game leaves overlay intact"); }
        }
        tests++;
    }
    public static void main(String[] args) throws Exception {
        bundledUpgrade();
        successfulUpdate();
        String valid = manifest(contents());
        rejected(valid.replace("format=1", "format=2"), contents(), ACTIVE);
        rejected(valid.replace("host_contract=1", "host_contract=2"), contents(), ACTIVE);
        rejected(valid.replace(hash(bytes("test-game-build\n")), "0".repeat(64)), contents(), ACTIVE);
        rejected(valid + "version=duplicate\n", contents(), ACTIVE);
        rejected(valid.replace("files=4", "files=5"), contents(), ACTIVE);
        Map<String, String> corrupted = contents(); corrupted.put("overlay/data/ui.data", "new interface dAta");
        rejected(valid, corrupted, ACTIVE);
        Map<String, String> bad = contents(); bad.put("save/character.d2s", "overwrite");
        rejected(manifest(bad), bad, ACTIVE);
        bad = contents(); bad.put("overlay/../manifest.json", "overwrite");
        rejected(manifest(bad), bad, ACTIVE);
        bad = contents(); bad.remove("overlay/data/ui.data");
        rejected(valid, bad, ACTIVE);
        bad = contents(); bad.put("overlay/touch_strings.tsv", "test\t00000000\t" + "x".repeat(4096) + "\n");
        rejected(manifest(bad), bad, ACTIVE);
        rejected(valid, contents(), new PackageArchive.Sink() {
            public boolean cancelled() { return true; }
            public void progress(int percent) { }
        });
        rejected(valid, contents(), new PackageArchive.Sink() {
            private boolean cancel;
            public boolean cancelled() { return cancel; }
            public void progress(int percent) { cancel = true; }
        });
        interruptionRecovery(false); interruptionRecovery(true); unreadableUpdate();
        failedRenameRollsBack(); invalidRecoveryPreservesBackup();
        wrongImportEntryPreservesGame();
        duplicateEntriesRejected();
        if (args.length == 1) builderRoundTrip(Path.of(args[0]));
        System.out.println("PASS OverlayArchiveTest " + tests);
    }
}
