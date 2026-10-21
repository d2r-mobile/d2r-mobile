package org.d2r.android.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Driver ZIPs stay in app-private executable storage, separate from game data. */
final class DriverArchive {
    static final class Info {
        final String name, library;
        final int minApi, driverId;
        Info(String name, String library, int minApi) throws IOException {
            this(name, library, minApi, 18);
        }
        Info(String name, String library, int minApi, int driverId) throws IOException {
            if (name == null || name.trim().isEmpty() || name.length() > 120 ||
                    name.matches("(?s).*\\p{Cntrl}.*") || !basename(library) ||
                    !library.endsWith(".so") || minApi < 1 || (driverId != 8 && driverId != 18))
                throw new IOException("Invalid driver metadata");
            this.name = name; this.library = library; this.minApi = minApi; this.driverId = driverId;
        }
    }
    interface MetadataReader { Info read(byte[] bytes) throws IOException; }
    private static boolean basename(String name) {
        return name != null && name.matches("[A-Za-z0-9_@+.-]+") &&
               !name.equals(".") && !name.equals("..");
    }
    static void elf(File file) throws IOException {
        byte[] h = new byte[20];
        try (InputStream in = new FileInputStream(file)) {
            int n = 0, got;
            while (n < h.length && (got = in.read(h, n, h.length - n)) > 0) n += got;
            if (n != h.length || h[0] != 127 || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
                    h[4] != 2 || h[5] != 1 || h[6] != 1 || h[16] != 3 || h[17] != 0 ||
                    (h[18] & 255) != 183 || h[19] != 0)
                throw new IOException("Driver must contain Android ARM64 shared libraries");
        }
    }
    static Info installed(File directory, MetadataReader reader, int api) throws IOException {
        File meta = new File(directory, "meta.json");
        byte[] data;
        try (InputStream in = new FileInputStream(meta)) { data = small(in); }
        Info info = reader.read(data);
        if (info.minApi > api) throw new IOException("Driver requires Android API " + info.minApi);
        elf(new File(directory, info.library));
        return info;
    }
    private static byte[] small(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[4096]; int n;
        while ((n = in.read(buf)) != -1) {
            if (out.size() + n > 65536) throw new IOException("Driver metadata too large");
            out.write(buf, 0, n);
        }
        return out.toByteArray();
    }
    static void remove(File directory) throws IOException {
        if (!directory.exists()) return;
        File[] files = directory.listFiles();
        if (files == null) throw new IOException("Cannot read imported driver directory");
        for (File file : files) {
            if (!file.isFile() || !file.delete()) throw new IOException("Cannot delete imported driver: " + file.getName());
        }
        if (!directory.delete()) throw new IOException("Cannot delete imported driver directory");
    }
    static Info install(InputStream input, File destination, MetadataReader reader, int api) throws IOException {
        File staging = new File(destination.getParentFile(), destination.getName() + ".import");
        SaveArchive.deleteTree(staging);
        if (!staging.mkdirs()) throw new IOException("Cannot create driver staging directory");
        try {
            Set<String> seen = new HashSet<>(); long total = 0;
            try (ZipInputStream zip = new ZipInputStream(input)) {
                ZipEntry entry; byte[] buf = new byte[65536];
                while ((entry = zip.getNextEntry()) != null) {
                    String name = entry.getName();
                    if (seen.size() >= 128 || !basename(name) || !seen.add(name) || entry.isDirectory() ||
                            !(name.equals("meta.json") || name.endsWith(".so")))
                        throw new IOException("Unsupported driver ZIP entry: " + name);
                    File target = new File(staging, name); long size = 0; int n;
                    try (FileOutputStream out = new FileOutputStream(target)) {
                        while ((n = zip.read(buf)) != -1) {
                            size += n; total += n;
                            if (size > (128L << 20) || total > (256L << 20) ||
                                    (name.equals("meta.json") && size > 65536))
                                throw new IOException("Driver ZIP exceeds size limit");
                            out.write(buf, 0, n);
                        }
                    }
                    if (name.endsWith(".so")) elf(target);
                }
            }
            Info info = installed(staging, reader, api);
            SaveArchive.replace(destination, staging);
            return info;
        } finally { SaveArchive.deleteTree(staging); }
    }
}
