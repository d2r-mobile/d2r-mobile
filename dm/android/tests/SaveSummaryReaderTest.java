package com.xiaoboluo.d2r.widget;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Comparator;

/** 仅生成合成存档，绝不访问玩家目录。 | Generates synthetic saves only; never reads a player's directory. */
public final class SaveSummaryReaderTest {
    private static int checks;
    private static void check(boolean value,String label) { ++checks; if (!value) throw new AssertionError(label); }
    private static void sign(byte[] bytes) {
        ByteBuffer data=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN); data.putInt(8,bytes.length); data.putInt(12,0);
        int sum=0; for (byte b:bytes) sum=Integer.rotateLeft(sum,1)+Byte.toUnsignedInt(b);
        data.putInt(12,sum);
    }
    private static int bits(byte[] bytes,int offset,int width,long value) {
        for (int i=0;i<width;++i) if ((value&(1L<<i))!=0) bytes[(offset+i)/8]|=(byte)(1<<((offset+i)%8));
        return offset+width;
    }
    private static byte[] save(String name,long played) {
        byte[] bytes=new byte[1024]; ByteBuffer data=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        data.putInt(0,0xaa55aa55); data.putInt(4,0x69); data.putInt(0x20,(int)played);
        bytes[0x18]=1; bytes[0x1b]=42;
        byte[] utf8=name.getBytes(StandardCharsets.UTF_8); System.arraycopy(utf8,0,bytes,0x12b,Math.min(47,utf8.length));
        bytes[0x180]=0x67; bytes[0x181]=0x66; int p=0x182*8;
        p=bits(bytes,p,9,12); p=bits(bytes,p,7,42);
        p=bits(bytes,p,9,13); p=bits(bytes,p,32,4_000_000_000L);
        p=bits(bytes,p,9,14); p=bits(bytes,p,25,12345);
        p=bits(bytes,p,9,15); p=bits(bytes,p,25,54321);
        bits(bytes,p,9,0x1ff); sign(bytes); return bytes;
    }
    public static void main(String[] args) throws Exception {
        byte[] valid=save("法师gf",4_100_000_000L), original=valid.clone();
        SaveSummaryReader.Hero hero=SaveSummaryReader.parse(valid);
        check(hero!=null,"valid v0x69"); check(hero.name.equals("法师gf"),"strict UTF-8 and gf inside name");
        check(hero.level==42 && hero.classIndex==1,"header fields"); check(hero.lastPlayed==4_100_000_000L,"unsigned timestamp");
        check(hero.experience==4_000_000_000L,"unsigned 32-bit experience"); check(hero.gold==66666,"carried plus bank gold");
        check(Arrays.equals(original,valid),"parser never mutates input");
        byte[] bad=valid.clone(); bad[4]=0x61; sign(bad); check(SaveSummaryReader.parse(bad)!=null,"supported v0x61");
        bad[4]=0x60; sign(bad); check(SaveSummaryReader.parse(bad)==null,"unknown version rejected");
        bad=valid.clone(); bad[100]^=1; check(SaveSummaryReader.parse(bad)==null,"checksum mismatch rejected");
        bad=valid.clone(); bad[8]=1; check(SaveSummaryReader.parse(bad)==null,"length mismatch rejected");
        check(SaveSummaryReader.parse(new byte[10])==null,"short header rejected");
        bad=valid.clone(); bad[0x12b]=(byte)0xff; sign(bad); check(SaveSummaryReader.parse(bad)==null,"malformed UTF-8 rejected");
        bad=valid.clone(); bad[0x12b]='\n'; sign(bad); check(SaveSummaryReader.parse(bad)==null,"control character rejected");
        bad=valid.clone(); bad[0x18]=8; sign(bad); check(SaveSummaryReader.parse(bad)==null,"unknown class rejected");
        bad=valid.clone(); bad[0x1b]=0; sign(bad); check(SaveSummaryReader.parse(bad)==null,"invalid level rejected");
        bad=valid.clone(); bad[0x180]=0; sign(bad); hero=SaveSummaryReader.parse(bad);
        check(hero!=null && hero.experience==-1 && hero.gold==-1,"missing stats remain unknown");
        bad=valid.clone(); bad[0x182]=(byte)0xff; bad[0x183]=(byte)0xff; sign(bad); hero=SaveSummaryReader.parse(bad);
        check(hero!=null && hero.gold==-1,"invalid stats never report invented gold");
        Path temp=Files.createTempDirectory("d2r-widget-synthetic-");
        try {
            Path root=temp.resolve("save");
            check(SaveSummaryReader.read(root).state==SaveSummaryReader.State.EMPTY,"absent directory is empty");
            Files.createDirectory(root); check(SaveSummaryReader.read(root).state==SaveSummaryReader.State.EMPTY,"empty directory");
            Path first=Files.createDirectory(root.resolve("First")); Path file=first.resolve("First.d2s"); Files.write(file,valid);
            Path second=Files.createDirectory(root.resolve("Second")); Files.write(second.resolve("Second.d2s"),save("Older",100));
            SaveSummaryReader.Result result=SaveSummaryReader.read(root);
            check(result.state==SaveSummaryReader.State.READY && result.heroes.size()==2,"two valid saves");
            check(result.heroes.get(0).name.equals("法师gf"),"newest first");
            check(Arrays.equals(Files.readAllBytes(file),valid),"reader never writes saves");
            Path broken=Files.createDirectory(root.resolve("Broken")); Files.write(broken.resolve("Broken.d2s"),new byte[400]);
            result=SaveSummaryReader.read(root); check(result.state==SaveSummaryReader.State.PARTIAL && result.rejectedFiles==1,"mixed validity reported");
            check(SaveSummaryReader.read(temp.resolve("save.work")).state==SaveSummaryReader.State.ERROR,"working directory rejected");
            Files.delete(file); Files.delete(second.resolve("Second.d2s"));
            check(SaveSummaryReader.read(root).state==SaveSummaryReader.State.ERROR,"only invalid saves is an error");
            boolean immutable=false; try { result.heroes.clear(); } catch (UnsupportedOperationException ex) { immutable=true; }
            check(immutable,"result cannot mutate cached hero list");
        } finally {
            /* 只清理由本测试创建的临时树。 | Remove only the temporary tree created by this test. */
            try (java.util.stream.Stream<Path> paths=Files.walk(temp)) {
                for (Path path:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator) Files.delete(path);
            }
        }
        System.out.println("SaveSummaryReader OK ("+checks+" synthetic checks; no player data)");
    }
}
