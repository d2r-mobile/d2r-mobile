// 数据包导入的规则与验收：哪些条目合法、解到哪里、什么才算一个能开机的包、以及新模块怎么换上去。
//
// 刻意只用 java.io / java.util.zip，不碰任何 Android API —— 理由和 SaveArchive 一样：导入是这条链上最容易
// 出错的一环（一个残缺的包能把上一次能用的安装打废），所以整套规则要能在电脑上跑单元测试，
// 见 dm/android/tests/PackageArchiveTest.java。
//
// Package import rules and acceptance: which entries are legal, where they land, what counts as a package the game
// can actually boot, and how the new modules get swapped in. Deliberately plain Java (java.io / java.util.zip, no
// Android APIs) for the same reason as SaveArchive: import is the error-prone step -- a partial package can wreck a
// previously working install -- so the whole rule set must be unit-testable on a PC
// (dm/android/tests/PackageArchiveTest.java).
package org.d2r.android.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class PackageArchive {
    // 认得的数据包格式版本；包自报别的版本就明确拒绝，不猜。契约见 docs/ANDROID_PACKAGE.zh-CN.md。
    // The package format version this build understands; anything else is refused rather than guessed at.
    static final int FORMAT = 1;
    static final String MANIFEST = "manifest.json";
    static final String DERIVED = "derived.tsv";
    static final String[] PREFIXES = {"game/", "romfs/", "shaders/"};

    // 启动时游戏按名字打开这些文件，缺一个就在启动时崩（实测：FATAL open .../game/sdk.fixups.tsv）。
    // 这与 build_package.py 的 GAME_MODULES / GAME_LAYOUTS 是同一份契约，改动必须两边一起。
    // Boot opens these by name and a missing one aborts startup (measured: FATAL open
    // .../game/sdk.fixups.tsv). This is the same contract as build_package.py's GAME_MODULES / GAME_LAYOUTS;
    // change both sides together.
    static final String[] MODULE_NAMES = {"main", "sdk", "subsdk0"};
    static final String[] LAYOUT_KINDS = {"fixups", "gates"};

    // 上限只用来挡恶意或损坏的包：真实包约 9 GB、约 4.2 万个条目、清单 310 字节，离这些值很远。
    // The caps only stop a hostile or corrupt archive: a real package is about 9 GB, about 42k entries and a
    // 310-byte manifest, far from these numbers.
    private static final long MAX_ENTRY_BYTES = 12L << 30;
    private static final long MAX_TOTAL_BYTES = 24L << 30;
    private static final int MAX_ENTRIES = 300000;
    private static final int MAX_MANIFEST_BYTES = 1 << 20;

    private PackageArchive() { }

    // 失败时带上"是哪条规则"的键与参数，由界面层翻成中英文字 —— 规则层不碰 i18n，测试也只断言键。
    // A failure carries the rule's key and its arguments; the UI layer turns them into text. This layer holds no
    // i18n, and the tests assert on the key.
    static final class Failure extends IOException {
        final String key;
        final Object[] arguments;

        Failure(String key, Object... arguments) {
            super(key + (arguments.length == 0 ? "" : " " + Arrays.toString(arguments)));
            this.key = key;
            this.arguments = arguments;
        }
    }

    static final class Counts {
        final int romfs;
        final int shaders;
        final int game;

        Counts(int romfs, int shaders, int game) {
            this.romfs = romfs;
            this.shaders = shaders;
            this.game = game;
        }
    }

    interface Sink {
        void progress(int percent);

        boolean cancelled();
    }

    // 清单里我们关心的几个数。**声明**与**实测**分开：包自报的数只用于比对，永远不当结果显示。
    // The handful of numbers we care about in the manifest. Declaration and measurement stay apart: what the
    // package claims is only ever compared, never shown as the result.
    static final class Declaration {
        static final int ABSENT = -1;
        final int format;
        final int game;
        final int romfs;
        final int shaders;

        Declaration(int format, int game, int romfs, int shaders) {
            this.format = format;
            this.game = game;
            this.romfs = romfs;
            this.shaders = shaders;
        }
    }

    // 清单由**调用方**解析：Android 端用 org.json，测试里直接构造。规则层因此不依赖任何 JSON 实现，
    // 测试也不必绕过 JSON 才能验证规则。
    // The caller parses the manifest (Android uses org.json, the test builds a Declaration directly), so this layer
    // depends on no JSON implementation and the tests exercise the rules rather than a parser.
    interface ManifestReader {
        Declaration read(byte[] manifest) throws IOException;
    }

    // 九个必需文件：三个模块，每个配一份 fixups 与一份 gates。
    // The nine required files: three modules, each with a fixups and a gates layout.
    static List<String> requiredModules() {
        List<String> names = new ArrayList<>();
        for (String module : MODULE_NAMES) {
            names.add("libdmguest_" + module + ".so");
            for (String kind : LAYOUT_KINDS) names.add(module + "." + kind + ".tsv");
        }
        return names;
    }

    // 条目名必须"干净"：不许有空段、"." 或 ".."，也不许绝对路径或反斜杠。
    // 只靠规范化路径是不够的 —— `romfs/../manifest.json` 规范化后落在包目录之内，会在正常写清单**之前**
    // 就把"完成标记"放进去，绕过"最后才写 manifest"的顺序保证。
    // Entry names must be clean: no empty, "." or ".." segment, no absolute path, no backslash. Relying on the
    // canonical path alone is not enough: `romfs/../manifest.json` canonicalises to a path inside the package
    // root, so it would plant the completion marker before the manifest is written, defeating the write-it-last
    // ordering guarantee.
    static boolean clean(String name) {
        if (name.isEmpty() || name.startsWith("/") || name.indexOf('\\') >= 0) return false;
        for (String part : name.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        }
        return true;
    }

    // 包根上的 derived.tsv 也允许（三张派生表的内容）；按**完整文件名**放行，不放宽成任意根文件。
    // The package-root derived.tsv is allowed too (the content of the three derived tables); it is admitted by its
    // exact name rather than by loosening the rule for arbitrary root files.
    static boolean allowed(String name) {
        if (!clean(name)) return false;
        if (name.equals(DERIVED)) return true;
        for (String prefix : PREFIXES) {
            if (name.startsWith(prefix) && name.length() > prefix.length()) return true;
        }
        return false;
    }

    // "game" / "romfs" / "shaders" / "derived.tsv"，不属于任何一类则 null。
    static String category(String name) {
        if (name.equals(DERIVED)) return DERIVED;
        for (String prefix : PREFIXES) {
            if (name.startsWith(prefix)) return prefix.substring(0, prefix.length() - 1);
        }
        return null;
    }

    // 解包 + 验收 + 换入。模块先落在暂存目录里，**验收不过就不换上去**，上一次能用的模块一个都不会丢。
    // Unpack, validate, then swap. The modules land in a staging directory first and are only moved into place once
    // acceptance passes, so a refused package never costs the previous, working module set.
    //
    // 顺序是刻意的：旧的 manifest.json 先撤，新的最后才落盘。游戏用"manifest.json 在不在"判断包能不能用，
    // 所以中途失败（空间不足、取消、zip 损坏）留下的是个没有清单的目录 —— 游戏宁可不认这个包，也不要读到半个。
    // The order is deliberate: the old manifest.json is removed first and the new one written last. The game treats
    // "manifest.json present" as "package usable", so an interrupted import leaves a directory with no manifest:
    // refusing the package beats reading half of one.
    static Counts unpack(InputStream raw, File assets, File modules, long totalBytes, Sink sink,
                         ManifestReader reader) throws IOException {
        File staging = new File(modules.getParentFile(), modules.getName() + ".import");
        File manifest = new File(assets, MANIFEST);
        deleteTree(staging);
        if (manifest.isFile() && !manifest.delete()) throw new IOException("cannot clear " + manifest);

        byte[] manifestBytes = null;
        int romfs = 0, shaders = 0, game = 0, entries = 0;
        long written = 0, lastReported = 0;
        byte[] buffer = new byte[1 << 20];
        try {
            try (ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (sink.cancelled()) throw new Failure("dmAndroidLaunchImportCancelled");
                    String name = entry.getName();
                    if (entry.isDirectory()) continue;
                    if (++entries > MAX_ENTRIES) throw new Failure("dmAndroidLaunchImportTooBig");
                    if (name.equals(MANIFEST)) {
                        // 读进内存，最后再落盘；顺序说明见函数上方。
                        // Kept in memory and written last; see the ordering note above.
                        manifestBytes = readAll(zip, MAX_MANIFEST_BYTES);
                        continue;
                    }
                    if (!allowed(name)) throw new Failure("dmAndroidLaunchImportBadEntry", name);
                    String category = category(name);
                    // game/ 剥掉前缀后落到暂存目录，其余原样落到外部目录。
                    // game/ drops its prefix and lands in the staging directory; everything else goes external.
                    File destination = "game".equals(category)
                        ? under(staging, name.substring("game/".length()))
                        : under(assets, name);
                    File parent = destination.getParentFile();
                    if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                        throw new IOException("cannot create " + parent);
                    }
                    long size = writeFile(zip, destination, buffer);
                    if (size > MAX_ENTRY_BYTES || written + size > MAX_TOTAL_BYTES) {
                        throw new Failure("dmAndroidLaunchImportTooBig");
                    }
                    written += size;
                    if ("romfs".equals(category)) romfs++;
                    else if ("shaders".equals(category)) shaders++;
                    else if ("game".equals(category)) game++;
                    if (totalBytes > 0 && written - lastReported > totalBytes / 100) {
                        lastReported = written;
                        sink.progress((int) Math.min(100, written * 100 / totalBytes));
                    }
                }
            }

            // 验收：先校验，后破坏。到这里为止只动过外部目录与暂存目录，上一次的模块还在。
            // Acceptance: validate before destroying. Up to here only the external directory and staging were touched;
            // the previous modules are still in place.
            if (manifestBytes == null) {
                throw new Failure("dmAndroidLaunchImportIncomplete", MANIFEST);
            }
            List<String> missing = new ArrayList<>();
            if (romfs == 0) missing.add("romfs/");
            if (shaders == 0) missing.add("shaders/");
            if (game == 0) missing.add("game/");
            for (String name : requiredModules()) {
                File file = new File(staging, name);
                // 布局文件允许是空的（subsdk0.gates.tsv 本来就是 0 字节），模块本体不能是空的。
                // A layout file may be empty (subsdk0.gates.tsv really is 0 bytes); a module itself may not.
                if (!file.isFile() || (name.endsWith(".so") && file.length() == 0)) missing.add("game/" + name);
            }
            if (!missing.isEmpty()) {
                throw new Failure("dmAndroidLaunchImportIncomplete", join(missing));
            }

            Declaration declaration = reader.read(manifestBytes);

            // 格式版本：契约写明"不认识的版本必须明确报错并拒绝导入"，那就真的拒绝。
            // Format version: the contract says an unknown version must be refused outright, so refuse it.
            if (declaration.format != FORMAT) {
                throw new Failure("dmAndroidLaunchImportFormat",
                                  declaration.format < 0 ? "?" : String.valueOf(declaration.format),
                                  String.valueOf(FORMAT));
            }

            // 清单自报的数量必须与实际解出的一致。包被截断时清单还是完好的，这一条是唯一能发现它的地方；
            // 反过来也不能拿清单的数字当结果显示，那等于用一个"自称"冒充"实际"。
            // The manifest's declared counts must match what was actually unpacked. A truncated archive still has an
            // intact manifest, and this is the only place that catches it; and the manifest's numbers must never be
            // shown as the result, which would pass a claim off as a measurement.
            //
            // counts.derived 是**行数**（三张表的行数），不是文件数，所以不在这里比对。
            // counts.derived is a row count (the three tables), not a file count, so it is not compared here.
            requireCount("game", declaration.game, game);
            requireCount("romfs", declaration.romfs, romfs);
            requireCount("shaders", declaration.shaders, shaders);

            swap(staging, modules);
            writeManifest(manifest, manifestBytes);
            return new Counts(romfs, shaders, game);
        } catch (IOException error) {
            deleteTree(staging);          // 失败不留半个暂存目录 | no half-staging tree survives a failure
            throw error;
        }
    }

    // 目录内的规范化落点；名字已经过 clean()，所以这里只做兜底。
    // The normalised destination inside target; the name already passed clean(), so this is only a backstop.
    private static File under(File target, String name) throws IOException {
        File candidate = new File(target, name);
        String root = target.getCanonicalPath() + File.separator;
        if (!candidate.getCanonicalPath().startsWith(root)) {
            throw new Failure("dmAndroidLaunchImportBadEntry", name);
        }
        return candidate;
    }

    // 新的模块换上去：旧的先改名留一手，新的就位后才删旧的；中间任何一步失败都退回去，两头不会都空。
    // Swap the new modules in: the old set is renamed aside, the new one moved in, and the old deleted only after
    // that; any failure rolls back, so the player is never left with neither.
    static void swap(File staged, File target) throws IOException {
        File previous = new File(target.getParentFile(), target.getName() + ".previous");
        deleteTree(previous);
        boolean hadPrevious = target.exists();
        if (hadPrevious && !target.renameTo(previous)) throw new IOException("cannot move " + target + " aside");
        if (!staged.renameTo(target)) {
            if (hadPrevious) previous.renameTo(target);
            throw new IOException("cannot move " + staged + " into place");
        }
        deleteTree(previous);
    }

    // 完成标记最后落盘，而且是原子的：先写 .tmp 再改名。写了一半的清单会让游戏以为包是好的。
    // The completion marker is written last and atomically (tmp, then rename): a half-written manifest would make
    // the game believe the package is ready.
    private static void writeManifest(File manifest, byte[] bytes) throws IOException {
        File temporary = new File(manifest.getParentFile(), manifest.getName() + ".tmp");
        try (OutputStream out = new FileOutputStream(temporary, false)) {
            out.write(bytes);
        }
        if (manifest.exists() && !manifest.delete()) throw new IOException("cannot replace " + manifest);
        if (!temporary.renameTo(manifest)) throw new IOException("cannot move " + temporary + " into place");
    }

    private static void requireCount(String key, int declared, int extracted) throws IOException {
        if (declared != Declaration.ABSENT && declared != extracted) {
            throw new Failure("dmAndroidLaunchImportCounts", key, String.valueOf(declared), String.valueOf(extracted));
        }
    }

    private static String join(List<String> names) {
        StringBuilder text = new StringBuilder();
        for (String name : names) {
            if (text.length() > 0) text.append(", ");
            text.append(name);
        }
        return text.toString();
    }

    // 递归删除一棵目录树；只用于应用自己的包目录，不碰别处。
    // Recursive delete, used only on the app's own package directory.
    static void deleteTree(File target) {
        if (target == null) return;
        File[] children = target.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        if (target.exists() && !target.delete()) target.deleteOnExit();
    }

    private static byte[] readAll(InputStream source, int limit) throws IOException {
        ByteArrayOutputStream collected = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = source.read(buffer)) > 0) {
            if (collected.size() + count > limit) throw new Failure("dmAndroidLaunchImportTooBig");
            collected.write(buffer, 0, count);
        }
        return collected.toByteArray();
    }

    private static long writeFile(InputStream source, File destination, byte[] buffer) throws IOException {
        long written = 0;
        try (OutputStream out = new FileOutputStream(destination, false)) {
            int count;
            while ((count = source.read(buffer)) > 0) {
                out.write(buffer, 0, count);
                written += count;
            }
        }
        return written;
    }
}
