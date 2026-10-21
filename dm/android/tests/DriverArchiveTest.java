package org.d2r.android.port;

import java.io.*;
import java.nio.file.Files;
import java.util.zip.*;

public final class DriverArchiveTest {
    static byte[] elf() {
        byte[] b = new byte[64]; b[0]=127;b[1]='E';b[2]='L';b[3]='F';
        b[4]=2;b[5]=1;b[6]=1;b[16]=3;b[18]=(byte)183; return b;
    }
    static byte[] zip(String[] names, byte[][] data) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(out)) {
            for (int i=0;i<names.length;i++) {
                z.putNextEntry(new ZipEntry(names[i]));z.write(data[i]);z.closeEntry();
            }
        }
        return out.toByteArray();
    }
    static final DriverArchive.MetadataReader READER = bytes ->
        new DriverArchive.Info("Fixture", "vulkan.adreno.so", bytes[0]);
    static void install(byte[] zip, File target) throws IOException {
        DriverArchive.install(new ByteArrayInputStream(zip),target,READER,35);
    }
    static void rejected(byte[] zip, File target) throws IOException {
        try { install(zip,target);throw new AssertionError("invalid archive accepted"); }
        catch (IOException expected) { }
        if (!new File(target,"libdependency.so").isFile())
            throw new AssertionError("failed import destroyed previous driver");
    }
    public static void main(String[] args) throws Exception {
        File root=Files.createTempDirectory("driver-archive-").toFile();
        File target=new File(root,"custom");
        try {
            install(zip(new String[]{"meta.json","vulkan.adreno.so","libdependency.so"},
                new byte[][]{{27},elf(),elf()}),target);
            DriverArchive.Info info=DriverArchive.installed(target,READER,35);
            if (!info.library.equals("vulkan.adreno.so")) throw new AssertionError();
            rejected(zip(new String[]{"../outside.so"},new byte[][]{elf()}),target);
            rejected(zip(new String[]{"meta.json"},new byte[][]{{27}}),target);
            rejected(zip(new String[]{"meta.json","vulkan.adreno.so"},new byte[][]{{36},elf()}),target);
            byte[] wrong=elf();wrong[18]=62;
            rejected(zip(new String[]{"meta.json","vulkan.adreno.so"},new byte[][]{{27},wrong}),target);
            rejected(zip(new String[]{"meta.json","vulkan.adreno.so"},new byte[][]{{27},new byte[2]}),target);
            rejected(zip(new String[]{"meta.json","vulkan.adreno.so"},new byte[][]{new byte[65537],elf()}),target);
            try { new DriverArchive.Info("bad\nname","vulkan.so",27); throw new AssertionError(); }
            catch(IOException expected) { }
            try { new DriverArchive.Info("bad","../vulkan.so",27); throw new AssertionError(); }
            catch(IOException expected) { }
            DriverArchive.remove(target);
            if (target.exists()) throw new AssertionError("driver directory remains after delete");
            DriverArchive.remove(target);
            System.out.println("PASS driver ZIP: dependencies, ARM64/API/metadata/path validation, failed-import preservation");
            if (args.length > 0) {
                try (InputStream in=new FileInputStream(args[0])) {
                    DriverArchive.install(in,new File(root,"real"), bytes ->
                        new DriverArchive.Info("Qualcomm Adreno 842.8","vulkan.adreno.so",27),35);
                }
                System.out.println("PASS real GameHub842.8 ZIP extraction and every ELF validation");
            }
        } finally { SaveArchive.deleteTree(root); }
    }
}
