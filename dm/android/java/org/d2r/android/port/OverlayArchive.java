package org.d2r.android.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class OverlayArchive {
    static final String BUNDLED_ASSET = "interface.zip";
    static final String MANIFEST = PackageArchive.OVERLAY_UPDATE_MANIFEST;
    private static final long MAX_BYTES = 2L << 30;
    private static final int MAX_FILES = 64;
    private static final String[] KEYS = {"format", "host_contract", "version", "base_sha256", "files", "bytes", "content_sha256"};

    private OverlayArchive() { }

    static final class Manifest {
        final String version, baseHash, contentHash;
        final int files;
        final long bytes;

        Manifest(byte[] raw) throws IOException {
            Map<String, String> fields = new HashMap<>();
            for (String line : new String(raw, StandardCharsets.UTF_8).split("\n")) {
                if (line.isEmpty()) continue;
                int eq = line.indexOf('=');
                if (eq <= 0) throw new IOException("Invalid overlay manifest line");
                String key = line.substring(0, eq), value = line.substring(eq + 1);
                boolean known = false;
                for (String allowed : KEYS) if (allowed.equals(key)) known = true;
                if (!known || fields.put(key, value) != null) throw new IOException("Unknown or duplicate overlay manifest key: " + key);
            }
            if (fields.size() != KEYS.length || !"1".equals(fields.get("format")) || !"1".equals(fields.get("host_contract")))
                throw new IOException("Unsupported overlay format or host contract");
            version = fields.get("version"); baseHash = fields.get("base_sha256"); contentHash = fields.get("content_sha256");
            if (!version.matches("[A-Za-z0-9][A-Za-z0-9._-]{0,79}") || !baseHash.matches("[0-9a-f]{64}") || !contentHash.matches("[0-9a-f]{64}"))
                throw new IOException("Invalid overlay version or checksum");
            try { files = Integer.parseInt(fields.get("files")); bytes = Long.parseLong(fields.get("bytes")); }
            catch (NumberFormatException error) { throw new IOException("Invalid overlay size", error); }
            if (files < 4 || files > MAX_FILES || bytes < 1 || bytes > MAX_BYTES) throw new IOException("Overlay exceeds size limits");
        }
    }

    private static MessageDigest digest() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException error) { throw new IllegalStateException(error); }
    }

    private static String hex(byte[] bytes) {
        StringBuilder value = new StringBuilder();
        for (byte b : bytes) value.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return value.toString();
    }

    static String baseHash(File assets) throws IOException {
        try (InputStream in = new FileInputStream(new File(assets, "romfs/data/build_key.txt"))) {
            byte[] key = read(in, 4096);
            if (key.length == 0) throw new IOException("Game build key is empty");
            return hex(digest().digest(key));
        }
    }

    private static byte[] read(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096]; int n;
        while ((n = in.read(buffer)) != -1) {
            if (out.size() + n > limit) throw new IOException("Overlay metadata exceeds size limit");
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }

    static synchronized String version(File assets) throws IOException {
        File file = new File(new File(assets, PackageArchive.OVERLAY), MANIFEST);
        if (!file.exists()) return null;
        try (InputStream in = new FileInputStream(file)) { return new Manifest(read(in, 16384)).version; }
    }

    private static byte[] readManifest(ZipInputStream zip) throws IOException {
        ZipEntry entry = zip.getNextEntry();
        if (entry == null || entry.isDirectory() || !MANIFEST.equals(entry.getName()))
            throw new IOException("Not an interface update ZIP");
        return read(zip, 16384);
    }

    static synchronized boolean bundledNeedsUpdate(InputStream raw, File assets) throws IOException {
        Manifest bundled;
        try (ZipInputStream zip = new ZipInputStream(raw)) { bundled = new Manifest(readManifest(zip)); }
        if (!bundled.baseHash.equals(baseHash(assets))) throw new IOException("APK interface is for different game data");
        File installed = new File(new File(assets, PackageArchive.OVERLAY), MANIFEST);
        if (!installed.isFile()) return true;
        try (InputStream in = new FileInputStream(installed)) {
            Manifest current = new Manifest(read(in, 16384));
            return !bundled.version.equals(current.version) || !bundled.baseHash.equals(current.baseHash)
                    || !bundled.contentHash.equals(current.contentHash);
        } catch (IOException invalidInstalledMetadata) { return true; }
    }

    static synchronized void recover(File assets) throws IOException {
        File target = new File(assets, PackageArchive.OVERLAY), previous = new File(assets, "overlay.replaced");
        if (previous.exists()) {
            if (!target.exists()) {
                PackageArchive.validateOverlay(previous);
                if (!previous.renameTo(target)) throw new IOException("Cannot restore previous overlay");
            } else {
                PackageArchive.validateOverlay(target);
                remove(previous);
            }
        }
        remove(new File(assets, "overlay.update"));
    }

    static synchronized String install(InputStream raw, File assets, File modules, PackageArchive.Sink sink) throws IOException {
        recover(assets);
        if (!PackageArchive.isReady(assets, modules)) throw new IOException("Import the game data before updating its interface");
        File target = new File(assets, PackageArchive.OVERLAY), staging = new File(assets, "overlay.update");
        remove(staging);
        if (!staging.mkdir()) throw new IOException("Cannot create overlay staging directory");
        try {
            Manifest manifest;
            byte[] manifestBytes;
            TreeMap<String, String> hashes = new TreeMap<>();
            Set<String> names = new HashSet<>();
            long total = 0;
            try (ZipInputStream zip = new ZipInputStream(raw)) {
                manifestBytes = readManifest(zip);
                manifest = new Manifest(manifestBytes);
                if (!manifest.baseHash.equals(baseHash(assets))) throw new IOException("Interface update is for different game data");
                byte[] buffer = new byte[65536];
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (sink.cancelled()) throw new IOException("Interface update cancelled");
                    String name = entry.getName();
                    if (entry.isDirectory() || !PackageArchive.clean(name) || !name.startsWith("overlay/") || name.equals("overlay/" + MANIFEST)
                            || !names.add(name) || names.size() > MAX_FILES) throw new IOException("Invalid interface update entry: " + name);
                    File file = new File(staging, name.substring(8));
                    if (!file.getCanonicalPath().startsWith(staging.getCanonicalPath() + File.separator)) throw new IOException("Unsafe overlay path");
                    File parent = file.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create overlay directory");
                    MessageDigest hash = digest();
                    long entryBytes = 0;
                    long entryLimit = name.startsWith("overlay/data/") && name.endsWith(".data") ? MAX_BYTES : 16L << 20;
                    try (FileOutputStream out = new FileOutputStream(file)) {
                        int n;
                        while ((n = zip.read(buffer)) != -1) {
                            if (sink.cancelled()) throw new IOException("Interface update cancelled");
                            total += n;
                            entryBytes += n;
                            if (total > manifest.bytes || total > MAX_BYTES || entryBytes > entryLimit) throw new IOException("Overlay exceeds declared size");
                            out.write(buffer, 0, n); hash.update(buffer, 0, n);
                        }
                        out.getFD().sync();
                    }
                    hashes.put(name, hex(hash.digest()));
                    sink.progress((int)(total * 100 / manifest.bytes));
                }
            }
            MessageDigest content = digest();
            for (Map.Entry<String, String> entry : hashes.entrySet())
                content.update((entry.getKey() + "\t" + entry.getValue() + "\n").getBytes(StandardCharsets.UTF_8));
            if (total != manifest.bytes || hashes.size() != manifest.files || !hex(content.digest()).equals(manifest.contentHash))
                throw new IOException("Interface update checksum or file count mismatch");
            PackageArchive.validateOverlay(staging);
            try (FileOutputStream out = new FileOutputStream(new File(staging, MANIFEST))) { out.write(manifestBytes); out.getFD().sync(); }
            if (sink.cancelled()) throw new IOException("Interface update cancelled");
            commit(staging, target);
            return manifest.version;
        } finally {
            remove(staging);
        }
    }

    static void commit(File staging, File target) throws IOException {
        File previous = new File(target.getParentFile(), "overlay.replaced");
        if (previous.exists()) throw new IOException("Previous overlay needs recovery before updating");
        boolean hadPrevious = target.exists();
        if (hadPrevious && !target.renameTo(previous)) throw new IOException("Cannot preserve previous overlay");
        if (!staging.renameTo(target)) {
            if (hadPrevious && !previous.renameTo(target)) throw new IOException("Cannot install overlay; previous version retained for recovery");
            throw new IOException("Cannot install interface update");
        }
        // A completed directory rename commits the update; cleanup can resume on the next launch.
        try { remove(previous); } catch (IOException ignored) { }
    }

    private static void remove(File file) throws IOException {
        if (file.isDirectory() && !Files.isSymbolicLink(file.toPath())) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot list overlay staging directory");
            for (File child : children) remove(child);
        }
        Files.deleteIfExists(file.toPath());
    }
}
