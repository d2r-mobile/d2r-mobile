package com.xiaoboluo.d2r.widget;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/** 已提交目录的只读摘要；不加载游戏、不修改存档。 | Read-only summaries of the committed directory; never loads the game or modifies saves. */
public final class SaveSummaryReader {
    private static final int MAX_SAVE_BYTES = 1024 * 1024;
    private static final int MAX_CHARACTERS = 256;
    private static final int[] WIDTH = {10,10,10,10,10,8,21,21,21,21,21,21,7,32,25,25};
    public enum State { EMPTY, READY, PARTIAL, ERROR }
    public static final class Hero {
        public final String name;
        public final int classIndex, level;
        public final long lastPlayed, experience, gold;
        private Hero(String name,int cls,int level,long time,long experience,long gold) {
            this.name=name; classIndex=cls; this.level=level; lastPlayed=time; this.experience=experience; this.gold=gold;
        }
    }
    public static final class Result {
        public final State state;
        public final List<Hero> heroes;
        public final int rejectedFiles;
        private Result(State state,List<Hero> heroes,int rejected) {
            this.state=state; this.heroes=Collections.unmodifiableList(new ArrayList<>(heroes)); rejectedFiles=rejected;
        }
    }
    private SaveSummaryReader() {}

    /** root必须是files/save，不能是运行中的save.work。 | root must be files/save, never the active save.work directory. */
    public static Result read(Path root) {
        List<Hero> heroes=new ArrayList<>(); int rejected=0; boolean scanFailed=false;
        if (root==null || root.getFileName()==null || !root.getFileName().toString().equals("save") || Files.isSymbolicLink(root)) return new Result(State.ERROR,heroes,0);
        if (!Files.exists(root,LinkOption.NOFOLLOW_LINKS)) return new Result(State.EMPTY,heroes,0);
        if (!Files.isDirectory(root,LinkOption.NOFOLLOW_LINKS)) return new Result(State.ERROR,heroes,0);
        try (DirectoryStream<Path> directories=Files.newDirectoryStream(root)) {
            int visited=0;
            for (Path dir:directories) {
                if (++visited>MAX_CHARACTERS) { ++rejected; break; }
                if (!Files.isDirectory(dir,LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(dir)) continue;
                Path save=dir.resolve(dir.getFileName().toString()+".d2s");
                if (!Files.exists(save,LinkOption.NOFOLLOW_LINKS)) continue;
                if (!Files.isRegularFile(save,LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(save)) { ++rejected; continue; }
                try {
                    long size=Files.size(save);
                    if (size<0x15b || size>MAX_SAVE_BYTES) { ++rejected; continue; }
                    /* 限长读取抵御并发增长；只读已提交版，校验和失败保留错误态。 | Bound reads even if a file grows concurrently; checksum failures remain errors. */
                    byte[] bytes;
                    try (java.io.InputStream in=Files.newInputStream(save); java.io.ByteArrayOutputStream out=new java.io.ByteArrayOutputStream((int)size)) {
                        byte[] chunk=new byte[8192]; int n;
                        while (out.size()<=MAX_SAVE_BYTES && (n=in.read(chunk,0,Math.min(chunk.length,MAX_SAVE_BYTES+1-out.size())))!=-1) out.write(chunk,0,n);
                        bytes=out.toByteArray();
                    }
                    Hero hero=parse(bytes);
                    if (hero==null) ++rejected; else heroes.add(hero);
                } catch (IOException | SecurityException ex) { ++rejected; }
            }
        } catch (IOException | SecurityException ex) { scanFailed=true; }
        heroes.sort(Comparator.comparingLong((Hero h)->h.lastPlayed).reversed().thenComparing(h->h.name));
        return new Result(heroes.isEmpty()?(rejected>0 || scanFailed?State.ERROR:State.EMPTY):(rejected>0 || scanFailed?State.PARTIAL:State.READY),heroes,rejected);
    }

    /** 仅支持已有本仓依据的D2R版本；没有经验表就不计算百分比。 | Only accepts D2R versions evidenced in this repository; no experience table means no percentage. */
    static Hero parse(byte[] bytes) {
        if (bytes==null || bytes.length<0x15b || bytes.length>MAX_SAVE_BYTES) return null;
        ByteBuffer data=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (data.getInt(0)!=0xaa55aa55) return null;
        int version=data.getInt(4);
        if (version!=0x61 && version!=0x69) return null;
        if (Integer.toUnsignedLong(data.getInt(8))!=bytes.length || data.getInt(12)!=checksum(bytes)) return null;
        int cls=Byte.toUnsignedInt(bytes[0x18]), level=Byte.toUnsignedInt(bytes[0x1b]);
        if (cls>7 || level<1 || level>99) return null;
        int end=0x12b;
        while (end<0x15b && bytes[end]!=0) ++end;
        if (end==0x12b || end==0x15b) return null;
        final String name;
        try { name=StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes,0x12b,end-0x12b)).toString(); }
        catch (CharacterCodingException ex) { return null; }
        for (int i=0;i<name.length();++i) if (Character.isISOControl(name.charAt(i))) return null;
        long experience=-1, gold=-1;
        /* 复用鸿蒙gf位流语义，但不把名称内的gf当统计段；候选必须有合法终止符和一致等级。 | Reuse the Harmony gf bitstream semantics; ignore gf in the name and require termination and a matching level. */
        int limit=Math.min(bytes.length,2048);
        for (int i=0x100;i+1<limit;++i) {
            if (i>=0x12a && i<0x15b) continue;
            if (bytes[i]==0x67 && bytes[i+1]==0x66) {
                long[] stats=stats(bytes,i+2,limit);
                if (stats!=null && stats[12]==level) {
                    experience=stats[13];
                    if (stats[14]>=0 && stats[15]>=0) gold=stats[14]+stats[15];
                    break;
                }
            }
        }
        return new Hero(name,cls,level,Integer.toUnsignedLong(data.getInt(0x20)),experience,gold);
    }
    private static long[] stats(byte[] bytes,int start,int limit) {
        long[] values=new long[16]; java.util.Arrays.fill(values,-1);
        int bit=start*8, last=-1;
        for (int i=0;i<17;++i) {
            if (bit+9>limit*8) return null;
            int id=(int)readBits(bytes,bit,9); bit+=9;
            if (id==0x1ff) return values;
            if (id<=last || id>=WIDTH.length || bit+WIDTH[id]>limit*8) return null;
            values[id]=readBits(bytes,bit,WIDTH[id]); bit+=WIDTH[id]; last=id;
        }
        return null;
    }
    private static long readBits(byte[] bytes,int offset,int length) {
        long value=0;
        for (int i=0;i<length;++i) if (((bytes[(offset+i)/8]>>>((offset+i)%8))&1)!=0) value|=1L<<i;
        return value;
    }
    private static int checksum(byte[] bytes) {
        int sum=0;
        for (int i=0;i<bytes.length;++i) sum=Integer.rotateLeft(sum,1)+(i>=12 && i<16?0:Byte.toUnsignedInt(bytes[i]));
        return sum;
    }
}
